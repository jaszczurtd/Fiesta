// vp37-viewer: static pages from VP37 recordings supplied by the caller.
//
//   vp37-viewer build      index, parse (cached), write site/
//   vp37-viewer describe   draft runs.json for iterations without one
//   vp37-viewer parse LOG  print what the parser reads from one log
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "builder.h"
#include "log_source.h"
#include "recording_cache.h"
#include "runs_model.h"

namespace fs = std::filesystem;
using namespace vp37;

namespace {

void usage() {
  std::fputs("usage: vp37-viewer build --root DIR --site DIR --cache DIR "
             "--descriptions DIR "
             "[--jobs N] [--quiet]\n"
             "       vp37-viewer describe --root DIR --cache DIR "
             "--descriptions DIR [--write] "
             "[--overwrite] [ITERATION...]\n"
             "       vp37-viewer parse LOG\n",
             stderr);
}

int parse_command(const std::string &path) {
  const Recording rec = parse_log(read_log(path));
  std::printf("lines %zu timed %zu untimed %zu time_reversals %zu\n", rec.lines,
              rec.timed_lines, rec.untimed_lines, rec.time_reversals);
  std::printf("time %.6f .. %.6f\nrevision %d (CFG lines %zu)\n", rec.first_t,
              rec.last_t, rec.revision, rec.config_lines);
  for (const auto &[name, series] : {std::pair{"C", &rec.control},
                                     {"IPULSE", &rec.pulse},
                                     {"IWAVE", &rec.wave},
                                     {"ADJ", &rec.adj},
                                     {"T", &rec.trace}}) {
    std::printf("%s rows %zu keys", name, series->rows());
    for (const std::string &key : series->keys)
      std::printf(" %s", key.c_str());
    std::printf("\n");
  }
  for (const TestSegment &test : rec.tests) {
    std::printf("test %s %.3f .. %.3f%s %s\n", test.test.c_str(), test.start,
                test.end, test.closed ? "" : " (open)", test.result.c_str());
  }
  std::printf("steps %zu phases %zu host lines %zu\n", rec.steps.size(),
              rec.phases.size(), rec.hosts.size());
  for (const auto &[key, count] : rec.undescribed)
    std::printf("undescribed %s %zu\n", key.c_str(), count);
  for (const auto &[kind, count] : rec.unknown_kinds)
    std::printf("unknown kind %s %zu\n", kind.c_str(), count);
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string command = argv[1];
  BuildOptions options;
  options.web = VP37V_WEB_DIR;
  bool write = false, overwrite = false;
  std::vector<std::string> positional;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--root")
      options.root = fs::absolute(value()).lexically_normal().generic_string();
    else if (arg == "--site")
      options.site = fs::absolute(value()).generic_string();
    else if (arg == "--cache")
      options.cache = fs::absolute(value()).generic_string();
    else if (arg == "--descriptions")
      options.descriptions = fs::absolute(value()).generic_string();
    else if (arg == "--jobs")
      options.jobs = unsigned(std::stoul(value()));
    else if (arg == "--quiet")
      options.quiet = true;
    else if (arg == "--write")
      write = true;
    else if (arg == "--overwrite")
      overwrite = true;
    else if (!arg.empty() && arg[0] == '-') {
      usage();
      return 2;
    } else {
      positional.push_back(arg);
    }
  }
  if (!options.root.empty() && options.root.back() == '/')
    options.root.pop_back();
  try {
    if (command == "parse" && positional.size() == 1)
      return parse_command(positional[0]);
    if (options.root.empty() || options.cache.empty() ||
        options.descriptions.empty() ||
        (command == "build" && options.site.empty())) {
      std::fputs("vp37-viewer: pass the input, output and metadata directories "
                 "explicitly\n",
                 stderr);
      usage();
      return 2;
    }
    if (command == "build" && positional.empty()) {
      const BuildStats s = build_site(options);
      std::printf(
          "logs %zu (read %zu, %.1f MB; parsed %zu), runs %zu, not runs %zu\n"
          "metrics files %zu, bound %zu\nrun pages written %zu, unchanged %zu\n"
          "index %.1f s, pages %.1f s, total %.1f s\nsite: %s\n",
          s.logs, s.logs_read, double(s.bytes_read) / 1e6, s.logs_parsed,
          s.runs, s.skipped, s.metrics_files, s.metrics_bound,
          s.run_pages_written, s.run_pages_kept, s.index_s, s.pages_s,
          s.total_s, (options.site + "/index.html").c_str());
      return 0;
    }
    if (command == "describe") {
      Model model;
      index_runs(model, options);
      size_t written = 0, same = 0, differing = 0, none = 0;
      for (const IterationView &view : model.iterations) {
        if (!positional.empty() &&
            std::find(positional.begin(), positional.end(), view.series) ==
                positional.end()) {
          continue;
        }
        // A runner-written runs.json is the source; drafts never replace it.
        std::error_code error;
        if (fs::is_regular_file(options.root + "/" + view.series + "/runs.json",
                                error))
          continue;
        const auto draft = describe_iteration(model.index, view.series);
        if (!draft) {
          ++none;
          continue;
        }
        const std::string text = description_json(*draft);
        const std::string path =
            options.descriptions + "/" + description_file_name(view.series);
        if (!write) {
          std::printf("== %s\n%s", path.c_str(), text.c_str());
          continue;
        }
        bool changed = false;
        if (fs::is_regular_file(path, error) && !overwrite) {
          if (read_file(path, size_t(64) << 20) == text)
            ++same;
          else {
            ++differing;
            std::printf("differs (kept, use --overwrite): %s\n", path.c_str());
          }
          continue;
        }
        changed = write_file(path, text);
        written += changed;
        same += !changed;
      }
      std::printf("descriptions written %zu, unchanged %zu, differing %zu, "
                  "iterations without roles %zu\n",
                  written, same, differing, none);
      return 0;
    }
  } catch (const std::exception &error) {
    std::fprintf(stderr, "vp37-viewer: %s\n", error.what());
    return 1;
  }
  usage();
  return 2;
}
