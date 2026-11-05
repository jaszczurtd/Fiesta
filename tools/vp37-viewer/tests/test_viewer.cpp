// Behaviour tests of the viewer on fragments of real logs (tests/fixtures,
// cut as described in tests/fixtures/README.md) and on a small fixture tree.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <regex>
#include <string>
#include <vector>

#include "builder.h"
#include "export.h"
#include "hal/core/jh_endian.h"
#include "log_source.h"
#include "metrics_view.h"
#include "recording_cache.h"
#include "runs_model.h"
#include "telemetry.h"

namespace fs = std::filesystem;
using namespace vp37;

namespace {

int failures = 0;
std::string current_test;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      ++failures;                                                              \
      std::fprintf(stderr, "%s: %s:%d: CHECK(%s)\n", current_test.c_str(),     \
                   __FILE__, __LINE__, #condition);                            \
    }                                                                          \
  } while (0)

const std::string kFixtures = VP37V_FIXTURES;
const std::string kWork = VP37V_WORK;

double value_at(const Series &series, const char *key, size_t row) {
  const int column = series.column(key);
  return column < 0 ? NAN : series.columns[size_t(column)][row];
}

bool has_step(const Recording &rec, const std::string &test, double percent,
              int index) {
  for (const StepEvent &step : rec.steps) {
    if (step.test == test && step.percent == percent && step.index == index)
      return true;
  }
  return false;
}

// Fresh copy of the fixture tree for tests that build or change files.
std::string fresh_tree(const std::string &name) {
  const std::string dir = kWork + "/" + name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  fs::copy(kFixtures + "/tree", dir + "/root", fs::copy_options::recursive);
  return dir;
}

BuildOptions options_for(const std::string &dir) {
  BuildOptions options;
  options.root = dir + "/root";
  options.site = dir + "/site";
  options.cache = dir + "/cache";
  options.descriptions = dir + "/descriptions";
  options.web = VP37V_WEB;
  options.jobs = 2;
  options.quiet = true;
  return options;
}

const MetricsFile *metrics_file(const Index &index, const std::string &rel) {
  for (const MetricsFile &file : index.metrics) {
    if (file.rel == rel)
      return &file;
  }
  return nullptr;
}

std::string page_json(const std::string &html) {
  static const std::regex data(R"(VP37\.page=([\s\S]*?);</script>)");
  std::smatch match;
  return std::regex_search(html, match, data) ? std::string(match[1])
                                              : std::string();
}

void test_rev82_gz() {
  const Recording rec =
      parse_log(read_log(kFixtures + "/logs/rev82-topzero.log.gz"));
  CHECK(rec.revision == 82);
  CHECK(rec.control.rows() == 45);
  CHECK(rec.control.column("pdt") >= 0);
  CHECK(has_step(rec, "top", 75.0, 1));
  CHECK(rec.undescribed.empty());
  CHECK(rec.time_reversals == 0);
  // Values come through as numbers, with unit suffixes and hex accepted.
  CHECK(rec.adj.rows() == 3);
  CHECK(std::isfinite(value_at(rec.adj, "f", 0)));
  CHECK(std::isfinite(value_at(rec.adj, "fl", 0)));
}

void test_rev114_markers_and_steps() {
  const Recording rec = parse_log(
      read_log(kFixtures + "/tree/vp37_implementation_tests/"
                           "2026-10-15-hal-device-tree/hw-x1/hw-x1.log.gz"));
  CHECK(rec.revision == 114);
  CHECK(has_step(rec, "topzero", 95.0, 1));
  bool reset = false, end = false;
  for (const HostEvent &host : rec.hosts) {
    reset |= host.text == "HOST TX gate-reset R" && host.t == 69.462139 &&
             host.label == "gate-reset";
    end |= host.text == "HOST TX gate-end T5.0" && host.command == "T5.0";
  }
  CHECK(reset && end);
  CHECK(rec.pulse.column("Ion") >= 0);
  CHECK(rec.undescribed.empty());
}

void test_rev148_new_field() {
  const Recording rec =
      parse_log(read_log(kFixtures + "/logs/rev148-install.log.gz"));
  CHECK(rec.revision == 148);
  CHECK(rec.control.column("cg") >= 0);
  CHECK(rec.undescribed.count("C cg") == 0);
  bool manual = false;
  for (const TestSegment &test : rec.tests)
    manual |= test.test == "manual";
  CHECK(manual);
}

