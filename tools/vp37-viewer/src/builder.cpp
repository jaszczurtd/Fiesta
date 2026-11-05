#include "builder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "log_source.h"
#include "recording_cache.h"

namespace vp37 {

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

// Keep the cache limit representable on 32-bit hosts.
constexpr size_t kMaxCacheBytes = static_cast<size_t>(
    std::min<uint64_t>(UINT64_C(8) << 30, std::numeric_limits<size_t>::max()));

double seconds_since(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool ends_with(const std::string &text, const std::string &suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string strip_log_extension(const std::string &rel) {
  if (ends_with(rel, ".log.gz"))
    return rel.substr(0, rel.size() - 7);
  if (ends_with(rel, ".log"))
    return rel.substr(0, rel.size() - 4);
  return rel;
}

std::string parent_of(const std::string &rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? std::string() : rel.substr(0, slash);
}

std::string base_name(const std::string &rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

template <typename Work>
void parallel_for(size_t count, unsigned jobs, Work work) {
  std::atomic<size_t> next{0};
  std::vector<std::thread> threads;
  const unsigned n = std::max(
      1u, std::min<unsigned>(jobs, unsigned(std::max<size_t>(count, 1))));
  for (unsigned i = 0; i < n; ++i) {
    threads.emplace_back([&] {
      for (size_t at = next++; at < count; at = next++)
        work(at);
    });
  }
  for (std::thread &thread : threads)
    thread.join();
}

struct LogEntry {
  FileStamp stamp;
  std::string sha;
  std::string status; // "vp37" or "other: <reason>"
  RunSummary summary;
  bool read = false, parsed = false;
  size_t bytes = 0;
};

// Remembered SHA-256 and kind of each log by (path, size, mtime), so an
// unchanged 14 GB tree is not read again. The cache key itself is the SHA.
std::map<std::string, LogEntry> load_memo(const std::string &path) {
  std::map<std::string, LogEntry> memo;
  std::string text;
  try {
    text = read_file(path, size_t(64) << 20);
  } catch (const Error &) {
    return memo;
  }
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    const std::string line = text.substr(start, end - start);
    start = end + 1;
    std::vector<std::string> fields;
    size_t from = 0;
    for (int i = 0; i < 4; ++i) {
      const size_t tab = line.find('\t', from);
      if (tab == std::string::npos)
        break;
      fields.push_back(line.substr(from, tab - from));
      from = tab + 1;
    }
    if (fields.size() != 4)
      continue;
    LogEntry entry;
    entry.stamp.rel = fields[0];
    entry.stamp.size = std::stoull(fields[1]);
    entry.stamp.mtime_ns = std::stoll(fields[2]);
    entry.sha = fields[3];
    entry.status = line.substr(from);
    memo[entry.stamp.rel] = std::move(entry);
  }
  return memo;
}

std::string summary_path(const std::string &cache, const std::string &sha) {
  std::string path = recording_cache_path(cache, sha);
  return path.substr(0, path.size() - 4) + ".summary.json";
}

RunSummary summarize(const Recording &rec) {
  RunSummary summary;
  summary.revision = rec.revision;
  summary.first_t = rec.first_t;
  summary.last_t = rec.last_t;
  summary.control_rows = rec.control.rows();
  summary.pulse_rows = rec.pulse.rows();
  for (const TestSegment &test : rec.tests) {
    if (std::find(summary.tests.begin(), summary.tests.end(), test.test) ==
        summary.tests.end()) {
      summary.tests.push_back(test.test);
    }
  }
  summary.steps = rec.steps.size();
  summary.time_reversals = rec.time_reversals;
  summary.undescribed = rec.undescribed;
  summary.unknown_kinds = rec.unknown_kinds;
  for (const HostEvent &host : rec.hosts)
    summary.host_marks.emplace_back(host.text, host.t);
  return summary;
}

std::string summary_json(const RunSummary &s) {
  JsonWriter out;
  out.begin_object();
  out.key("parser").integer(kParserVersion);
  out.key("revision").integer(s.revision);
  out.key("first_t").number(s.first_t);
  out.key("last_t").number(s.last_t);
  out.key("control_rows").integer(int64_t(s.control_rows));
  out.key("pulse_rows").integer(int64_t(s.pulse_rows));
  out.key("tests").begin_array();
  for (const std::string &test : s.tests)
    out.string(test);
  out.end_array();
  out.key("steps").integer(int64_t(s.steps));
  out.key("time_reversals").integer(int64_t(s.time_reversals));
  for (const auto &[name, counts] : {std::pair{"undescribed", &s.undescribed},
                                     {"unknown_kinds", &s.unknown_kinds}}) {
    out.key(name).begin_object();
    for (const auto &[key, count] : *counts)
      out.key(key).integer(int64_t(count));
    out.end_object();
  }
  out.key("host_marks").begin_array();
  for (const auto &[text, t] : s.host_marks)
    out.begin_array().string(text).number(t).end_array();
  out.end_array();
  out.end_object();
  return out.finish();
}

std::optional<RunSummary> read_summary(const std::string &path) {
  std::error_code error;
  if (!fs::is_regular_file(path, error))
    return std::nullopt;
  try {
    const auto doc = JsonDoc::load(path);
    const Json root = doc->root();
    if (root["parser"].number().value_or(-1) != kParserVersion)
      return std::nullopt;
    RunSummary s;
    s.revision = int(root["revision"].number().value_or(-1));
    s.first_t = root["first_t"].number().value_or(0);
    s.last_t = root["last_t"].number().value_or(0);
    s.control_rows = size_t(root["control_rows"].number().value_or(0));
    s.pulse_rows = size_t(root["pulse_rows"].number().value_or(0));
    for (const Json &test : root["tests"].items())
      s.tests.push_back(test.str());
    s.steps = size_t(root["steps"].number().value_or(0));
    s.time_reversals = size_t(root["time_reversals"].number().value_or(0));
    for (const Json &item : root["undescribed"].items()) {
      s.undescribed[item.key()] = size_t(item.number().value_or(0));
    }
    for (const Json &item : root["unknown_kinds"].items()) {
      s.unknown_kinds[item.key()] = size_t(item.number().value_or(0));
    }
    for (const Json &mark : root["host_marks"].items()) {
      s.host_marks.emplace_back(mark.at(0).str(),
                                mark.at(1).number().value_or(0));
    }
    return s;
  } catch (const Error &) {
    return std::nullopt;
  }
}

std::vector<FileStamp> find_files(const std::string &root,
                                  bool (*wanted)(const std::string &name)) {
  std::vector<FileStamp> files;
  for (const std::string &area : data_areas()) {
    const fs::path dir = fs::path(root) / area;
    std::error_code error;
    if (!fs::is_directory(dir, error))
      continue;
    for (auto it = fs::recursive_directory_iterator(dir, error);
         !error && it != fs::recursive_directory_iterator();
         it.increment(error)) {
      std::error_code entry_error;
      if (!it->is_regular_file(entry_error))
        continue;
      const std::string name = it->path().filename().generic_string();
      if (!wanted(name))
        continue;
      FileStamp stamp;
      stamp.rel = relative_to(root, it->path().generic_string());
      stamp.size = it->file_size(entry_error);
      stamp.mtime_ns =
          int64_t(it->last_write_time(entry_error).time_since_epoch().count());
      files.push_back(std::move(stamp));
    }
  }
  std::sort(
      files.begin(), files.end(),
      [](const FileStamp &a, const FileStamp &b) { return a.rel < b.rel; });
  return files;
}

bool is_log(const std::string &name) {
  return ends_with(name, ".log") || ends_with(name, ".log.gz");
}
bool is_metrics(const std::string &name) {
  return name.find("metrics") != std::string::npos && ends_with(name, ".json");
}

} // namespace

void index_runs(Model &model, const BuildOptions &options, BuildStats *stats) {
  const auto start = Clock::now();
  BuildStats local;
  BuildStats &st = stats ? *stats : local;
  const unsigned jobs = options.jobs
                            ? options.jobs
                            : std::max(1u, std::thread::hardware_concurrency());
  Index &index = model.index;
  index = Index();
  index.root = options.root;

  const std::string memo_path = options.cache + "/files.tsv";
  const auto memo = load_memo(memo_path);
  std::vector<LogEntry> logs;
  for (FileStamp &stamp : find_files(options.root, is_log)) {
    LogEntry entry;
    entry.stamp = std::move(stamp);
    logs.push_back(std::move(entry));
  }
  st.logs = logs.size();
  std::mutex skipped_mutex;
  parallel_for(logs.size(), jobs, [&](size_t i) {
    LogEntry &entry = logs[i];
    const auto known = memo.find(entry.stamp.rel);
    if (known != memo.end() && known->second.stamp.size == entry.stamp.size &&
        known->second.stamp.mtime_ns == entry.stamp.mtime_ns) {
      entry.sha = known->second.sha;
      entry.status = known->second.status;
      if (entry.status != "vp37")
        return;
      if (const auto summary =
              read_summary(summary_path(options.cache, entry.sha))) {
        std::error_code error;
        if (fs::is_regular_file(recording_cache_path(options.cache, entry.sha),
                                error)) {
          entry.summary = *summary;
          return;
        }
      }
    }
    try {
      const std::string text = read_log(options.root + "/" + entry.stamp.rel);
      entry.read = true;
      entry.bytes = text.size();
      entry.sha = sha256_hex(text);
      const std::string cache_file =
          recording_cache_path(options.cache, entry.sha);
      std::error_code error;
      if (const auto summary =
              read_summary(summary_path(options.cache, entry.sha));
          summary && fs::is_regular_file(cache_file, error)) {
        entry.summary = *summary;
        entry.status = "vp37";
        return;
      }
      if (text.find("VP37 C ") == std::string::npos) {
        entry.status = "other: brak linii VP37 C";
        return;
      }
      const Recording rec = parse_log(text);
      entry.parsed = true;
      if (rec.control.rows() < kMinControlRows) {
        entry.status = "other: mniej niż " + std::to_string(kMinControlRows) +
                       " linii VP37 C z czasem";
        return;
      }
      write_output(cache_file, serialize_recording(rec));
      entry.summary = summarize(rec);
      write_output(summary_path(options.cache, entry.sha),
                   summary_json(entry.summary));
      entry.status = "vp37";
    } catch (const std::exception &error) {
      entry.status = std::string("error: ") + error.what();
      std::lock_guard<std::mutex> lock(skipped_mutex);
      if (!options.quiet)
        std::fprintf(stderr, "vp37-viewer: %s\n", error.what());
    }
  });

  std::string memo_text;
  for (const LogEntry &entry : logs) {
    st.logs_read += entry.read;
    st.logs_parsed += entry.parsed;
    st.bytes_read += entry.bytes;
    if (entry.status.rfind("error:", 0) == 0) {
      index.skipped.push_back(entry.stamp.rel + ": " + entry.status);
      continue;
    }
    memo_text += entry.stamp.rel + "\t" + std::to_string(entry.stamp.size) +
                 "\t" + std::to_string(entry.stamp.mtime_ns) + "\t" +
                 entry.sha + "\t" + entry.status + "\n";
    if (entry.status != "vp37")
      index.skipped.push_back(entry.stamp.rel + ": " + entry.status);
  }
  write_output(memo_path, memo_text);

  // Runs: one per log content; `x.log` and `x.log.gz` with the same text are
  // one run (the plain log is the primary file).
  std::map<std::string, std::vector<const LogEntry *>> by_id;
  for (const LogEntry &entry : logs) {
    if (entry.status == "vp37")
      by_id[strip_log_extension(entry.stamp.rel)].push_back(&entry);
  }
  for (auto &[id, entries] : by_id) {
    std::sort(entries.begin(), entries.end(),
              [](const LogEntry *a, const LogEntry *b) {
                return ends_with(a->stamp.rel, ".log") >
                       ends_with(b->stamp.rel, ".log");
              });
    std::vector<std::vector<const LogEntry *>> groups;
    for (const LogEntry *entry : entries) {
      bool placed = false;
      for (auto &group : groups) {
        if (group.front()->sha == entry->sha) {
          group.push_back(entry);
          placed = true;
        }
      }
      if (!placed)
        groups.push_back({entry});
    }
    for (size_t g = 0; g < groups.size(); ++g) {
      Run run;
      run.id = g == 0 ? id
                      : id + (ends_with(groups[g].front()->stamp.rel, ".gz")
                                  ? ".gz"
                                  : ".log");
      run.series = series_of(groups[g].front()->stamp.rel);
      for (const std::string &area : data_areas()) {
        if (run.series.rfind(area + "/", 0) == 0 || run.series == area)
          run.area = area;
      }
      run.local = run.id.substr(std::min(run.id.size(), run.series.size() + 1));
      run.log = groups[g].front()->stamp;
      for (size_t k = 1; k < groups[g].size(); ++k)
        run.duplicates.push_back(groups[g][k]->stamp);
      run.sha = groups[g].front()->sha;
      run.summary = groups[g].front()->summary;
      index.run_by_id[run.id] = index.runs.size();
      index.run_by_file[run.log.rel] = index.runs.size();
      for (const FileStamp &duplicate : run.duplicates)
        index.run_by_file[duplicate.rel] = index.runs.size();
      index.runs.push_back(std::move(run));
    }
  }
  // `<x>-raw/<x>-raw.log` is the raw-position copy of run `<x>/<x>`.
  for (Run &run : index.runs) {
    const std::string dir = base_name(parent_of(run.id));
    if (!ends_with(dir, "-raw") || base_name(run.id) != dir)
      continue;
    const std::string base = dir.substr(0, dir.size() - 4);
    const std::string base_id =
        parent_of(parent_of(run.id)) + "/" + base + "/" + base;
    if (index.run_by_id.count(base_id))
      run.raw_of = base_id;
  }
  st.runs = index.runs.size();
  st.skipped = index.skipped.size();

  // Metrics files and their binding.
  const auto metrics_files = find_files(options.root, is_metrics);
  index.metrics.resize(metrics_files.size());
  parallel_for(metrics_files.size(), jobs, [&](size_t i) {
    MetricsFile &file = index.metrics[i];
    file.rel = metrics_files[i].rel;
    try {
      std::string text =
          read_file(options.root + "/" + file.rel, size_t(256) << 20);
      file.sha = sha256_hex(text);
      file.doc =
          std::shared_ptr<JsonDoc>(JsonDoc::parse(std::move(text), file.rel));
      if (!file.doc->root().is_object())
        file.doc.reset();
    } catch (const Error &error) {
      file.reason = error.what();
    }
  });
  bind_metrics(index);
  build_model(model, options.descriptions);
  st.metrics_files = index.metrics.size();
  st.metrics_bound = 0;
  for (const MetricsFile &file : index.metrics)
    st.metrics_bound += is_bound(file.status);
  st.index_s = seconds_since(start);
}

BuildStats build_site(const BuildOptions &options) {
  const auto start = Clock::now();
  BuildStats stats;
  Model model;
  index_runs(model, options, &stats);

  const auto pages_start = Clock::now();
  const unsigned jobs = options.jobs
                            ? options.jobs
                            : std::max(1u, std::thread::hardware_concurrency());
  std::atomic<size_t> written{0}, kept{0};
  std::mutex error_mutex;
  std::vector<std::string> errors;
  parallel_for(model.index.runs.size(), jobs, [&](size_t i) {
    const Run &run = model.index.runs[i];
    const std::string slug = run_slug(run.id);
    const std::string stamp = run_stamp(model, run);
    const std::string stamp_path = options.cache + "/stamps/" + slug + ".txt";
    std::error_code error;
    if (fs::is_regular_file(options.site + "/run/" + slug + ".html", error)) {
      try {
        if (read_file(stamp_path, size_t(16) << 20) == stamp) {
          ++kept;
          return;
        }
      } catch (const Error &) {
      }
    }
    try {
      const auto rec = deserialize_recording(read_file(
          recording_cache_path(options.cache, run.sha), kMaxCacheBytes));
      if (!rec)
        throw Error("damaged cache entry for " + run.log.rel);
      write_run_pages(model, run, *rec, options.site);
      write_output(stamp_path, stamp);
      ++written;
    } catch (const std::exception &failure) {
      std::lock_guard<std::mutex> lock(error_mutex);
      errors.push_back(run.id + ": " + failure.what());
    }
  });
  for (const std::string &error : errors)
    std::fprintf(stderr, "vp37-viewer: %s\n", error.c_str());
  if (!errors.empty())
    throw Error(std::to_string(errors.size()) + " run pages failed");
  stats.run_pages_written = written;
  stats.run_pages_kept = kept;
  write_shared_pages(model, options.site, options.web);

  // Outputs of runs that disappeared, and cache entries nothing uses.
  std::set<std::string> slugs, shas;
  for (const Run &run : model.index.runs) {
    slugs.insert(run_slug(run.id));
    shas.insert(run.sha);
  }
  std::error_code error;
  for (const auto &[dir, suffix] :
       {std::pair{options.site + "/run", std::string(".html")},
        {options.cache + "/stamps", std::string(".txt")}}) {
    if (!fs::is_directory(dir, error))
      continue;
    for (const auto &entry : fs::directory_iterator(dir, error)) {
      std::string name = entry.path().filename().generic_string();
      if (ends_with(name, suffix))
        name.erase(name.size() - suffix.size());
      if (!slugs.count(name))
        fs::remove(entry.path(), error);
    }
  }
  if (fs::is_directory(options.site + "/data", error)) {
    for (const auto &entry :
         fs::directory_iterator(options.site + "/data", error)) {
      if (entry.is_directory(error) &&
          !slugs.count(entry.path().filename().generic_string())) {
        fs::remove_all(entry.path(), error);
      }
    }
  }
  const std::string current = "-p" + std::to_string(kParserVersion) + ".";
  if (fs::is_directory(options.cache + "/rec", error)) {
    for (auto it =
             fs::recursive_directory_iterator(options.cache + "/rec", error);
         !error && it != fs::recursive_directory_iterator();
         it.increment(error)) {
      if (!it->is_regular_file(error))
        continue;
      const std::string name = it->path().filename().generic_string();
      const std::string sha = name.substr(0, 64);
      // Unknown or older parser versions and logs no longer present.
      if (name.find(current) == std::string::npos)
        fs::remove(it->path(), error);
      else if (!shas.count(sha)) {
        // A SHA of a non-run log never has a cache entry; this one is gone.
        fs::remove(it->path(), error);
      }
    }
  }
  stats.pages_s = seconds_since(pages_start);
  stats.total_s = seconds_since(start);
  return stats;
}

} // namespace vp37
