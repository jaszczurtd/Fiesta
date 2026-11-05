#ifndef VP37V_METRICS_VIEW_H
#define VP37V_METRICS_VIEW_H

// Values from metrics files under the labels of the gate comparisons. The
// viewer only selects values here; it never computes a metric. Labels follow
// compare_gate.collect() and analyze_session.comparison_values() from the
// gate scripts. Means over single steps (ring_sd_hz, settled_*_hz) exist only
// in the comparison files the gate wrote, because they are computed there.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "json_io.h"

namespace vp37 {

using MetricValues = std::vector<std::pair<std::string, double>>;

MetricValues flatten_metrics(const Json &metrics);
std::optional<double> metric_value(const MetricValues &values,
                                   const std::string &label);

// One metric of a comparison, as judged by the gate (threshold 0: any
// increase over any reference is worse).
struct ComparisonRow {
  std::string metric;
  std::vector<std::optional<double>> references;
  std::optional<double> candidate;
  std::string status;                   // worse, no_increase, missing
  std::optional<double> change_percent; // largest (candidate - ref) / |ref|
};

std::string gate_status(const std::optional<double> &candidate,
                        const std::vector<std::optional<double>> &references);
std::optional<double>
change_percent(const std::optional<double> &candidate,
               const std::vector<std::optional<double>> &references);

// Rows of a comparison-*.json written by the gate (status taken from it).
std::vector<ComparisonRow> rows_from_comparison_file(const Json &comparison);
// Rows of gate-comparison.json: candidate_mean against every reference.
std::vector<ComparisonRow>
rows_from_gate_comparison(const Json &comparison,
                          const std::vector<std::string> &references);
// Rows from flattened metrics files of the candidate and its references.
std::vector<ComparisonRow>
rows_from_metrics(const MetricValues &candidate,
                  const std::vector<MetricValues> &references);

} // namespace vp37

#endif
