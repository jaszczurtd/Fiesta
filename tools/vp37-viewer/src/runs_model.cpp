#include "runs_model.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>

#include "log_source.h"

namespace vp37 {

namespace fs = std::filesystem;

namespace {

bool starts_with(const std::string &text, const std::string &prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

std::string base_name(const std::string &rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

std::string parent_of(const std::string &rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? std::string() : rel.substr(0, slash);
}

std::string inside(const std::string &series, const std::string &rel) {
  return starts_with(rel, series + "/") ? rel.substr(series.size() + 1) : rel;
}

// reference-a2 -> 2, candidate-x1 -> 1; nothing for names without digits.
std::optional<int> trailing_number(const std::string &name) {
  size_t end = name.size();
  while (end > 0 && !std::isdigit(static_cast<unsigned char>(name[end - 1]))) {
    if (name[end - 1] == '-' || name[end - 1] == '/')
      return std::nullopt;
    --end;
  }
  size_t begin = end;
  while (begin > 0 && std::isdigit(static_cast<unsigned char>(name[begin - 1])))
    --begin;
  if (begin == end || end - begin > 6)
    return std::nullopt;
  return std::stoi(name.substr(begin, end - begin));
}

std::string kind_from_name(const std::string &name) {
  // An aborted run has no role in a comparison, whatever its prefix.
  if (name.find("abort") != std::string::npos)
    return std::string();
  if (starts_with(name, "reference"))
    return "reference";
  if (starts_with(name, "candidate"))
    return "candidate";
  if (starts_with(name, "warm"))
    return "warmup";
  return std::string();
}

std::shared_ptr<JsonDoc> try_load(const std::string &path) {
  try {
    return std::shared_ptr<JsonDoc>(JsonDoc::load(path));
  } catch (const Error &) {
    return nullptr;
  }
}

// Run whose log a comparison names (absolute, relative or moved path). A
// raw-position log `<x>-raw/<x>-raw.log` that was deleted after the
// analysis still names run `<x>` of the same iteration.
const Run *run_for_source(const Index &index, const std::string &series,
                          const std::string &file_rel,
                          const std::string &source, bool *raw = nullptr) {
  if (raw)
    *raw = false;
  if (source.empty())
    return nullptr;
  for (const std::string &candidate :
       source_candidates(index.root, file_rel, source)) {
    const std::string rel = relative_to(
        index.root, fs::path(candidate).lexically_normal().generic_string());
    const auto found = index.run_by_file.find(rel);
    if (found != index.run_by_file.end()) {
      const Run &run = index.runs[found->second];
      if (raw)
        *raw = !run.raw_of.empty();
      return &run;
    }
  }
  const std::string dir = base_name(parent_of(source));
  if (dir.size() > 4 && dir.compare(dir.size() - 4, 4, "-raw") == 0 &&
      base_name(source).rfind(dir + ".log", 0) == 0) {
    const std::string name = dir.substr(0, dir.size() - 4);
    if (const Run *run = index.find(series + "/" + name + "/" + name)) {
      if (raw)
        *raw = true;
      return run;
    }
  }
  return nullptr;
}

std::string started_of(const Index &index, const Run &run) {
  const std::string path =
      index.root + "/" + parent_of(run.log.rel) + "/image.json";
  const auto doc = try_load(path);
  return doc ? doc->root()["started"].str() : std::string();
}

} // namespace

std::string run_name(const Run &run) {
  const size_t slash = run.local.find('/');
  if (slash != std::string::npos &&
      run.local.find('/', slash + 1) == std::string::npos &&
      run.local.substr(0, slash) == run.local.substr(slash + 1)) {
    return run.local.substr(0, slash);
  }
  return run.local;
}

std::string description_file_name(const std::string &series) {
  std::string name = series;
  std::replace(name.begin(), name.end(), '/', '~');
  return name + ".runs.json";
}

IterationDescription parse_description(const Json &root,
                                       const std::string &series) {
  IterationDescription out;
  out.series = series;
  if (root["formatVersion"].number().value_or(0) != kRunsFormatVersion) {
    throw Error("runs.json formatVersion is not " +
                std::to_string(kRunsFormatVersion));
  }
  for (const Json &note : root["notes"].items())
    out.notes.push_back(note.str());
  for (const Json &item : root["variants"].items()) {
    Variant variant;
    variant.id = item["id"].str();
    variant.ecu_sha256 = item["ecu_sha256"].str();
    variant.adjustometer_sha256 = item["adjustometer_sha256"].str();
    variant.defines = item["defines"].str();
    out.variants.push_back(std::move(variant));
  }
  for (const Json &item : root["runs"].items()) {
    RunRole run;
    run.id = item["id"].str();
    run.log = item["log"]["path"].str();
    run.sha256 = item["log"]["sha256"].str();
    run.kind = item["kind"].str();
    if (const auto repeat = item["repeat"].number())
      run.repeat = static_cast<int>(*repeat);
    run.variant = item["variant"].str();
    for (const Json &metrics : item["metrics"].items()) {
      run.metrics.push_back({metrics["path"].str(), metrics["basis"].str()});
    }
    if (run.id.empty() || run.log.empty())
      throw Error("runs.json run without id or log");
    out.runs.push_back(std::move(run));
  }
  for (const Json &item : root["comparisons"].items()) {
    Comparison comparison;
    comparison.id = item["id"].str();
    for (const Json &candidate : item["candidates"].items()) {
      comparison.candidates.push_back(candidate.str());
    }
    for (const Json &reference : item["references"].items()) {
      comparison.references.push_back(
          {reference["run"].str(), reference["position"].str()});
    }
    comparison.basis = item["basis"].str("filtered");
    comparison.result_file = item["result_file"].str();
    comparison.described_from = item["described_from"].str();
    out.comparisons.push_back(std::move(comparison));
  }
  return out;
}

std::optional<IterationDescription>
load_description(const std::string &root, const std::string &series,
                 const std::string &descriptions_dir) {
  for (const std::string &path :
       {root + "/" + series + "/runs.json",
        descriptions_dir + "/" + description_file_name(series)}) {
    std::error_code error;
    if (!fs::is_regular_file(path, error))
      continue;
    const std::string text = read_file(path, size_t(64) << 20);
    const auto doc = JsonDoc::parse(text, path);
    IterationDescription description = parse_description(doc->root(), series);
    description.origin = path;
    description.origin_sha = sha256_hex(text);
    return description;
  }
  return std::nullopt;
}

std::string description_json(const IterationDescription &d) {
  JsonWriter out;
  out.begin_object();
  out.key("format").string("vp37-runs");
  out.key("formatVersion").integer(kRunsFormatVersion);
  out.key("iteration").string(d.series);
  out.key("notes").begin_array();
  for (const std::string &note : d.notes)
    out.string(note);
  out.end_array();
  out.key("variants").begin_array();
  for (const Variant &variant : d.variants) {
    out.begin_object();
    out.key("id").string(variant.id);
    out.key("ecu_sha256").string(variant.ecu_sha256);
    out.key("adjustometer_sha256").string(variant.adjustometer_sha256);
    out.key("defines").string(variant.defines);
    out.end_object();
  }
  out.end_array();
  out.key("runs").begin_array();
  for (const RunRole &run : d.runs) {
    out.begin_object();
    out.key("id").string(run.id);
    out.key("log").begin_object();
    out.key("path").string(run.log);
    out.key("sha256").string(run.sha256);
    out.end_object();
    out.key("kind");
    if (run.kind.empty())
      out.null();
    else
      out.string(run.kind);
    out.key("repeat");
    if (run.repeat)
      out.integer(*run.repeat);
    else
      out.null();
    out.key("variant");
    if (run.variant.empty())
      out.null();
    else
      out.string(run.variant);
    out.key("metrics").begin_array();
    for (const RunMetricsRef &metrics : run.metrics) {
      out.begin_object();
      out.key("path").string(metrics.path);
      out.key("basis").string(metrics.basis);
      out.end_object();
    }
    out.end_array();
    out.end_object();
  }
  out.end_array();
  out.key("comparisons").begin_array();
  for (const Comparison &comparison : d.comparisons) {
    out.begin_object();
    out.key("id").string(comparison.id);
    out.key("candidates").begin_array();
    for (const std::string &candidate : comparison.candidates)
      out.string(candidate);
    out.end_array();
    out.key("references").begin_array();
    for (const ComparisonReference &reference : comparison.references) {
      out.begin_object();
      out.key("run").string(reference.run);
      out.key("position");
      if (reference.position.empty())
        out.null();
      else
        out.string(reference.position);
      out.end_object();
    }
    out.end_array();
    out.key("basis").string(comparison.basis);
    out.key("result_file");
    if (comparison.result_file.empty())
      out.null();
    else
      out.string(comparison.result_file);
    out.key("described_from").string(comparison.described_from);
    out.end_object();
  }
  out.end_array();
  out.end_object();
  return out.finish() + "\n";
}

std::optional<IterationDescription>
describe_iteration(const Index &index, const std::string &series) {
  IterationDescription d;
  d.series = series;
  std::vector<const Run *> runs;
  std::map<std::string, std::vector<const Run *>> raw_children;
  for (const Run &run : index.runs) {
    if (run.series != series)
      continue;
    if (!run.raw_of.empty()) {
      raw_children[run.raw_of].push_back(&run);
    } else {
      runs.push_back(&run);
    }
  }
  if (runs.empty())
    return std::nullopt;

  std::map<std::string, std::string> kinds; // run id -> kind
  std::set<std::string> sources;
  const auto assign = [&](const Run *run, const std::string &kind) {
    if (!run)
      return;
    const Run *base = run->raw_of.empty() ? run : index.find(run->raw_of);
    if (!base)
      return;
    auto [at, inserted] = kinds.emplace(base->id, kind);
    const std::string note = run_name(*base) + ": rola niejednoznaczna (" +
                             at->second + " i " + kind + "), zostaje " +
                             at->second;
    if (!inserted && at->second != kind &&
        std::find(d.notes.begin(), d.notes.end(), note) == d.notes.end()) {
      d.notes.push_back(note);
    }
  };
  const auto base_of = [&](const Run *run) -> const Run * {
    if (!run)
      return nullptr;
    return run->raw_of.empty() ? run : index.find(run->raw_of);
  };

  // comparison-*.json: gate output with before/current/reference_after.
  std::vector<std::string> comparison_files;
  const std::string series_dir = index.root + "/" + series;
  std::error_code error;
  for (auto it = fs::recursive_directory_iterator(series_dir, error);
       !error && it != fs::recursive_directory_iterator();
       it.increment(error)) {
    const std::string name = it->path().filename().generic_string();
    if (it->is_regular_file(error) && starts_with(name, "comparison") &&
        it->path().extension() == ".json") {
      comparison_files.push_back(
          relative_to(index.root, it->path().generic_string()));
    }
  }
  std::sort(comparison_files.begin(), comparison_files.end());
  for (const std::string &rel : comparison_files) {
    const auto doc = try_load(index.root + "/" + rel);
    if (!doc) {
      d.notes.push_back(inside(series, rel) + ": nieczytelny plik porównania");
      continue;
    }
    const Json sources_node = doc->root()["sources"];
    if (!sources_node.is_object())
      continue;
    bool raw = false;
    const Run *current =
        run_for_source(index, series, rel, sources_node["current"].str(), &raw);
    const Run *before =
        run_for_source(index, series, rel, sources_node["before"].str());
    const Run *after = run_for_source(index, series, rel,
                                      sources_node["reference_after"].str());
    if (!current || !before) {
      d.notes.push_back(inside(series, rel) +
                        ": logi porównania nie istnieją, pominięte");
      continue;
    }
    Comparison comparison;
    comparison.id = inside(series, rel);
    comparison.id = comparison.id.substr(0, comparison.id.size() - 5);
    comparison.candidates.push_back(run_name(*base_of(current)));
    comparison.references.push_back({run_name(*base_of(before)), "before"});
    if (after)
      comparison.references.push_back({run_name(*base_of(after)), "after"});
    comparison.basis = (raw || base_name(rel).find("raw") != std::string::npos)
                           ? "raw"
                           : "filtered";
    comparison.result_file = inside(series, rel);
    comparison.described_from = "comparison-file";
    assign(current, "candidate");
    assign(before, "reference");
    assign(after, "reference");
    d.comparisons.push_back(std::move(comparison));
    sources.insert("comparison-*.json");
  }

  // gate-comparison.json: interleaved candidate and reference runs.
  if (const auto doc = try_load(series_dir + "/gate-comparison.json")) {
    Comparison comparison;
    comparison.id = "gate-comparison";
    comparison.basis = "filtered";
    comparison.result_file = "gate-comparison.json";
    comparison.described_from = "gate-comparison";
    const auto lookup = [&](const std::string &name) -> const Run * {
      return index.find(series + "/" + name + "/" + name);
    };
    for (const Json &name : doc->root()["candidate"].items()) {
      const Run *run = lookup(name.str());
      if (!run) {
        d.notes.push_back("gate-comparison.json: brak logu przebiegu " +
                          name.str());
        continue;
      }
      comparison.candidates.push_back(run_name(*run));
      assign(run, "candidate");
    }
    for (const Json &name : doc->root()["references"].items()) {
      const Run *run = lookup(name.str());
      if (!run) {
        d.notes.push_back("gate-comparison.json: brak logu przebiegu " +
                          name.str());
        continue;
      }
      comparison.references.push_back({run_name(*run), ""});
      assign(run, "reference");
    }
    if (!comparison.candidates.empty() && !comparison.references.empty()) {
      d.comparisons.push_back(std::move(comparison));
      sources.insert("gate-comparison.json");
    }
  }

  // Directory names for runs no comparison file mentions.
  bool from_names = false;
  for (const Run *run : runs) {
    if (kinds.count(run->id))
      continue;
    const std::string kind = kind_from_name(base_name(parent_of(run->log.rel)));
    if (kind.empty())
      continue;
    kinds[run->id] = kind;
    from_names = true;
  }
  if (from_names)
    sources.insert("nazwy katalogów");
  if (kinds.empty())
    return std::nullopt;

  // Comparisons from names only when no file recorded any: each candidate
  // against every reference, ordered by image.json start time.
  if (d.comparisons.empty()) {
    std::vector<const Run *> references, candidates;
    for (const Run *run : runs) {
      const auto kind = kinds.find(run->id);
      if (kind == kinds.end())
        continue;
      if (kind->second == "reference")
        references.push_back(run);
      if (kind->second == "candidate")
        candidates.push_back(run);
    }
    for (const Run *candidate : candidates) {
      if (references.empty())
        break;
      const std::string started = started_of(index, *candidate);
      for (const std::string &basis :
           {std::string("filtered"), std::string("raw")}) {
        const auto has_basis = [&](const Run *run) {
          if (basis == "filtered")
            return true;
          return raw_children.count(run->id) != 0;
        };
        if (!has_basis(candidate))
          continue;
        Comparison comparison;
        comparison.id = run_name(*candidate) + (basis == "raw" ? "-raw" : "");
        comparison.candidates.push_back(run_name(*candidate));
        for (const Run *reference : references) {
          if (!has_basis(reference))
            continue;
          const std::string at = started_of(index, *reference);
          std::string position;
          if (!started.empty() && !at.empty())
            position = at < started ? "before" : "after";
          comparison.references.push_back({run_name(*reference), position});
        }
        comparison.basis = basis;
        comparison.described_from = "names";
        if (!comparison.references.empty())
          d.comparisons.push_back(std::move(comparison));
      }
    }
  }

  // Variants from image.json next to each log.
  std::map<std::string, std::string> variant_ids; // ecu+adj -> id
  for (const Run *run : runs) {
    RunRole role;
    role.id = run_name(*run);
    role.log = inside(series, run->log.rel);
    role.sha256 = run->sha;
    if (const auto kind = kinds.find(run->id); kind != kinds.end())
      role.kind = kind->second;
    role.repeat = trailing_number(role.id);
    if (const auto image = try_load(index.root + "/" + parent_of(run->log.rel) +
                                    "/image.json")) {
      const Json root = image->root();
      const std::string ecu = root["ecu"]["sha256"].str();
      const std::string adj = root["adjustometer"]["sha256"].str();
      if (!ecu.empty()) {
        const std::string key = ecu + "+" + adj;
        auto found = variant_ids.find(key);
        if (found == variant_ids.end()) {
          Variant variant;
          variant.id = "ecu-" + ecu.substr(0, 8) +
                       (adj.empty() ? "" : "+adj-" + adj.substr(0, 8));
          variant.ecu_sha256 = ecu;
          variant.adjustometer_sha256 = adj;
          variant.defines = root["JH_EXTRA_DEFINES"].str();
          found = variant_ids.emplace(key, variant.id).first;
          d.variants.push_back(std::move(variant));
        }
        role.variant = found->second;
      }
    }
    std::vector<const Run *> owners = {run};
    for (const Run *child : raw_children[run->id])
      owners.push_back(child);
    for (const Run *owner : owners) {
      for (size_t index_of : owner->metrics) {
        const MetricsFile &file = index.metrics[index_of];
        role.metrics.push_back({inside(series, file.rel), file.basis});
      }
    }
    d.runs.push_back(std::move(role));
  }
  std::string from = "Opis wygenerowany przez vp37-viewer describe z: ";
  bool first = true;
  for (const std::string &source : sources) {
    from += (first ? "" : ", ") + source;
    first = false;
  }
  d.notes.insert(d.notes.begin(), from + ".");
  return d;
}

std::string report_verdict(const std::string &md, std::string *section) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= md.size()) {
    const size_t end = md.find('\n', start);
    lines.push_back(md.substr(
        start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  const auto paragraph_after = [&](size_t from) {
    std::string text;
    size_t i = from;
    while (i < lines.size() && lines[i].empty())
      ++i;
    for (; i < lines.size() && !lines[i].empty() && !starts_with(lines[i], "#");
         ++i) {
      text += (text.empty() ? "" : "\n") + lines[i];
    }
    return text;
  };
  for (size_t i = 0; i < lines.size(); ++i) {
    if (starts_with(lines[i], "## ") &&
        lines[i].find("erdykt") != std::string::npos) {
      if (section)
        *section = lines[i].substr(3);
      return paragraph_after(i + 1);
    }
  }
  for (size_t i = 0; i < lines.size(); ++i) {
    if (starts_with(lines[i], "# ")) {
      const std::string lead = paragraph_after(i + 1);
      if (!lead.empty() && section)
        *section = "wstęp";
      return lead;
    }
    if (starts_with(lines[i], "## "))
      break;
  }
  return std::string();
}

} // namespace vp37