void test_rev149_grid_fields() {
  // rev149 reports the deadline grid in CFG: `late` (window maximum of the
  // start after the deadline) and `ovr` (whole-period overruns).
  const Recording rec =
      parse_log(read_log(kFixtures + "/logs/rev149-grid.log.gz"));
  CHECK(rec.revision == 149);
  CHECK(rec.configs.size() == 1);
  bool late = false, ovr = false;
  for (const auto &field : rec.configs[0].fields) {
    late |= field.first == "late" && field.second == "74";
    ovr |= field.first == "ovr" && field.second == "0";
  }
  CHECK(late && ovr);
  CHECK(rec.undescribed.empty());
  CHECK(rec.control.rows() == 38);
  CHECK(has_step(rec, "top", 75.0, 1));
}

void test_rev205_force_return_field() {
  const Recording rec =
      parse_log(read_log(kFixtures + "/logs/rev205-force-return.log.gz"));
  CHECK(rec.revision == 205);
  CHECK(rec.configs.size() == 1);
  bool travel = false;
  for (const auto &field : rec.configs[0].fields) {
    travel |= field.first == "fret" && field.second == "20.0";
  }
  CHECK(travel);
  CHECK(rec.undescribed.count("CFG fret") == 0);
}

void test_rev203_force_offset_field() {
  const Recording rec =
      parse_log(read_log(kFixtures + "/logs/rev203-force.log.gz"));
  CHECK(rec.revision == 203);
  CHECK(rec.configs.size() == 1);
  bool offset = false;
  for (const auto &field : rec.configs[0].fields) {
    offset |= field.first == "foff" && field.second == "12.000";
  }
  CHECK(offset);
  CHECK(rec.undescribed.count("CFG foff") == 0);
}

void test_rev154_holding_fields() {
  const Recording rec =
      parse_log(read_log(kFixtures + "/logs/rev154-holding.log.gz"));
  CHECK(rec.revision == 154);
  CHECK(rec.control.column("uh") >= 0);
  CHECK(rec.control.column("hb") >= 0);
  CHECK(rec.undescribed.empty());
  bool active = false, inactive = false, bias = false;
  for (size_t row = 0; row < rec.control.rows(); ++row) {
    active |= value_at(rec.control, "uh", row) == 1.0;
    inactive |= value_at(rec.control, "uh", row) == 0.0;
    bias |= std::fabs(value_at(rec.control, "hb", row)) > 0.0;
  }
  CHECK(active && inactive && bias);
}

void test_unknown_key_and_kind() {
  // A newer firmware line: an undescribed key and an unknown line kind are
  // kept and reported, never dropped.
  std::string text = read_log(kFixtures + "/logs/rev148-install.log.gz");
  text = std::regex_replace(text, std::regex(" mtrim:"), " zz:5 mtrim:");
  text += "99.000000 ECU: VP37 NEWKIND a:1 b:2\n";
  const Recording rec = parse_log(text);
  CHECK(rec.undescribed.count("C zz") == 1);
  CHECK(rec.undescribed.at("C zz") == rec.control.rows());
  CHECK(value_at(rec.control, "zz", 0) == 5.0);
  CHECK(rec.unknown_kinds.count("NEWKIND") == 1);
}

void test_time_reversal() {
  const Recording rec =
      parse_log("10.0 ECU: VP37 C tar:1\n11.0 ECU: VP37 C tar:2\n1.0 ECU: VP37 "
                "C tar:3\n2.0 ECU: VP37 C tar:4\n");
  CHECK(rec.time_reversals == 1);
  CHECK(rec.control.t[2] == 11.0 && rec.control.t[3] == 12.0);
}

