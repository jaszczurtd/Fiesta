#include "telemetry.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace vp37 {

namespace {

constexpr double kMissing = std::numeric_limits<double>::quiet_NaN();

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view trim(std::string_view text) {
  while (!text.empty() && is_space(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && is_space(text.back()))
    text.remove_suffix(1);
  return text;
}

bool consume(std::string_view &text, std::string_view prefix) {
  if (text.substr(0, prefix.size()) != prefix)
    return false;
  text.remove_prefix(prefix.size());
  return true;
}

std::string_view next_token(std::string_view &text) {
  text = trim(text);
  size_t end = 0;
  while (end < text.size() && !is_space(text[end]))
    ++end;
  std::string_view token = text.substr(0, end);
  text.remove_prefix(end);
  text = trim(text);
  return token;
}

// Serial consoles colour some lines; the escapes never carry data.
std::string strip_ansi(std::string_view line) {
  std::string out;
  out.reserve(line.size());
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[') {
      i += 2;
      while (i < line.size() && !(line[i] >= '@' && line[i] <= '~'))
        ++i;
      continue;
    }
    out.push_back(line[i]);
  }
  return out;
}

// Appends rows to one series. Keys usually repeat in the same order, so the
// lookup first tries the column that followed the previous key.
class SeriesBuilder {
public:
  SeriesBuilder(Series &series, std::string_view kind)
      : series_(series), kind_(kind) {}

  void begin_row(double t) {
    series_.t.push_back(t);
    for (auto &column : series_.columns)
      column.push_back(kMissing);
    expected_ = 0;
  }

  void set(std::string_view key, double value) {
    size_t index;
    if (expected_ < series_.keys.size() && series_.keys[expected_] == key) {
      index = expected_;
    } else {
      auto found = index_.find(std::string(key));
      if (found == index_.end()) {
        index = series_.keys.size();
        series_.keys.emplace_back(key);
        series_.columns.emplace_back(series_.t.size(), kMissing);
        index_.emplace(std::string(key), index);
        described_.push_back(find_field(kind_, key) != nullptr);
        seen_.push_back(0);
      } else {
        index = found->second;
      }
    }
    series_.columns[index].back() = value;
    ++seen_[index];
    expected_ = index + 1;
  }

  // Rows that carried each key missing from the catalog.
  void count_undescribed(std::map<std::string, size_t> &out) const {
    for (size_t i = 0; i < described_.size(); ++i) {
      if (!described_[i])
        out[std::string(kind_) + " " + series_.keys[i]] += seen_[i];
    }
  }

private:
  Series &series_;
  std::string_view kind_;
  std::unordered_map<std::string, size_t> index_;
  std::vector<bool> described_;
  std::vector<size_t> seen_;
  size_t expected_ = 0;
};

struct Parser {
  Recording &rec;
  SeriesBuilder control{rec.control, "C"}, pulse{rec.pulse, "IPULSE"},
      wave{rec.wave, "IWAVE"}, adj{rec.adj, "ADJ"}, trace{rec.trace, "T"},
      config_values{rec.config, "CFG"};
  std::unordered_map<std::string, bool> config_described;
  std::map<std::string, size_t> open_tests; // test -> index in rec.tests
  double offset = 0;
  bool mcu_time;
  vp37_sample_clock_t clock{};

  explicit Parser(Recording &recording, bool use_mcu_time = false)
      : rec(recording), mcu_time(use_mcu_time) {}

  double sample_time(std::string_view body, double host, bool advance) {
    if (!mcu_time)
      return host;
    while (!body.empty()) {
      const std::string_view token = next_token(body);
      if (token.substr(0, 3) != "us:")
        continue;
      uint32_t us = 0;
      const auto value = token.substr(3);
      const auto parsed =
          std::from_chars(value.data(), value.data() + value.size(), us);
      if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size())
        return host;
      double mapped = host;
      (void)vp37_sample_clock_time(&clock, us, host, advance, &mapped);
      return mapped;
    }
    return host;
  }

  void key_values(std::string_view body, SeriesBuilder &out, double t) {
    out.begin_row(t);
    while (!body.empty()) {
      std::string_view token = next_token(body);
      const size_t colon = token.find(':');
      if (colon == std::string_view::npos || colon == 0)
        continue;
      const std::string_view key = token.substr(0, colon);
      double value;
      if (!parse_value(token.substr(colon + 1), value))
        value = kMissing;
      out.set(key, value);
    }
  }

