#ifndef VP37V_BUILDER_H
#define VP37V_BUILDER_H

#include <cstddef>
#include <string>
#include <vector>

#include "site.h"

namespace vp37 {

struct BuildOptions {
  std::string root;         // caller-supplied recording tree
  std::string site;         // output pages
  std::string cache;        // parsed recordings, summaries, stamps
  std::string descriptions; // runs.json of existing iterations
  std::string web;          // viewer.js, viewer.css
  unsigned jobs = 0;        // 0: hardware threads
  bool quiet = false;
};

struct BuildStats {
  size_t logs = 0, runs = 0, skipped = 0;
  size_t logs_read = 0, logs_parsed = 0;
  size_t bytes_read = 0;
  size_t run_pages_written = 0, run_pages_kept = 0;
  size_t metrics_files = 0, metrics_bound = 0;
  double index_s = 0, pages_s = 0, total_s = 0;
};

// A log is a VP37 run when it has at least this many timed `VP37 C` lines.
constexpr size_t kMinControlRows = 10;

// Index and model only (no pages): used by describe and by the tests.
void index_runs(Model &model, const BuildOptions &options,
                BuildStats *stats = nullptr);
BuildStats build_site(const BuildOptions &options);

} // namespace vp37

#endif