void test_incremental_telemetry() {
  const auto text = read_log(kFixtures + "/logs/rev148-install.log.gz");
  const auto whole = serialize_recording(parse_log(text));
  for (const size_t chunk : {size_t(1), size_t(7), size_t(257)}) {
    Recording rec;
    TelemetryParser parser(rec);
    for (size_t at = 0; at < text.size(); at += chunk) {
      CHECK(parser.feed(std::string_view(text).substr(at, chunk)) == HAL_OK);
    }
    CHECK(parser.finish() == HAL_OK);
    CHECK(serialize_recording(rec) == whole);
    CHECK(parser.finish() == HAL_OK);
    CHECK(serialize_recording(rec) == whole);
    CHECK(parser.feed("\n") == HAL_ESTATE);
  }
  Recording partial;
  TelemetryParser parser(partial);
  CHECK(parser.feed("1.0 ECU: VP37 C us:100 tar:") == HAL_OK);
  CHECK(partial.control.rows() == 0);
  CHECK(parser.feed("25\r\n1.5 ECU: VP37 CFG rev:208 scanus:100\n") == HAL_OK);
  CHECK(partial.control.rows() == 1);
  CHECK(value_at(partial.control, "tar", 0) == 25.0);
  CHECK(parser.feed("2.0 ECU: VP37 CFG rev:208 scanus:200") == HAL_OK);
  CHECK(partial.config.rows() == 1);
  CHECK(parser.finish() == HAL_OK);
  CHECK(partial.config.rows() == 2);
  CHECK(value_at(partial.config, "scanus", 0) == 100.0);
  CHECK(value_at(partial.config, "scanus", 1) == 200.0);
  CHECK(partial.configs.size() == 1); // revision summaries keep their meaning
}

void test_mcu_timeline() {
  Recording rec;
  TelemetryParser parser(rec, true);
  CHECK(parser.feed("10.0 ECU: VP37 C us:4294967040 adj:1\n"
                    "10.01 ECU: VP37 C us:4744 adj:2\n"
                    "10.1 ECU: VP37 IPULSE us:4444 state_us:4744 Ion:1\n"
                    "20.0 ECU: VP37 T us:4294967040 adj:1\n"
                    "20.01 ECU: VP37 T us:4744 adj:2\n"
                    "20.1 ECU: VP37 C us:20 adj:3\n"
                    "20.2 ECU: VP37 C adj:4\n") == HAL_OK);
  CHECK(parser.finish() == HAL_OK);
  CHECK(rec.control.rows() == 4 && rec.trace.rows() == 2);
  CHECK(std::fabs(rec.control.t[1] - 10.005) < 1e-9);
  CHECK(std::fabs(rec.pulse.t[0] - 10.0047) < 1e-9);
  CHECK(std::fabs(rec.trace.t[0] - 10.0) < 1e-9);
  CHECK(std::fabs(rec.trace.t[1] - 10.005) < 1e-9);
  CHECK(rec.control.t[2] == 20.1); // counter reset starts a new host anchor
  CHECK(rec.control.t[3] == 20.2); // old telemetry without us keeps host time
}

void test_line_stream_limits() {
  struct Sink {
    size_t calls = 0;
    size_t bytes = 0;
  } sink;
  const auto receive = [](void *context, const char *,
                          size_t size) -> hal_status_t {
    auto &out = *static_cast<Sink *>(context);
    ++out.calls;
    out.bytes += size;
    return HAL_OK;
  };
  char buffer[4];
  vp37_line_stream_t stream{};
  CHECK(vp37_line_stream_init(&stream, buffer, sizeof buffer, receive, &sink) ==
        HAL_OK);
  CHECK(vp37_line_stream_feed(&stream, "abcd", 4) == HAL_OK);
  CHECK(sink.calls == 0);
  CHECK(vp37_line_stream_feed(&stream, "\n", 1) == HAL_OK);
  CHECK(sink.calls == 1 && sink.bytes == 4);
  CHECK(vp37_line_stream_feed(&stream, "abcde", 5) == HAL_EOVERFLOW);
  CHECK(vp37_line_stream_finish(&stream) == HAL_EOVERFLOW);
  CHECK(sink.calls == 1);
  CHECK(vp37_line_stream_init(&stream, buffer, sizeof buffer, receive, &sink) ==
        HAL_OK);
  CHECK(vp37_line_stream_feed(&stream, "xy", 2) == HAL_OK);
  CHECK(vp37_line_stream_finish(&stream) == HAL_OK);
  CHECK(vp37_line_stream_finish(&stream) == HAL_OK);
  CHECK(sink.calls == 2 && sink.bytes == 6);
}