  void config(std::string_view body, double t) {
    ConfigSnapshot snapshot;
    snapshot.t = t;
    while (!body.empty()) {
      std::string_view token = next_token(body);
      const size_t colon = token.find(':');
      if (colon == std::string_view::npos || colon == 0)
        continue;
      const std::string_view key = token.substr(0, colon);
      const std::string_view value = token.substr(colon + 1);
      if (key == "rev") {
        double number;
        if (parse_value(value, number))
          snapshot.revision = static_cast<int>(number);
      }
      auto described = config_described.find(std::string(key));
      if (described == config_described.end()) {
        described =
            config_described
                .emplace(std::string(key), find_field("CFG", key) != nullptr)
                .first;
      }
      if (!described->second)
        ++rec.undescribed["CFG " + std::string(key)];
      snapshot.fields.emplace_back(key, value);
    }
    ++rec.config_lines;
    if (rec.revision < 0)
      rec.revision = snapshot.revision;
    if (rec.configs.empty() ||
        rec.configs.back().revision != snapshot.revision) {
      rec.configs.push_back(std::move(snapshot));
    }
  }

  void close_test(const std::string &test, double t) {
    auto open = open_tests.find(test);
    if (open == open_tests.end())
      return;
    rec.tests[open->second].end = t;
    rec.tests[open->second].closed = true;
    open_tests.erase(open);
  }

  // `TEST: <name> started|stopped|result: <x>`, step lines and phase lines.
  void test_line(std::string_view body, double t) {
    std::string_view rest = body;
    const std::string name(next_token(rest));
    if (name.empty())
      return;
    if (rest == "started") {
      // A test restarted without a stop (manual S75 -> S0) closes the
      // previous segment at the new start.
      close_test(name, t);
      open_tests[name] = rec.tests.size();
      TestSegment segment;
      segment.test = name;
      segment.start = segment.end = t;
      rec.tests.push_back(std::move(segment));
      return;
    }
    if (rest == "stopped") {
      close_test(name, t);
      return;
    }
    if (consume(rest, "result:")) {
      for (size_t i = rec.tests.size(); i-- > 0;) {
        if (rec.tests[i].test == name) {
          rec.tests[i].result = std::string(trim(rest));
          break;
        }
      }
      return;
    }
    // `<percent> % (series <i>/<n>)`
    std::string_view step = rest;
    double percent;
    const std::string_view number = next_token(step);
    if (parse_value(number, percent) && consume(step, "% (series ")) {
      StepEvent event;
      event.t = t;
      event.test = name;
      event.percent = percent;
      const size_t slash = step.find('/');
      if (slash != std::string_view::npos) {
        std::from_chars(step.data(), step.data() + slash, event.index);
        std::from_chars(step.data() + slash + 1, step.data() + step.size(),
                        event.count);
      }
      rec.steps.push_back(std::move(event));
      return;
    }
    rec.phases.push_back({t, name, std::string(rest)});
  }

