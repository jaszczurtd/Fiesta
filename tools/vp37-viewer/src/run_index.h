#ifndef VP37V_RUN_INDEX_H
#define VP37V_RUN_INDEX_H

// Runs found under the supplied root and the metrics files that belong to them.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "json_io.h"
#include "telemetry.h"

namespace vp37 {

// The three data areas, relative to the supplied root.
const std::vector<std::string> &data_areas();

struct FileStamp {
  std::string rel; // relative to the data root, '/' separated
  uint64_t size = 0;
  int64_t mtime_ns = 0;
};

// Summary of a parsed log kept in the index (the full recording stays in
// the cache).
struct RunSummary {
  int revision = -1;
  double first_t = 0, last_t = 0;
  size_t control_rows = 0, pulse_rows = 0;
  std::vector<std::string> tests; // distinct test names in order of first start
  size_t steps = 0;
  size_t time_reversals = 0;
  std::map<std::string, size_t> undescribed;
  std::map<std::string, size_t> unknown_kinds;
  // Host markers needed to check analysis windows: label/text -> times.
  std::vector<std::pair<std::string, double>> host_marks;
};

struct Window {
  std::string kind; // gate_markers, whole_log
  double start_s = 0, end_s = 0;
  std::string start_marker, end_marker;
};

enum class BindingStatus {
  bound,           // source, run and window agree
  bound_no_window, // older metrics without analysis_window: run agrees, window
                   // unknown
  no_source,       // file has no `source`
  source_missing,  // `source` does not resolve to an existing file
  not_a_run,       // `source` is a file that is not a VP37 run log
  window_mismatch, // the window markers are not in the log at those times
  sha_mismatch,    // runs.json describes another log content
  unreadable       // invalid JSON
};
const char *binding_status_text(BindingStatus status);
bool is_bound(BindingStatus status);

struct MetricsFile {
  std::string rel;    // path relative to the data root
  std::string sha;    // SHA-256 of the file bytes (page refresh input)
  std::string source; // `source` as written
  std::string resolved_rel;
  std::string run_id; // bound run, empty when unbound
  std::string basis;  // filtered or raw
  BindingStatus status = BindingStatus::unreadable;
  std::string reason;
  std::optional<Window> window;
  std::shared_ptr<JsonDoc> doc; // null when unreadable
};

struct Run {
  std::string id;   // log path relative to the data root, without .log/.log.gz
  std::string area; // one of data_areas()
  std::string series; // area + first directory below it (iteration or series)
  std::string local;  // id relative to the series directory
  FileStamp log;
  std::vector<FileStamp> duplicates; // same content in another compression
  std::string sha;                   // SHA-256 of the log text (decompressed)
  std::string
      raw_of; // for `<x>-raw/<x>-raw` logs: id of the run they derive from
  RunSummary summary;
  std::vector<size_t> metrics; // indices into Index::metrics
};

struct Index {
  std::string root; // absolute recording root
  std::vector<Run> runs;
  std::vector<MetricsFile> metrics;
  std::vector<std::string>
      skipped; // logs that are not VP37 runs or failed to read, with reason
  std::map<std::string, size_t> run_by_id;
  std::map<std::string, size_t>
      run_by_file; // every log/duplicate rel path -> run
  const Run *find(const std::string &id) const;
};

// Candidate files of `source` (absolute, relative to the metrics file or
// one of its parents, or a moved data area). Returned paths are absolute.
std::vector<std::string> source_candidates(const std::string &root,
                                           const std::string &metrics_rel,
                                           const std::string &source);
// Fallback for a run directory moved as a whole (e.g. into series1/): the
// last two parts of `source` (`<run>/<log>`) under the metrics file
// directory and its parents. The window check still has to pass.
std::vector<std::string> moved_run_candidates(const std::string &root,
                                              const std::string &metrics_rel,
                                              const std::string &source);

std::optional<Window> read_window(const Json &metrics);
// Window markers present in the log at the recorded times (1 ms tolerance).
bool window_matches(const Window &window, const RunSummary &summary,
                    std::string &reason);

// Binds every metrics file of the index to its run.
void bind_metrics(Index &index);

std::string relative_to(const std::string &root, const std::string &path);
std::string series_of(const std::string &rel);

} // namespace vp37

#endif