void test_gzip_checks() {
  const std::string gz =
      read_file(kFixtures + "/logs/rev82-topzero.log.gz", 1u << 20);
  const std::string text = gunzip(gz, 1u << 26);
  CHECK(text.find(" VP37 CFG rev:82") != std::string::npos);
  for (const size_t at : {gz.size() - 6, gz.size() - 2}) { // CRC-32 and ISIZE
    std::string damaged = gz;
    damaged[at] = char(damaged[at] ^ 0x5a);
    bool thrown = false;
    try {
      (void)gunzip(damaged, 1u << 26);
    } catch (const Error &) {
      thrown = true;
    }
    CHECK(thrown);
  }
  bool second_member = false;
  try {
    (void)gunzip(gz + gz, 1u << 26);
  } catch (const Error &) {
    second_member = true;
  }
  CHECK(second_member);
}

void test_cache_round_trip() {
  const Recording rec =
      parse_log(read_log(kFixtures + "/logs/rev82-topzero.log.gz"));
  const std::string bytes = serialize_recording(rec);
  const auto back = deserialize_recording(bytes);
  CHECK(back.has_value());
  if (!back)
    return;
  CHECK(serialize_recording(*back) == bytes);
  CHECK(back->revision == 82 && back->steps.size() == rec.steps.size());
  CHECK(back->control.rows() == rec.control.rows());
  CHECK(value_at(back->control, "tar", 3) == value_at(rec.control, "tar", 3));
  CHECK(!deserialize_recording(bytes.substr(0, bytes.size() - 1)));
  // The parser version is part of the cache file name.
  CHECK(recording_cache_path("c", std::string(64, 'a'))
            .find("-p" + std::to_string(kParserVersion) + ".bin") !=
        std::string::npos);

  Recording fractions;
  fractions.control.t = {1.0, 2.0, 3.0};
  fractions.control.keys = {"fraction"};
  fractions.control.columns = {{1.25, NAN, -2.5}};
  const auto floats = deserialize_recording(serialize_recording(fractions));
  CHECK(floats.has_value());
  if (floats) {
    CHECK(value_at(floats->control, "fraction", 0) == 1.25);
    CHECK(std::isnan(value_at(floats->control, "fraction", 1)));
    CHECK(value_at(floats->control, "fraction", 2) == -2.5);
  }

  // The final field is the last unknown-kind counter, encoded as uint64_t.
  // A 64-bit cache must not silently truncate that counter on Win32.
  Recording counters;
  counters.unknown_kinds["NEW"] = 1;
  std::string wide = serialize_recording(counters);
  jh_store_le64(reinterpret_cast<uint8_t *>(wide.data() + wide.size() - 8),
                UINT64_C(4294967296));
  const auto counted = deserialize_recording(wide);
  if (sizeof(size_t) == 4) {
    CHECK(!counted);
  } else {
    CHECK(counted && counted->unknown_kinds.at("NEW") == UINT64_C(4294967296));
  }
}

void test_overview_keeps_spikes() {
  Recording rec;
  for (int i = 0; i < 20000; ++i)
    rec.control.t.push_back(i * 0.025);
  rec.control.keys = {"tar"};
  rec.control.columns.assign(1, std::vector<double>(20000, 100.0));
  rec.control.columns[0][12345] = 9999.0;
  rec.first_t = 0;
  rec.last_t = rec.control.t.back();
  JsonWriter out;
  write_overview(out, rec);
  const auto doc = JsonDoc::parse(out.finish(), "overview");
  const Json hi = doc->root()["C"]["tar"]["hi"];
  CHECK(hi.size() == size_t(kOverviewBuckets));
  bool spike = false;
  for (const Json &value : hi.items())
    spike |= value.number().value_or(0) == 9999.0;
  CHECK(spike);
}

void test_histogram_reports_outside_axis() {
  Series series;
  series.keys = {"pdt"};
  series.columns.resize(1);
  for (int i = 0; i < 1000; ++i) {
    series.t.push_back(i);
    series.columns[0].push_back(i == 500 ? 250000.0 : 5000.0 + (i % 7));
  }
  const Histogram h = histogram(series, "pdt", "us", 0, 999);
  size_t counted = 0;
  for (size_t count : h.bins)
    counted += count;
  CHECK(h.n == 1000);
  CHECK(h.above == 1);
  CHECK(counted + h.above + h.below == h.n);
  CHECK(h.max == 250000.0);
  CHECK(h.hi < 250000.0);
  const Histogram window = histogram(series, "pdt", "us", 100, 199);
  CHECK(window.n == 100 && window.above == 0);
}