  void line(std::string_view raw) {
    std::string cleaned;
    if (raw.find('\x1b') != std::string_view::npos) {
      cleaned = strip_ansi(raw);
      raw = cleaned;
    }
    std::string_view text = trim(raw);
    if (text.empty())
      return;
    ++rec.lines;
    double t;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), t);
    if (parsed.ec != std::errc() || parsed.ptr == text.data() + text.size() ||
        !is_space(*parsed.ptr) || !std::isfinite(t)) {
      ++rec.untimed_lines;
      return;
    }
    text.remove_prefix(static_cast<size_t>(parsed.ptr - text.data()));
    text = trim(text);
    t += offset;
    if (rec.timed_lines != 0 && t < rec.last_t) {
      ++rec.time_reversals;
      offset += rec.last_t - t;
      t = rec.last_t;
    }
    if (rec.timed_lines == 0)
      rec.first_t = t;
    ++rec.timed_lines;
    rec.last_t = t;

    if (consume(text, "HOST ")) {
      HostEvent event;
      event.t = t;
      event.text = "HOST " + std::string(text);
      if (consume(text, "TX ")) {
        event.label = std::string(next_token(text));
        event.command = std::string(text);
      }
      rec.hosts.push_back(std::move(event));
      return;
    }
    if (consume(text, "ECU:"))
      text = trim(text);
    if (consume(text, "TEST: ")) {
      test_line(text, t);
      return;
    }
    if (!consume(text, "VP37 "))
      return;
    const std::string_view kind = next_token(text);
    if (kind == "C") {
      key_values(text, control, sample_time(text, t, true));
    } else if (kind == "IPULSE") {
      key_values(text, pulse, sample_time(text, t, false));
    } else if (kind == "IWAVE") {
      key_values(text, wave, sample_time(text, t, false));
    } else if (kind == "ADJ") {
      key_values(text, adj, t);
    } else if (kind == "T") {
      key_values(text, trace, sample_time(text, t, false));
    } else if (kind == "CFG") {
      key_values(text, config_values, t);
      config(text, t);
    } else if (!kind.empty() && kind.back() != ':') {
      // `VP37 demand: ...` is a plain message; an unknown kind with
      // key:value pairs is a new line type.
      if (text.find(':') != std::string_view::npos)
        ++rec.unknown_kinds[std::string(kind)];
    }
  }

  void finish() {
    for (auto &[test, index] : open_tests)
      rec.tests[index].end = rec.last_t;
    open_tests.clear();
    for (const SeriesBuilder *series :
         {&control, &pulse, &wave, &adj, &trace}) {
      series->count_undescribed(rec.undescribed);
    }
  }
};

} // namespace

struct TelemetryParser::State {
  Parser parser;
  char line[64 * 1024];
  vp37_line_stream_t stream{};
  bool finalized = false;

  State(Recording &output, bool mcu_time) : parser(output, mcu_time) {
    (void)vp37_line_stream_init(&stream, line, sizeof line, deliver, this);
  }

  static hal_status_t deliver(void *context, const char *text, size_t length) {
    auto &self = *static_cast<State *>(context);
    try {
      self.parser.line(std::string_view(text, length));
      return HAL_OK;
    } catch (const std::bad_alloc &) {
      return HAL_ENOMEM;
    } catch (...) {
      return HAL_EINTERNAL;
    }
  }
};

TelemetryParser::TelemetryParser(Recording &output, bool mcu_time)
    : state_(new State(output, mcu_time)) {}

TelemetryParser::~TelemetryParser() { delete state_; }

hal_status_t TelemetryParser::feed(std::string_view bytes) {
  return vp37_line_stream_feed(&state_->stream, bytes.data(), bytes.size());
}

hal_status_t TelemetryParser::finish() {
  const hal_status_t status = vp37_line_stream_finish(&state_->stream);
  if (status != HAL_OK || state_->finalized)
    return status;
  state_->finalized = true;
  try {
    state_->parser.finish();
    return HAL_OK;
  } catch (const std::bad_alloc &) {
    state_->stream.status = HAL_ENOMEM;
  } catch (...) {
    state_->stream.status = HAL_EINTERNAL;
  }
  return state_->stream.status;
}

int Series::column(std::string_view key) const {
  for (size_t i = 0; i < keys.size(); ++i) {
    if (keys[i] == key)
      return static_cast<int>(i);
  }
  return -1;
}

bool parse_value(std::string_view text, double &out) {
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    unsigned long long value = 0;
    const auto parsed =
        std::from_chars(text.data() + 2, text.data() + text.size(), value, 16);
    if (parsed.ec != std::errc() || parsed.ptr == text.data() + 2)
      return false;
    out = static_cast<double>(value);
    return true;
  }
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), out);
  if (parsed.ec != std::errc())
    return false;
  // A unit suffix (`34626Hz`, `18.0C`) is allowed; letters glued to digits
  // that are not a unit (`1e`, `5x3`) are not numbers.
  for (const char *p = parsed.ptr; p < text.data() + text.size(); ++p) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '%'))
      return false;
  }
  return true;
}

Recording parse_log(std::string_view text) {
  Recording recording;
  TelemetryParser parser(recording);
  if (parser.feed(text) != HAL_OK || parser.finish() != HAL_OK) {
    throw std::runtime_error(
        "cannot parse telemetry (line limit or allocation failure)");
  }
  return recording;
}

} // namespace vp37
