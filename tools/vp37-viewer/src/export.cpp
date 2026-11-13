#include "export.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace vp37 {

namespace {

const Series &series_of(const Recording &recording, const char *line) {
  return std::string_view(line) == "C" ? recording.control : recording.pulse;
}

// Fixed decimals keep the files small; -0 prints as 0.
std::string format(double value, int decimals) {
  if (!std::isfinite(value))
    return "null";
  char buffer[64];
  const double scale = std::pow(10.0, decimals);
  double rounded = std::round(value * scale) / scale;
  if (rounded == 0)
    rounded = 0;
  const auto result = std::to_chars(buffer, buffer + sizeof buffer, rounded,
                                    std::chars_format::fixed, decimals);
  std::string text(buffer, result.ptr);
  if (text.find('.') != std::string::npos) {
    while (text.back() == '0')
      text.pop_back();
    if (text.back() == '.')
      text.pop_back();
  }
  return text;
}

void write_column(JsonWriter &out, const std::vector<double> &values,
                  size_t begin, size_t end, int decimals) {
  out.begin_array();
  for (size_t i = begin; i < end; ++i) {
    const std::string text = format(values[i], decimals);
    if (text == "null")
      out.null();
    else
      out.number_token(text);
  }
  out.end_array();
}

double fragment_start(const Recording &recording) {
  return std::floor(recording.first_t);
}

// Rows of the series whose time falls in [from, to).
std::pair<size_t, size_t> rows_between(const Series &series, double from,
                                       double to) {
  const auto begin = std::lower_bound(series.t.begin(), series.t.end(), from);
  const auto end = std::lower_bound(begin, series.t.end(), to);
  return {size_t(begin - series.t.begin()), size_t(end - series.t.begin())};
}

void write_fragment_group(JsonWriter &out, const Recording &recording,
                          const char *line, double from, double to) {
  const Series &series = series_of(recording, line);
  const auto [begin, end] = rows_between(series, from, to);
  out.begin_object();
  out.key("t").begin_array();
  for (size_t i = begin; i < end; ++i)
    out.number_token(format((series.t[i] - from) * 1000.0, 1));
  out.end_array();
  for (const ExportSeries &item : export_series()) {
    if (std::string_view(item.line) != line)
      continue;
    const int column = series.column(item.key);
    if (column < 0)
      continue;
    out.key(item.key);
    write_column(out, series.columns[size_t(column)], begin, end,
                 item.decimals);
  }
  out.end_object();
}

} // namespace

void write_field_metadata(JsonWriter &out, const FieldInfo *info) {
  out.key("unit").string(info ? info->unit : "");
  out.key("description").string(info ? info->description : "");
  out.key("label").string(info ? info->label : "");
  out.key("label_pl").string(info ? info->label_pl : "");
  out.key("description_pl").string(info ? info->description_pl : "");
  if (info && std::string_view(info->kind) == "ADJ" &&
      std::string_view(info->key) == "s") {
    // Bit order is defined by common/adjustometer_protocol.h. Keep the
    // remaining bits numeric; older/newer firmware may assign them meanings.
    out.key("bits").begin_array();
    for (const auto &bit : adj_status_bits())
      out.begin_object()
          .key("label")
          .string(bit.label)
          .key("label_pl")
          .string(bit.label_pl)
          .key("description")
          .string(bit.label)
          .key("description_pl")
          .string(bit.label_pl)
          .end_object();
    out.end_array();
  }
}

const std::vector<ExportSeries> &export_series() {
  static const std::vector<ExportSeries> series = {
      {"C", "tar", 0}, {"C", "des", 0},      {"C", "adj", 0},
      {"C", "err", 0}, {"C", "thr", 1},      {"C", "pwm", 0},
      {"C", "ff", 1},  {"C", "P", 1},        {"C", "I", 1},
      {"C", "D", 1},   {"C", "pdt", 0},      {"C", "age", 0},
      {"C", "hz", 0},  {"C", "hzraw", 0},    {"C", "V", 2},
      {"C", "ft", 0},  {"IPULSE", "Ion", 3}, {"IPULSE", "I95", 3}};
  return series;
}

size_t fragment_count(const Recording &recording) {
  if (recording.control.rows() == 0)
    return 0;
  return size_t(std::floor((recording.last_t - fragment_start(recording)) /
                           kFragmentSeconds)) +
         1;
}

void write_overview(JsonWriter &out, const Recording &recording) {
  const double t0 = recording.first_t;
  const double span = std::max(recording.last_t - t0, 1e-3);
  const double width = span / kOverviewBuckets;
  out.begin_object();
  out.key("t0").number(t0);
  out.key("t1").number(recording.last_t);
  out.key("bucket").number(width);
  out.key("fragment_s").number(kFragmentSeconds);
  out.key("fragment_t0").number(fragment_start(recording));
  out.key("fragments").integer(int64_t(fragment_count(recording)));
  for (const char *line : {"C", "IPULSE"}) {
    const Series &series = series_of(recording, line);
    out.key(line).begin_object();
    for (const ExportSeries &item : export_series()) {
      if (std::string_view(item.line) != line)
        continue;
      const int column = series.column(item.key);
      if (column < 0)
        continue;
      std::vector<double> lo(kOverviewBuckets, NAN), hi(kOverviewBuckets, NAN);
      const auto &values = series.columns[size_t(column)];
      for (size_t i = 0; i < series.rows(); ++i) {
        const double value = values[i];
        if (!std::isfinite(value))
          continue;
        const size_t bucket =
            std::min(size_t(kOverviewBuckets - 1),
                     size_t(std::max(0.0, (series.t[i] - t0) / width)));
        if (std::isnan(lo[bucket]) || value < lo[bucket])
          lo[bucket] = value;
        if (std::isnan(hi[bucket]) || value > hi[bucket])
          hi[bucket] = value;
      }
      out.key(item.key).begin_object();
      out.key("lo");
      write_column(out, lo, 0, lo.size(), item.decimals);
      out.key("hi");
      write_column(out, hi, 0, hi.size(), item.decimals);
      out.end_object();
    }
    out.end_object();
  }
  out.end_object();
}