void test_flatten_matches_gate_labels() {
  const auto doc = JsonDoc::parse(
      R"({"cyclic_profiles":{"2ms":{"p95_abs_err_hz":662.5}},"top_steps":{"0%":{"early_ringing_error_rms_hz":3.0}},)"
      R"("manual_holds":{"S20":{"tracking_error_hz":{"mean":-4.5,"sd":6.0,"p95":-30.0},"absolute_error_hz":{"p95":31.0}}},)"
      R"("control_period_us":{"p95":5120.0}})",
      "flatten");
  const MetricValues values = flatten_metrics(doc->root());
  CHECK(metric_value(values, "cyclic 2ms p95_abs_err_hz") == 662.5);
  CHECK(metric_value(values, "top 0% early_ringing_error_rms_hz [spoczynek]") ==
        3.0);
  CHECK(metric_value(values, "S20 mean") == 4.5);
  CHECK(!metric_value(values, "S20 p95"));
  CHECK(metric_value(values, "S20 p95_abs_error_hz") == 31.0);
  CHECK(metric_value(values, "control_period_us p95") == 5120.0);
  // Threshold 0: any increase over any reference is worse.
  CHECK(gate_status(10.0, {9.0, 11.0}) == "worse");
  CHECK(gate_status(10.0, {10.0, 11.0}) == "no_increase");
  CHECK(gate_status(10.1, {10.0}) == "worse"); // 1 %: no noise allowance
  CHECK(gate_status(std::nullopt, {10.0}) == "missing");
  CHECK(change_percent(10.5, {10.0, 11.0}).value_or(0) == 5.0);
}

void test_binding_hal_device_tree() {
  const std::string dir = fresh_tree("binding");
  Model model;
  index_runs(model, options_for(dir));
  const std::string iteration =
      "vp37_implementation_tests/2026-10-15-hal-device-tree";
  const MetricsFile *own =
      metrics_file(model.index, iteration + "/hw-x1/metrics.json");
  const MetricsFile *other =
      metrics_file(model.index, iteration + "/ref-hw-x1-metrics.json");
  CHECK(own && other);
  if (!own || !other)
    return;
  CHECK(own->status == BindingStatus::bound);
  CHECK(own->run_id == iteration + "/hw-x1/hw-x1");
  CHECK(metric_value(flatten_metrics(own->doc->root()),
                     "cyclic 2ms p95_abs_err_hz") == 662.5);
  // Lies next to the run but was computed from another log.
  CHECK(other->status == BindingStatus::source_missing);
  CHECK(other->run_id.empty());
  const Run *run = model.index.find(iteration + "/hw-x1/hw-x1");
  CHECK(run && run->metrics.size() == 1);

  // The same metrics with a window that is not in the log do not bind.
  std::string text =
      read_file(dir + "/root/" + iteration + "/hw-x1/metrics.json", 1u << 24);
  text = std::regex_replace(text, std::regex("69\\.462139"), "70.462139");
  write_file(dir + "/root/" + iteration + "/hw-x1/shifted-metrics.json", text);
  // An old parent directory does not change the identity of the data area.
  std::string moved =
      read_file(dir + "/root/" + iteration + "/hw-x1/metrics.json", 1u << 24);
  moved = std::regex_replace(moved,
                             std::regex("recordings/vp37_implementation_tests"),
                             "recordings/old-parent/vp37_implementation_tests");
  write_file(dir + "/root/" + iteration + "/hw-x1/moved-metrics.json", moved);
  Model again;
  index_runs(again, options_for(dir));
  const MetricsFile *shifted =
      metrics_file(again.index, iteration + "/hw-x1/shifted-metrics.json");
  const MetricsFile *old_path =
      metrics_file(again.index, iteration + "/hw-x1/moved-metrics.json");
  CHECK(shifted && shifted->status == BindingStatus::window_mismatch &&
        shifted->run_id.empty());
  CHECK(old_path && old_path->status == BindingStatus::bound);
}

