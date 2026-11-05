#include "recording_cache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace vp37 {

namespace {

constexpr char kMagic[8] = {'V', 'P', '3', '7', 'R', 'E', 'C', '2'};

class Out {
public:
  template <typename T> void pod(T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    bytes.append(reinterpret_cast<const char *>(&value), sizeof value);
  }
  void str(const std::string &text) {
    pod<uint64_t>(text.size());
    bytes.append(text);
  }
  template <typename T> void array(const std::vector<T> &values) {
    pod<uint64_t>(values.size());
    if (!values.empty()) {
      bytes.append(reinterpret_cast<const char *>(values.data()),
                   values.size() * sizeof(T));
    }
  }
  std::string bytes;
};

class In {
public:
  explicit In(const std::string &data) : data_(data) {}
  template <typename T> T pod() {
    T value{};
    take(&value, sizeof value);
    return value;
  }
  size_t count() {
    const uint64_t value = pod<uint64_t>();
    if (value > std::numeric_limits<size_t>::max())
      throw Truncated();
    return static_cast<size_t>(value);
  }
  std::string str() {
    const size_t size = count();
    if (size > data_.size() - pos_)
      throw Truncated();
    std::string out = data_.substr(pos_, size);
    pos_ += size;
    return out;
  }
  template <typename T> std::vector<T> array() {
    const size_t size = count();
    if (size > (data_.size() - pos_) / sizeof(T))
      throw Truncated();
    std::vector<T> out(size);
    take(out.data(), size * sizeof(T));
    return out;
  }
  bool done() const { return pos_ == data_.size(); }
  struct Truncated {};

private:
  void take(void *out, size_t size) {
    if (size > data_.size() - pos_)
      throw Truncated();
    if (size == 0)
      return; // an empty vector may have no storage
    std::memcpy(out, data_.data() + pos_, size);
    pos_ += size;
  }
  const std::string &data_;
  size_t pos_ = 0;
};

// Column encodings. Most columns are flags, small integers or constant over
// a run; single precision (about seven digits) is enough for plotting every
// other field except large counters and MCU timestamps, which stay double.
enum Encoding : uint8_t { kFloat, kDouble, kConstant, kInt8, kInt16, kInt32 };

template <typename T> constexpr T missing_code() {
  return std::numeric_limits<T>::min();
}

Encoding choose_encoding(const std::vector<double> &column) {
  bool constant = true, integral = true, small_float = true;
  double lo = 0, hi = 0;
  bool any = false;
  for (const double value : column) {
    if (std::memcmp(&value, &column.front(), sizeof value) != 0)
      constant = false;
    if (!std::isfinite(value)) {
      if (!std::isnan(value))
        integral = small_float = false;
      continue;
    }
    if (std::fabs(value) > 1.0e6)
      small_float = false;
    if (value != std::floor(value))
      integral = false;
    lo = any ? std::min(lo, value) : value;
    hi = any ? std::max(hi, value) : value;
    any = true;
  }
  if (column.empty() || constant)
    return kConstant;
  if (integral && lo > -128 && hi <= 127)
    return kInt8;
  if (integral && lo > -32768 && hi <= 32767)
    return kInt16;
  if (integral && lo > -2147483648.0 && hi <= 2147483647.0)
    return kInt32;
  return small_float ? kFloat : kDouble;
}

template <typename T>
void write_as(Out &out, const std::vector<double> &column) {
  std::vector<T> values(column.size());
  for (size_t i = 0; i < column.size(); ++i) {
    values[i] = std::numeric_limits<T>::is_integer && std::isnan(column[i])
                    ? missing_code<T>()
                    : static_cast<T>(column[i]);
  }
  out.array(values);
}

template <typename T> std::vector<double> read_as(In &in) {
  const auto values = in.array<T>();
  std::vector<double> column(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    column[i] = values[i] == missing_code<T>()
                    ? std::numeric_limits<double>::quiet_NaN()
                    : static_cast<double>(values[i]);
  }
  return column;
}

void write_series(Out &out, const Series &series) {
  out.array(series.t);
  out.pod<uint32_t>(static_cast<uint32_t>(series.keys.size()));
  for (size_t i = 0; i < series.keys.size(); ++i) {
    out.str(series.keys[i]);
    const auto &column = series.columns[i];
    const Encoding encoding = choose_encoding(column);
    out.pod<uint8_t>(encoding);
    switch (encoding) {
    case kConstant:
      out.pod<double>(column.empty() ? 0.0 : column.front());
      break;
    case kInt8:
      write_as<int8_t>(out, column);
      break;
    case kInt16:
      write_as<int16_t>(out, column);
      break;
    case kInt32:
      write_as<int32_t>(out, column);
      break;
    case kFloat:
      write_as<float>(out, column);
      break;
    case kDouble:
      out.array(column);
      break;
    }
  }
}

Series read_series(In &in) {
  Series series;
  series.t = in.array<double>();
  const uint32_t columns = in.pod<uint32_t>();
  for (uint32_t i = 0; i < columns; ++i) {
    series.keys.push_back(in.str());
    switch (in.pod<uint8_t>()) {
    case kConstant:
      series.columns.emplace_back(series.t.size(), in.pod<double>());
      break;
    case kInt8:
      series.columns.push_back(read_as<int8_t>(in));
      break;
    case kInt16:
      series.columns.push_back(read_as<int16_t>(in));
      break;
    case kInt32:
      series.columns.push_back(read_as<int32_t>(in));
      break;
    case kFloat: {
      const auto values = in.array<float>();
      series.columns.emplace_back(values.begin(), values.end());
      break;
    }
    case kDouble:
      series.columns.push_back(in.array<double>());
      break;
    default:
      throw In::Truncated();
    }
    if (series.columns.back().size() != series.t.size())
      throw In::Truncated();
  }
  return series;
}

void write_counts(Out &out, const std::map<std::string, size_t> &counts) {
  out.pod<uint64_t>(counts.size());
  for (const auto &[key, count] : counts) {
    out.str(key);
    out.pod<uint64_t>(count);
  }
}

std::map<std::string, size_t> read_counts(In &in) {
  std::map<std::string, size_t> counts;
  const size_t size = in.count();
  for (size_t i = 0; i < size; ++i) {
    std::string key = in.str();
    counts[key] = in.count();
  }
  return counts;
}

} // namespace

