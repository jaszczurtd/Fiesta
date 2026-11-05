#ifndef VP37V_RUNS_MODEL_H
#define VP37V_RUNS_MODEL_H

// runs.json of one iteration: the runs with their roles and the comparisons
// between them. The file is read from `<iteration>/runs.json` (written by a
// runner) or from the viewer's `descriptions/` directory (existing iterations
// described by `vp37-viewer describe`). Nothing is guessed at build time: an
// iteration without a description is shown as undescribed.

#include <optional>
#include <string>
#include <vector>

#include "run_index.h"

namespace vp37 {

constexpr int kRunsFormatVersion = 1;

struct RunMetricsRef {
  std::string path;  // relative to the iteration directory
  std::string basis; // filtered or raw
};

struct RunRole {
  std::string id;  // run name inside the iteration (see run_name())
  std::string log; // log path relative to the iteration directory
  std::string sha256;
  std::string kind; // reference, candidate, warmup or empty (not described)
  std::optional<int> repeat;
  std::string variant; // key into IterationDescription::variants, may be empty
  std::vector<RunMetricsRef> metrics;
};

struct Variant {
  std::string id;
  std::string ecu_sha256, adjustometer_sha256, defines;
};

struct ComparisonReference {
  std::string run;
  std::string
      position; // before, after, or empty when the order is not recorded
};

struct Comparison {
  std::string id;
  std::vector<std::string> candidates; // repeats of one variant
  std::vector<ComparisonReference> references;
  std::string basis;       // filtered or raw
  std::string result_file; // gate output with per-metric verdicts, may be empty
  std::string described_from; // comparison-file, gate-comparison, names
};

struct IterationDescription {
  std::string series;     // iteration path relative to the data root
  std::string origin;     // file the description was read from (absolute)
  std::string origin_sha; // page refresh input
  std::vector<std::string> notes; // what the description is based on, gaps
  std::vector<Variant> variants;
  std::vector<RunRole> runs;
  std::vector<Comparison> comparisons;
};

// Short run name inside its iteration: "candidate-x1" for
// "candidate-x1/candidate-x1", otherwise the path without the extension.
std::string run_name(const Run &run);

std::optional<IterationDescription>
load_description(const std::string &root, const std::string &series,
                 const std::string &descriptions_dir);
IterationDescription parse_description(const Json &root,
                                       const std::string &series);
std::string description_json(const IterationDescription &description);
// Draft from gate-comparison.json, comparison-*.json, run directory names
// and image.json. Returns nullopt when nothing in the iteration names roles.
std::optional<IterationDescription>
describe_iteration(const Index &index, const std::string &series);

// Description file name inside descriptions/ for an iteration.
std::string description_file_name(const std::string &series);

// Verdict paragraph of report.md: the section whose heading names the
// verdict ("Werdykt"), else the lead text under the title. Empty if neither.
std::string report_verdict(const std::string &report_md,
                           std::string *section = nullptr);

} // namespace vp37

#endif