void test_describe_and_runs_json() {
  const std::string dir = fresh_tree("describe");
  Model model;
  index_runs(model, options_for(dir));
  const std::string series =
      "vp37_implementation_tests/2026-01-01-fixture-gate";
  const auto d = describe_iteration(model.index, series);
  CHECK(d.has_value());
  if (!d)
    return;
  CHECK(d->comparisons.size() == 2);
  bool raw = false, filtered = false;
  for (const Comparison &c : d->comparisons) {
    CHECK(c.candidates == std::vector<std::string>{"candidate-x1"});
    CHECK(c.references.size() == 2 && c.references[0].run == "reference-a1" &&
          c.references[0].position == "before" &&
          c.references[1].position == "after");
    raw |= c.basis == "raw";
    filtered |= c.basis == "filtered";
  }
  CHECK(raw && filtered);
  for (const RunRole &run : d->runs) {
    if (run.id == "candidate-x1")
      CHECK(run.kind == "candidate" && run.repeat == 1);
    if (run.id == "reference-a2")
      CHECK(run.kind == "reference" && run.repeat == 2);
  }
  CHECK(d->variants.size() == 2);
  // runs.json survives a write and read unchanged.
  const std::string text = description_json(*d);
  const auto doc = JsonDoc::parse(text, "runs.json");
  CHECK(description_json(parse_description(doc->root(), series)) == text);
  // The other iteration has nothing that names roles: no draft.
  CHECK(!describe_iteration(
      model.index, "vp37_implementation_tests/2026-10-15-hal-device-tree"));
}

void test_sha_mismatch_unbinds() {
  const std::string dir = fresh_tree("sha");
  BuildOptions options = options_for(dir);
  Model model;
  index_runs(model, options);
  const std::string series =
      "vp37_implementation_tests/2026-01-01-fixture-gate";
  auto d = describe_iteration(model.index, series);
  CHECK(d.has_value());
  if (!d)
    return;
  for (RunRole &run : d->runs) {
    if (run.id == "reference-a1")
      run.sha256 = std::string(64, '0');
  }
  write_file(options.descriptions + "/" + description_file_name(series),
             description_json(*d));
  Model described;
  index_runs(described, options);
  const MetricsFile *file =
      metrics_file(described.index, series + "/reference-a1/metrics.json");
  CHECK(file && file->status == BindingStatus::sha_mismatch &&
        file->run_id.empty());
  const MetricsFile *kept =
      metrics_file(described.index, series + "/reference-a2/metrics.json");
  CHECK(kept && kept->status == BindingStatus::bound);
}