std::string recording_cache_path(const std::string &cache_dir,
                                 const std::string &log_sha) {
  return cache_dir + "/rec/" + log_sha.substr(0, 2) + "/" + log_sha + "-p" +
         std::to_string(kParserVersion) + ".bin";
}

std::string serialize_recording(const Recording &rec) {
  Out out;
  out.bytes.append(kMagic, sizeof kMagic);
  out.pod<int32_t>(kParserVersion);
  for (const Series *series : {&rec.control, &rec.pulse, &rec.wave, &rec.adj,
                               &rec.trace, &rec.config}) {
    write_series(out, *series);
  }
  out.pod<uint64_t>(rec.tests.size());
  for (const TestSegment &test : rec.tests) {
    out.str(test.test);
    out.pod(test.start);
    out.pod(test.end);
    out.str(test.result);
    out.pod<uint8_t>(test.closed ? 1 : 0);
  }
  out.pod<uint64_t>(rec.steps.size());
  for (const StepEvent &step : rec.steps) {
    out.pod(step.t);
    out.str(step.test);
    out.pod(step.percent);
    out.pod<int32_t>(step.index);
    out.pod<int32_t>(step.count);
  }
  out.pod<uint64_t>(rec.phases.size());
  for (const PhaseEvent &phase : rec.phases) {
    out.pod(phase.t);
    out.str(phase.test);
    out.str(phase.text);
  }
  out.pod<uint64_t>(rec.hosts.size());
  for (const HostEvent &host : rec.hosts) {
    out.pod(host.t);
    out.str(host.label);
    out.str(host.command);
    out.str(host.text);
  }
  out.pod<uint64_t>(rec.configs.size());
  for (const ConfigSnapshot &config : rec.configs) {
    out.pod(config.t);
    out.pod<int32_t>(config.revision);
    out.pod<uint64_t>(config.fields.size());
    for (const auto &[key, value] : config.fields) {
      out.str(key);
      out.str(value);
    }
  }
  out.pod<uint64_t>(rec.config_lines);
  out.pod<int32_t>(rec.revision);
  out.pod(rec.first_t);
  out.pod(rec.last_t);
  out.pod<uint64_t>(rec.lines);
  out.pod<uint64_t>(rec.timed_lines);
  out.pod<uint64_t>(rec.untimed_lines);
  out.pod<uint64_t>(rec.time_reversals);
  write_counts(out, rec.undescribed);
  write_counts(out, rec.unknown_kinds);
  return std::move(out.bytes);
}

std::optional<Recording> deserialize_recording(const std::string &bytes) {
  if (bytes.size() < sizeof kMagic ||
      std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
    return std::nullopt;
  }
  try {
    In in(bytes);
    for (size_t i = 0; i < sizeof kMagic; ++i)
      (void)in.pod<char>();
    if (in.pod<int32_t>() != kParserVersion)
      return std::nullopt;
    Recording rec;
    for (Series *series : {&rec.control, &rec.pulse, &rec.wave, &rec.adj,
                           &rec.trace, &rec.config}) {
      *series = read_series(in);
    }
    rec.tests.resize(in.count());
    for (TestSegment &test : rec.tests) {
      test.test = in.str();
      test.start = in.pod<double>();
      test.end = in.pod<double>();
      test.result = in.str();
      test.closed = in.pod<uint8_t>() != 0;
    }
    rec.steps.resize(in.count());
    for (StepEvent &step : rec.steps) {
      step.t = in.pod<double>();
      step.test = in.str();
      step.percent = in.pod<double>();
      step.index = in.pod<int32_t>();
      step.count = in.pod<int32_t>();
    }
    rec.phases.resize(in.count());
    for (PhaseEvent &phase : rec.phases) {
      phase.t = in.pod<double>();
      phase.test = in.str();
      phase.text = in.str();
    }
    rec.hosts.resize(in.count());
    for (HostEvent &host : rec.hosts) {
      host.t = in.pod<double>();
      host.label = in.str();
      host.command = in.str();
      host.text = in.str();
    }
    rec.configs.resize(in.count());
    for (ConfigSnapshot &config : rec.configs) {
      config.t = in.pod<double>();
      config.revision = in.pod<int32_t>();
      config.fields.resize(in.count());
      for (auto &[key, value] : config.fields) {
        key = in.str();
        value = in.str();
      }
    }
    rec.config_lines = in.count();
    rec.revision = in.pod<int32_t>();
    rec.first_t = in.pod<double>();
    rec.last_t = in.pod<double>();
    rec.lines = in.count();
    rec.timed_lines = in.count();
    rec.untimed_lines = in.count();
    rec.time_reversals = in.count();
    rec.undescribed = read_counts(in);
    rec.unknown_kinds = read_counts(in);
    if (!in.done())
      return std::nullopt;
    return rec;
  } catch (const In::Truncated &) {
    return std::nullopt;
  } catch (const std::bad_alloc &) {
    return std::nullopt;
  }
}

} // namespace vp37
