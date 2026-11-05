#ifndef VP37V_TELEMETRY_H
#define VP37V_TELEMETRY_H

// The one reader of collector logs: `<t> [ECU:] VP37 <kind> key:value ...`,
// `TEST: ...` and `HOST ...` lines. It keeps every key, described or not, so
// a log from newer firmware is still readable before the catalog catches up.
// Any change of what parse_log() produces must raise kParserVersion: cached
// recordings are keyed by it.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "telemetry_stream.h"

namespace vp37 {

constexpr int kParserVersion = 6;

// Description of one telemetry field. Fields that firmware stopped printing
// stay here, so older logs keep their meaning.
struct FieldInfo {
  const char *kind; // line kind: "C", "IPULSE", "IWAVE", "CFG", "ADJ"
  const char *key;
  const char *unit; // empty for counts, flags and codes
  const char *description;
};

// nullptr when the key is not described for this line kind ("nieopisane").
const FieldInfo *find_field(std::string_view kind, std::string_view key);
const std::vector<FieldInfo> &field_catalog();

// One line kind as columns. Every row has a value in every column; a key
// missing on a row (field added or removed mid-log) reads as NaN.
struct Series {
  std::vector<double> t;
  std::vector<std::string> keys;
  std::vector<std::vector<double>> columns;

  int column(std::string_view key) const;
  size_t rows() const { return t.size(); }
};

struct TestSegment {
  std::string test;
  double start = 0, end = 0;
  std::string result;  // text after "result:", empty when none was printed
  bool closed = false; // false: the log ended while the test was running
};

// `TEST: topzero 95.0 % (series 1/5)` and similar: the moment a step starts.
struct StepEvent {
  double t = 0;
  std::string test;
  double percent = 0;
  int index = 0, count = 0;
};

// Other `TEST:` lines that are not start/stop/result/step, e.g.
// `TEST: pot rise 350 %/s (pass 1/3)`.
struct PhaseEvent {
  double t = 0;
  std::string test, text;
};

// `HOST TX <label> <command>`; text keeps the whole line after the time.
struct HostEvent {
  double t = 0;
  std::string label, command, text;
};

struct ConfigSnapshot {
  double t = 0;
  int revision = -1;
  std::vector<std::pair<std::string, std::string>> fields;
};

struct Recording {
  Series control; // VP37 C
  Series pulse;   // VP37 IPULSE
  Series wave;    // VP37 IWAVE
  Series adj;     // VP37 ADJ
  Series trace;   // VP37 T (RAM trace dump; collector time is the dump time)
  Series config;  // numeric fields from every VP37 CFG, including timing data
  std::vector<TestSegment> tests;
  std::vector<StepEvent> steps;
  std::vector<PhaseEvent> phases;
  std::vector<HostEvent> hosts;
  // First CFG and every CFG whose revision differs from the previous one.
  std::vector<ConfigSnapshot> configs;
  size_t config_lines = 0;
  int revision = -1; // first CFG revision, -1 when the log has no CFG line
  double first_t = 0, last_t = 0;
  size_t lines = 0, timed_lines = 0, untimed_lines = 0;
  // Times that went back (e.g. two collector sessions in one file). Later
  // lines are shifted so time keeps rising; the page warns about it.
  size_t time_reversals = 0;
  // "<kind> <key>" -> rows: keys missing from the catalog.
  std::map<std::string, size_t> undescribed;
  // VP37 line kinds this parser does not know -> lines.
  std::map<std::string, size_t> unknown_kinds;
};

Recording parse_log(std::string_view text);

/**
 * Incremental reader using the same line parser as parse_log(). The output
 * must be empty and outlive the reader. Do not modify it while feeding.
 * MCU mode maps telemetry us to seconds and unwraps the counter; ordered C
 * records anchor delayed T/IPULSE/IWAVE records. Lines without us keep host
 * time. Collector mode preserves historical metric-window coordinates.
 * A line may contain up to 64 KiB excluding LF. Storage is allocated once;
 * construction may throw std::bad_alloc, feed/finish return status codes.
 */
class TelemetryParser {
public:
  explicit TelemetryParser(Recording &output, bool mcu_time = false);
  ~TelemetryParser();
  TelemetryParser(const TelemetryParser &) = delete;
  TelemetryParser &operator=(const TelemetryParser &) = delete;
  /** Append a chunk. On error, discard the reader and its partial output. */
  hal_status_t feed(std::string_view bytes);
  /** End the input and deliver its last line; calling twice is harmless. */
  hal_status_t finish();

private:
  struct State;
  State *state_;
};

// Value of `<number>[suffix]` or `0x<hex>`; false for text values.
bool parse_value(std::string_view text, double &out);

} // namespace vp37

#endif