std::vector<std::string> fragment_parts(const Recording &recording) {
  std::vector<std::string> parts;
  const double start = fragment_start(recording);
  for (size_t k = 0, n = fragment_count(recording); k < n; ++k) {
    const double from = start + double(k) * kFragmentSeconds;
    const double to = from + kFragmentSeconds;
    JsonWriter out;
    out.begin_object();
    out.key("t0").number(from);
    out.key("C");
    write_fragment_group(out, recording, "C", from, to);
    out.key("IPULSE");
    write_fragment_group(out, recording, "IPULSE", from, to);
    out.end_object();
    parts.push_back(out.finish());
  }
  return parts;
}

void write_steps(JsonWriter &out, const Recording &recording) {
  const Series &series = recording.control;
  const int tar = series.column("tar"), des = series.column("des"),
            adj = series.column("adj");
  out.begin_array();
  for (const StepEvent &step : recording.steps) {
    const auto [begin, end] =
        rows_between(series, step.t - kStepBefore, step.t + kStepAfter);
    out.begin_object();
    out.key("test").string(step.test);
    out.key("percent").number(step.percent);
    out.key("index").integer(step.index);
    out.key("count").integer(step.count);
    out.key("t").number(step.t);
    out.key("dt").begin_array();
    for (size_t i = begin; i < end; ++i)
      out.number_token(format((series.t[i] - step.t) * 1000.0, 1));
    out.end_array();
    for (const auto &[name, column] :
         {std::pair{"tar", tar}, {"des", des}, {"adj", adj}}) {
      if (column < 0)
        continue;
      out.key(name);
      write_column(out, series.columns[size_t(column)], begin, end, 0);
    }
    out.end_object();
  }
  out.end_array();
}

Histogram histogram(const Series &series, const char *key, const char *unit,
                    double start, double end, size_t bins) {
  Histogram h;
  h.key = key;
  h.unit = unit;
  const int column = series.column(key);
  if (column < 0)
    return h;
  const auto [begin, stop] =
      rows_between(series, start, std::nextafter(end, INFINITY));
  std::vector<double> values;
  for (size_t i = begin; i < stop; ++i) {
    const double value = series.columns[size_t(column)][i];
    if (std::isfinite(value))
      values.push_back(value);
    else
      ++h.missing;
  }
  h.n = values.size();
  if (values.empty())
    return h;
  h.min = *std::min_element(values.begin(), values.end());
  h.max = *std::max_element(values.begin(), values.end());
  // Axis only: the 99.5th percentile rounded up to a round value.
  std::vector<double> sorted = values;
  const size_t rank = std::min(
      sorted.size() - 1, size_t(std::ceil(0.995 * double(sorted.size()))));
  std::nth_element(sorted.begin(), sorted.begin() + long(rank), sorted.end());
  const double top = std::max(sorted[rank], 1.0);
  const double magnitude = std::pow(10.0, std::floor(std::log10(top)));
  double hi = magnitude;
  for (double step : {1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0}) {
    hi = step * magnitude;
    if (hi >= top * 1.05)
      break;
  }
  h.lo = std::min(0.0, h.min);
  h.hi = hi;
  h.bins.assign(bins, 0);
  for (double value : values) {
    if (value < h.lo) {
      ++h.below;
    } else if (value > h.hi) {
      ++h.above;
    } else {
      const size_t bin = std::min(
          bins - 1, size_t((value - h.lo) / (h.hi - h.lo) * double(bins)));
      ++h.bins[bin];
    }
  }
  return h;
}

void write_histogram(JsonWriter &out, const Histogram &h) {
  out.begin_object();
  out.key("key").string(h.key);
  out.key("unit").string(h.unit);
  out.key("lo").number(h.lo);
  out.key("hi").number(h.hi);
  out.key("bins").begin_array();
  for (size_t count : h.bins)
    out.integer(int64_t(count));
  out.end_array();
  out.key("n").integer(int64_t(h.n));
  out.key("below").integer(int64_t(h.below));
  out.key("above").integer(int64_t(h.above));
  out.key("missing").integer(int64_t(h.missing));
  out.key("min").number(h.n ? h.min : NAN);
  out.key("max").number(h.n ? h.max : NAN);
  out.end_object();
}

std::string js_put(const std::string &slug, const std::string &part,
                   const std::string &json) {
  JsonWriter names;
  names.begin_array().string(slug).string(part).end_array();
  std::string args = names.finish();
  args = args.substr(1, args.size() - 2);
  return "VP37.put(" + args + "," + script_safe(json) + ");\n";
}

std::string script_safe(std::string json) {
  for (size_t at = json.find("</"); at != std::string::npos;
       at = json.find("</", at + 3)) {
    json.replace(at, 2, "<\\/");
  }
  return json;
}

} // namespace vp37