void test_site_uses_metrics_files_and_refreshes() {
  const std::string dir = fresh_tree("site");
  BuildOptions options = options_for(dir);
  const std::string series =
      "vp37_implementation_tests/2026-01-01-fixture-gate";
  {
    Model model;
    index_runs(model, options);
    write_file(options.descriptions + "/" + description_file_name(series),
               description_json(*describe_iteration(model.index, series)));
  }
  const BuildStats first = build_site(options);
  CHECK(first.runs == 4);
  CHECK(first.run_pages_written == 4);
  // The candidate page shows the value of its metrics file (impossible for
  // this short log, so it cannot come from an own computation), and the
  // comparison rows come from the gate's comparison file.
  const std::string candidate =
      read_file(options.site + "/run/" +
                    run_slug(series + "/candidate-x1/candidate-x1") + ".html",
                1u << 26);
  const auto page = JsonDoc::parse(page_json(candidate), "candidate page");
  const Json metrics = page->root()["metrics"];
  CHECK(metrics.size() == 1); // the raw metrics name a deleted log: unbound
  CHECK(metrics.at(0)["values"]["cyclic 2ms p95_abs_err_hz"].number() ==
        12345.5);
  const std::string other =
      read_file(options.site + "/run/" +
                    run_slug("vp37_implementation_tests/"
                             "2026-10-15-hal-device-tree/hw-x1/hw-x1") +
                    ".html",
                1u << 26);
  const auto hw = JsonDoc::parse(page_json(other), "hw-x1 page");
  CHECK(hw->root()["metrics"].size() == 1);
  CHECK(hw->root()["metrics"]
            .at(0)["values"]["cyclic 2ms p95_abs_err_hz"]
            .number() == 662.5);
  const std::string device_tree = read_file(
      options.site + "/it/" +
          iteration_slug(
              "vp37_implementation_tests/2026-10-15-hal-device-tree") +
          ".html",
      1u << 26);
  const auto files = JsonDoc::parse(page_json(device_tree), "device tree page");
  size_t unbound = 0;
  for (const Json &file : files->root()["iteration"]["metrics_files"].items()) {
    if (file["rel"].str().find("ref-hw-x1-metrics.json") != std::string::npos) {
      unbound += file["status"].str() == "unbound" && file["run"].str().empty();
    }
  }
  CHECK(unbound == 1);
  const std::string iteration = read_file(
      options.site + "/it/" + iteration_slug(series) + ".html", 1u << 26);
  const auto it = JsonDoc::parse(page_json(iteration), "iteration page");
  const Json comparisons = it->root()["iteration"]["comparisons"];
  CHECK(comparisons.size() == 2);
  for (const Json &c : comparisons.items()) {
    CHECK(c["rows"].size() == 2);
    CHECK(c["rows"].at(0)["s"].str() == "worse");
    CHECK(c["rows"].at(0)["c"].number() == 105.0);
  }
  CHECK(it->root()["iteration"]["verdict"].str().find("Kandydat gorszy") !=
        std::string::npos);

  // Nothing changed: every page is kept.
  const BuildStats second = build_site(options);
  CHECK(second.run_pages_written == 0 && second.run_pages_kept == 4);
  // A changed metrics file refreshes only the page of its run.
  const std::string path =
      options.root + "/" + series + "/reference-a2/metrics.json";
  write_file(path, read_file(path, 1u << 24) + "\n");
  const BuildStats third = build_site(options);
  CHECK(third.run_pages_written == 1);
  // So does a changed runs.json for the runs of its iteration.
  const std::string runs =
      options.descriptions + "/" + description_file_name(series);
  write_file(runs, read_file(runs, 1u << 24) + "\n");
  const BuildStats fourth = build_site(options);
  CHECK(fourth.run_pages_written == 3);
}

void test_report_verdict() {
  std::string section;
  CHECK(report_verdict("# T\n\nlead\n\n## Werdykt\n\n**X.** y\nz\n\n## A\n",
                       &section) == "**X.** y\nz");
  CHECK(section == "Werdykt");
  CHECK(report_verdict("# T\n\nlead line\n\n## A\n", &section) == "lead line");
  CHECK(section == "wstęp");
  CHECK(report_verdict("no headings").empty());
}

} // namespace

int main() {
  const std::vector<std::pair<const char *, std::function<void()>>> tests = {
      {"rev82_gz", test_rev82_gz},
      {"rev114_markers_and_steps", test_rev114_markers_and_steps},
      {"rev148_new_field", test_rev148_new_field},
      {"rev149_grid_fields", test_rev149_grid_fields},
      {"rev154_holding_fields", test_rev154_holding_fields},
      {"rev205_force_return_field", test_rev205_force_return_field},
      {"rev203_force_offset_field", test_rev203_force_offset_field},
      {"unknown_key_and_kind", test_unknown_key_and_kind},
      {"time_reversal", test_time_reversal},
      {"incremental_telemetry", test_incremental_telemetry},
      {"mcu_timeline", test_mcu_timeline},
      {"line_stream_limits", test_line_stream_limits},
      {"gzip_checks", test_gzip_checks},
      {"cache_round_trip", test_cache_round_trip},
      {"overview_keeps_spikes", test_overview_keeps_spikes},
      {"histogram_reports_outside_axis", test_histogram_reports_outside_axis},
      {"flatten_matches_gate_labels", test_flatten_matches_gate_labels},
      {"binding_hal_device_tree", test_binding_hal_device_tree},
      {"describe_and_runs_json", test_describe_and_runs_json},
      {"sha_mismatch_unbinds", test_sha_mismatch_unbinds},
      {"site_uses_metrics_files_and_refreshes",
       test_site_uses_metrics_files_and_refreshes},
      {"report_verdict", test_report_verdict},
  };
  for (const auto &[name, test] : tests) {
    current_test = name;
    const int before = failures;
    try {
      test();
    } catch (const std::exception &error) {
      ++failures;
      std::fprintf(stderr, "%s: exception: %s\n", name, error.what());
    }
    std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", name);
  }
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
