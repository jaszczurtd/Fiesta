#ifndef VP37V_EXPORT_H
#define VP37V_EXPORT_H

// Recording -> data for the static pages. Series files are JavaScript that
// calls VP37.put(slug, part, value), so pages opened from file:// load them
// with a plain <script src>.

#include <string>
#include <vector>

#include "json_io.h"
#include "telemetry.h"

namespace vp37 {

// Series drawn on the run page and kept in fragments, with display decimals.
struct ExportSeries {
  const char *line; // "C" or "IPULSE"
  const char *key;
  int decimals;
};
const std::vector<ExportSeries> &export_series();

constexpr int kOverviewBuckets = 1500;
constexpr double kFragmentSeconds = 10.0;
constexpr double kStepBefore = 0.3, kStepAfter = 2.5;

// Min and max of every bucket, so spikes survive the reduction.
void write_overview(JsonWriter &out, const Recording &recording);
// Full-rate samples of [t0 + k * kFragmentSeconds, t0 + (k + 1) * ...).
std::vector<std::string> fragment_parts(const Recording &recording);
size_t fragment_count(const Recording &recording);
// tar/des/adj around every step event, time relative to the step.
void write_steps(JsonWriter &out, const Recording &recording);

struct Histogram {
  std::string key, unit;
  double lo = 0, hi = 0; // axis
  std::vector<size_t> bins;
  size_t n = 0, below = 0, above = 0, missing = 0;
  double min = 0, max = 0;
};
// Every finite sample of the column inside [start, end] is counted; the axis
// covers 0 .. a round value above the 99.5th percentile and the samples
// outside it are reported, not dropped.
Histogram histogram(const Series &series, const char *key, const char *unit,
                    double start, double end, size_t bins = 60);
void write_histogram(JsonWriter &out, const Histogram &histogram);

std::string js_put(const std::string &slug, const std::string &part,
                   const std::string &json);
// JSON for a <script> element: "</" cannot close the element.
std::string script_safe(std::string json);

} // namespace vp37

#endif
