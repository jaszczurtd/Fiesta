#include "site.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <regex>
#include <set>

#include "export.h"
#include "log_source.h"
#include "metrics_view.h"

namespace vp37 {

namespace fs = std::filesystem;

namespace {

bool starts_with(const std::string &text, const std::string &prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

std::string parent_of(const std::string &rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? std::string() : rel.substr(0, slash);
}

std::string base_name(const std::string &rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

std::string optional_file(const std::string &path) {
  std::error_code error;
  if (!fs::is_regular_file(path, error))
    return std::string();
  return read_file(path, size_t(16) << 20);
}

std::shared_ptr<JsonDoc> optional_json(const std::string &path) {
  std::error_code error;
  if (!fs::is_regular_file(path, error))
    return nullptr;
  try {
    return std::shared_ptr<JsonDoc>(JsonDoc::load(path));
  } catch (const Error &) {
    return nullptr;
  }
}

std::string area_of(const std::string &series) {
  for (const std::string &area : data_areas()) {
    if (series == area || starts_with(series, area + "/"))
      return area;
  }
  return std::string();
}

std::string report_title(const std::string &md) {
  size_t start = 0;
  while (start < md.size()) {
    const size_t end = md.find('\n', start);
    const std::string line = md.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (starts_with(line, "# "))
      return line.substr(2);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return std::string();
}

void write_window(JsonWriter &out, const std::optional<Window> &window) {
  if (!window) {
    out.null();
    return;
  }
  out.begin_object();
  out.key("kind").string(window->kind);
  out.key("start_s").number(window->start_s);
  out.key("end_s").number(window->end_s);
  out.key("start_marker").string(window->start_marker);
  out.key("end_marker").string(window->end_marker);
  out.end_object();
}

void write_values(JsonWriter &out, const MetricValues &values) {
  out.begin_object();
  for (const auto &[label, value] : values)
    out.key(label).number(value);
  out.end_object();
}

void write_rows(JsonWriter &out, const std::vector<ComparisonRow> &rows) {
  out.begin_array();
  for (const ComparisonRow &row : rows) {
    out.begin_object();
    out.key("m").string(row.metric);
    out.key("c");
    if (row.candidate)
      out.number(*row.candidate);
    else
      out.null();
    out.key("r").begin_array();
    for (const auto &reference : row.references) {
      if (reference)
        out.number(*reference);
      else
        out.null();
    }
    out.end_array();
    out.key("s").string(row.status);
    out.key("p");
    if (row.change_percent)
      out.number(*row.change_percent);
    else
      out.null();
    out.end_object();
  }
  out.end_array();
}

// Bound metrics files of a run, including those of its raw-position log.
std::vector<const MetricsFile *> run_metrics(const Model &model,
                                             const Run &run) {
  std::vector<const MetricsFile *> out;
  for (size_t index : run.metrics)
    out.push_back(&model.index.metrics[index]);
  for (const Run &other : model.index.runs) {
    if (other.raw_of != run.id)
      continue;
    for (size_t index : other.metrics)
      out.push_back(&model.index.metrics[index]);
  }
  return out;
}

const MetricsFile *first_basis(const std::vector<const MetricsFile *> &files,
                               const std::string &basis) {
  for (const MetricsFile *file : files) {
    if (file->basis == basis && file->status == BindingStatus::bound)
      return file;
  }
  for (const MetricsFile *file : files) {
    if (file->basis == basis)
      return file;
  }
  return nullptr;
}

void write_metrics_file(JsonWriter &out, const MetricsFile &file,
                        bool with_values) {
  out.begin_object();
  out.key("rel").string(file.rel);
  out.key("basis").string(file.basis);
  out.key("status").string(is_bound(file.status) ? "bound" : "unbound");
  out.key("status_text").string(binding_status_text(file.status));
  out.key("reason").string(file.reason);
  out.key("source").string(file.source);
  out.key("run").string(file.run_id);
  out.key("window");
  write_window(out, file.window);
  if (with_values && file.doc) {
    const Json root = file.doc->root();
    out.key("values");
    write_values(out, flatten_metrics(root));
    out.key("warnings").node(root["warnings"]);
    out.key("control_period_us").node(root["control_period_us"]);
    out.key("feedback_age_us").node(root["feedback_age_us"]);
    out.key("tests_seen").node(root["tests_seen"]);
    out.key("faults").node(root["faults"]);
    out.key("registry").node(root["registry"]);
  }
  out.end_object();
}

std::string page(const std::string &title, const std::string &base,
                 const std::string &kind, const std::string &data_json,
                 const std::vector<std::string> &scripts) {
  std::string html =
      "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
      "<meta name=\"viewport\" content=\"width=device-width, "
      "initial-scale=1\">\n"
      "<title>" +
      html_escape(title) + "</title>\n<link rel=\"stylesheet\" href=\"" + base +
      "assets/viewer.css\">\n</head>\n<body data-page=\"" + kind +
      "\">\n<header class=\"top\"><a href=\"" + base +
      "index.html\" data-i18n=\"Comparison overview\">Comparison "
      "overview</a><a href=\"" +
      base +
      "compare.html\" data-i18n=\"A/B comparison\">A/B comparison</a><span "
      "class=\"title\">" +
      html_escape(title) +
      "</span></header>\n<main id=\"app\"><noscript>This page requires "
      "JavaScript.</noscript></main>\n"
      "<script src=\"" +
      base + "assets/i18n.js\"></script>\n<script src=\"" + base +
      "assets/viewer.js\"></script>\n<script>VP37.base=\"" + base +
      "\";VP37.page=" + script_safe(data_json) + ";</script>\n";
  for (const std::string &script : scripts) {
    html += "<script src=\"" + base + script + "\"></script>\n";
  }
  html += "<script>VP37.start();</script>\n</body>\n</html>\n";
  return html;
}

void remove_stale(const std::string &dir, const std::set<std::string> &keep) {
  std::error_code error;
  if (!fs::is_directory(dir, error))
    return;
  for (const auto &entry : fs::directory_iterator(dir, error)) {
    if (!keep.count(entry.path().filename().generic_string()))
      fs::remove_all(entry.path(), error);
  }
}

} // namespace

std::string html_escape(const std::string &text) {
  std::string out;
  for (char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out.push_back(c);
    }
  }
  return out;
}

std::string run_slug(const std::string &id) {
  std::string slug = id;
  std::replace(slug.begin(), slug.end(), '/', '~');
  return slug;
}

std::string iteration_slug(const std::string &series) {
  return run_slug(series);
}

const IterationView *Model::iteration_of(const Run &run) const {
  const auto found = iteration_by_series.find(run.series);
  return found == iteration_by_series.end() ? nullptr
                                            : &iterations[found->second];
}

const RunRole *Model::role_of(const Run &run) const {
  const IterationView *iteration = iteration_of(run);
  if (!iteration || !iteration->description)
    return nullptr;
  const std::string name = run_name(run);
  for (const RunRole &role : iteration->description->runs) {
    if (role.id == name)
      return &role;
  }
  return nullptr;
}

void build_model(Model &model, const std::string &descriptions_dir) {
  Index &index = model.index;
  model.iterations.clear();
  model.iteration_by_series.clear();
  const auto iteration_for = [&](const std::string &series) -> IterationView & {
    auto found = model.iteration_by_series.find(series);
    if (found == model.iteration_by_series.end()) {
      found = model.iteration_by_series.emplace(series, model.iterations.size())
                  .first;
      IterationView view;
      view.series = series;
      model.iterations.push_back(std::move(view));
    }
    return model.iterations[found->second];
  };
  for (size_t i = 0; i < index.runs.size(); ++i)
    iteration_for(index.runs[i].series).runs.push_back(i);
  for (size_t i = 0; i < index.metrics.size(); ++i) {
    const std::string series = series_of(index.metrics[i].rel);
    if (model.iteration_by_series.count(series))
      iteration_for(series).metrics.push_back(i);
  }
  for (IterationView &view : model.iterations) {
    view.slug = iteration_slug(view.series);
    view.area = area_of(view.series);
    const std::string name = base_name(view.series);
    static const std::regex date(R"(^(\d{4}-\d{2}-\d{2}))");
    std::smatch match;
    if (std::regex_search(name, match, date))
      view.date = match[1];
    const std::string dir = index.root + "/" + view.series;
    view.report_md = optional_file(dir + "/report.md");
    if (view.report_md.empty())
      view.report_md = optional_file(dir + "/README.md");
    if (!view.report_md.empty()) {
      view.report_sha = sha256_hex(view.report_md);
      view.title = report_title(view.report_md);
      view.verdict = report_verdict(view.report_md, &view.verdict_section);
    }
    view.base_commit = optional_file(dir + "/base-commit.txt");
    if (!view.base_commit.empty())
      view.base_commit_sha = sha256_hex(view.base_commit);
    try {
      view.description =
          load_description(index.root, view.series, descriptions_dir);
    } catch (const Error &error) {
      view.description_error = error.what();
    }
  }
  // A description names its logs by SHA-256: metrics of a log that changed
  // since then no longer belong to the described run.
  for (IterationView &view : model.iterations) {
    if (!view.description)
      continue;
    for (const RunRole &role : view.description->runs) {
      std::string id = view.series + "/" + role.log;
      for (const char *ext : {".log.gz", ".log"}) {
        const std::string suffix = ext;
        if (id.size() > suffix.size() &&
            id.compare(id.size() - suffix.size(), suffix.size(), suffix) == 0) {
          id.erase(id.size() - suffix.size());
          break;
        }
      }
      const auto found = index.run_by_id.find(id);
      if (found == index.run_by_id.end()) {
        view.description->notes.push_back("runs.json: brak logu " + role.log);
        continue;
      }
      Run &run = index.runs[found->second];
      if (role.sha256.empty() || role.sha256 == run.sha)
        continue;
      view.description->notes.push_back(
          role.id + ": SHA-256 logu różni się od runs.json");
      std::vector<Run *> owners = {&run};
      for (Run &other : index.runs) {
        if (other.raw_of == run.id)
          owners.push_back(&other);
      }
      for (Run *owner : owners) {
        for (size_t metrics : owner->metrics) {
          index.metrics[metrics].status = BindingStatus::sha_mismatch;
          index.metrics[metrics].run_id.clear();
        }
        owner->metrics.clear();
      }
    }
  }
  // Newest vp37-baseline-<date>-<rev>.md in the data root.
  std::error_code error;
  std::string newest;
  for (const auto &entry : fs::directory_iterator(index.root, error)) {
    const std::string name = entry.path().filename().generic_string();
    if (starts_with(name, "vp37-baseline-") && name > newest)
      newest = name;
  }
  static const std::regex baseline(
      R"(vp37-baseline-(\d{4}-\d{2}-\d{2})-(rev\w+)\.md)");
  std::smatch match;
  if (std::regex_match(newest, match, baseline)) {
    model.baseline = std::string(match[2]) + " (" + std::string(match[1]) + ")";
  }
}

std::string run_stamp(const Model &model, const Run &run) {
  std::string text = "generator " + std::to_string(kGeneratorVersion) +
                     "\nparser " + std::to_string(kParserVersion) + "\nlog " +
                     run.sha + " " + run.log.rel + "\n";
  for (const MetricsFile *file : run_metrics(model, run)) {
    text += "metrics " + file->rel + " " + file->sha + " " +
            std::to_string(int(file->status)) + "\n";
  }
  // Files near the run that did not bind still show on its page.
  for (const MetricsFile &file : model.index.metrics) {
    if (parent_of(file.rel) == parent_of(run.log.rel) &&
        file.run_id != run.id) {
      text += "near " + file.rel + " " + file.sha + " " +
              std::to_string(int(file.status)) + "\n";
    }
  }
  if (const IterationView *view = model.iteration_of(run)) {
    text +=
        "report " + view->report_sha + "\nbase " + view->base_commit_sha + "\n";
    if (view->description)
      text += "runs " + view->description->origin_sha + "\n";
    text += "describe-error " + view->description_error + "\n";
  }
  const std::string image = optional_file(
      model.index.root + "/" + parent_of(run.log.rel) + "/image.json");
  text += "image " + (image.empty() ? std::string() : sha256_hex(image)) + "\n";
  for (const Run &other : model.index.runs) {
    if (other.raw_of == run.id)
      text += "raw " + other.id + "\n";
  }
  return text;
}

void write_run_pages(const Model &model, const Run &run, const Recording &rec,
                     const std::string &site_dir) {
  const std::string slug = run_slug(run.id);
  const std::string data_dir = site_dir + "/data/" + slug;
  std::set<std::string> keep;
  const auto data_file = [&](const std::string &part, const std::string &json) {
    const std::string name = part + ".js";
    keep.insert(name);
    write_output(data_dir + "/" + name, js_put(slug, part, json));
  };
  {
    JsonWriter out;
    write_overview(out, rec);
    data_file("overview", out.finish());
  }
  const std::vector<std::string> fragments = fragment_parts(rec);
  for (size_t k = 0; k < fragments.size(); ++k)
    data_file("frag-" + std::to_string(k), fragments[k]);
  {
    JsonWriter out;
    write_steps(out, rec);
    data_file("steps", out.finish());
  }
  remove_stale(data_dir, keep);

  const IterationView *view = model.iteration_of(run);
  const RunRole *role = model.role_of(run);
  const auto metrics = run_metrics(model, run);
  const MetricsFile *window_source = first_basis(metrics, "filtered");
  if (!window_source)
    window_source = first_basis(metrics, "raw");

  JsonWriter out;
  out.begin_object();
  out.key("kind").string("run");
  out.key("slug").string(slug);
  out.key("id").string(run.id);
  out.key("series").string(run.series);
  out.key("series_slug").string(iteration_slug(run.series));
  out.key("name").string(run_name(run));
  out.key("raw_of").string(run.raw_of);
  out.key("raw_of_slug").string(run.raw_of.empty() ? "" : run_slug(run.raw_of));
  out.key("raw_children").begin_array();
  for (const Run &other : model.index.runs) {
    if (other.raw_of == run.id)
      out.string(run_slug(other.id));
  }
  out.end_array();
  out.key("log").begin_object();
  out.key("rel").string(run.log.rel);
  out.key("sha256").string(run.sha);
  out.key("bytes").integer(int64_t(run.log.size));
  out.key("duplicates").begin_array();
  for (const FileStamp &duplicate : run.duplicates)
    out.string(duplicate.rel);
  out.end_array();
  out.end_object();
  out.key("revision").integer(rec.revision);
  out.key("revisions").begin_array();
  for (const ConfigSnapshot &config : rec.configs)
    out.integer(config.revision);
  out.end_array();
  out.key("first_t").number(rec.first_t);
  out.key("last_t").number(rec.last_t);
  out.key("control_rows").integer(int64_t(rec.control.rows()));
  out.key("pulse_rows").integer(int64_t(rec.pulse.rows()));
  out.key("time_reversals").integer(int64_t(rec.time_reversals));
  out.key("untimed_lines").integer(int64_t(rec.untimed_lines));
  out.key("config").begin_array();
  if (!rec.configs.empty()) {
    for (const auto &[key, value] : rec.configs.front().fields) {
      out.begin_array().string(key).string(value).end_array();
    }
  }
  out.end_array();
  out.key("fields").begin_array();
  for (const auto *series : {&rec.control, &rec.pulse}) {
    const char *line = series == &rec.control ? "C" : "IPULSE";
    for (const std::string &key : series->keys) {
      const FieldInfo *info = find_field(line, key);
      out.begin_object();
      out.key("line").string(line);
      out.key("key").string(key);
      write_field_metadata(out, info);
      out.key("described").boolean(info != nullptr);
      out.end_object();
    }
  }
  out.end_array();
  out.key("undescribed").begin_object();
  for (const auto &[key, count] : rec.undescribed)
    out.key(key).integer(int64_t(count));
  out.end_object();
  out.key("unknown_kinds").begin_object();
  for (const auto &[key, count] : rec.unknown_kinds)
    out.key(key).integer(int64_t(count));
  out.end_object();

  out.key("role");
  if (role) {
    out.begin_object();
    out.key("kind").string(role->kind);
    out.key("repeat");
    if (role->repeat)
      out.integer(*role->repeat);
    else
      out.null();
    out.key("variant").string(role->variant);
    out.key("origin").string(
        view && view->description
            ? relative_to(model.index.root, view->description->origin)
            : "");
    out.end_object();
  } else {
    out.null();
  }
  out.key("image");
  if (const auto image = optional_json(
          model.index.root + "/" + parent_of(run.log.rel) + "/image.json")) {
    out.node(image->root());
  } else {
    out.null();
  }
  out.key("base_commit").string(view ? view->base_commit : "");

  out.key("metrics").begin_array();
  for (const MetricsFile *file : metrics)
    write_metrics_file(out, *file, true);
  out.end_array();
  out.key("near_metrics").begin_array();
  for (const MetricsFile &file : model.index.metrics) {
    if (parent_of(file.rel) == parent_of(run.log.rel) &&
        file.run_id != run.id) {
      write_metrics_file(out, file, false);
    }
  }
  out.end_array();

  // Histograms over the analysis window of the bound metrics, else the log.
  double start = rec.first_t, end = rec.last_t;
  std::string window_text = "cały log (brak przypisanych metryk z oknem)";
  if (window_source && window_source->window &&
      std::isfinite(window_source->window->start_s) &&
      std::isfinite(window_source->window->end_s)) {
    start = window_source->window->start_s;
    end = window_source->window->end_s;
    window_text = "okno analizy z " + window_source->rel;
  }
  out.key("window").begin_object();
  out.key("start").number(start);
  out.key("end").number(end);
  out.key("text").string(window_text);
  out.end_object();
  out.key("histograms").begin_array();
  write_histogram(out, histogram(rec.control, "pdt", "us", start, end));
  write_histogram(out, histogram(rec.control, "age", "us", start, end));
  out.end_array();

  out.key("tests").begin_array();
  for (const TestSegment &test : rec.tests) {
    out.begin_object();
    out.key("test").string(test.test);
    out.key("start").number(test.start);
    out.key("end").number(test.end);
    out.key("result").string(test.result);
    out.key("closed").boolean(test.closed);
    out.end_object();
  }
  out.end_array();
  out.key("steps").begin_array();
  for (const StepEvent &step : rec.steps) {
    out.begin_object();
    out.key("t").number(step.t);
    out.key("test").string(step.test);
    out.key("percent").number(step.percent);
    out.key("index").integer(step.index);
    out.key("count").integer(step.count);
    out.end_object();
  }
  out.end_array();
  out.key("hosts").begin_array();
  for (const HostEvent &host : rec.hosts) {
    if (host.label.empty())
      continue;
    out.begin_object();
    out.key("t").number(host.t);
    out.key("label").string(host.label);
    out.key("command").string(host.command);
    out.end_object();
  }
  out.end_array();
  out.key("report").begin_object();
  out.key("verdict").string(view ? view->verdict : "");
  out.key("section").string(view ? view->verdict_section : "");
  out.key("markdown").string(view ? view->report_md : "");
  out.end_object();
  out.end_object();

  const std::string title = run_name(run) + " - " + base_name(run.series);
  write_output(site_dir + "/run/" + slug + ".html",
               page(title, "../", "run", out.finish(),
                    {"data/" + slug + "/overview.js"}));
}

namespace {

void write_comparison(JsonWriter &out, const Model &model,
                      const IterationView &view, const Comparison &comparison) {
  const auto run_by_name = [&](const std::string &name) -> const Run * {
    for (size_t index : view.runs) {
      const Run &run = model.index.runs[index];
      if (run.raw_of.empty() && run_name(run) == name)
        return &run;
    }
    return nullptr;
  };
  const auto values_of =
      [&](const Run *run,
          const std::string &basis) -> std::optional<MetricValues> {
    if (!run)
      return std::nullopt;
    const MetricsFile *file = first_basis(run_metrics(model, *run), basis);
    if (!file || !is_bound(file->status) || !file->doc)
      return std::nullopt;
    return flatten_metrics(file->doc->root());
  };
  out.begin_object();
  out.key("id").string(comparison.id);
  out.key("basis").string(comparison.basis);
  out.key("described_from").string(comparison.described_from);
  out.key("result_file").string(comparison.result_file);
  out.key("candidates").begin_array();
  for (const std::string &candidate : comparison.candidates) {
    const Run *run = run_by_name(candidate);
    out.begin_object();
    out.key("name").string(candidate);
    out.key("id").string(run ? run->id : "");
    out.end_object();
  }
  out.end_array();
  out.key("references").begin_array();
  for (const ComparisonReference &reference : comparison.references) {
    const Run *run = run_by_name(reference.run);
    out.begin_object();
    out.key("name").string(reference.run);
    out.key("id").string(run ? run->id : "");
    out.key("position").string(reference.position);
    out.end_object();
  }
  out.end_array();
  std::vector<ComparisonRow> rows;
  std::string problem;
  if (!comparison.result_file.empty()) {
    const std::string path =
        model.index.root + "/" + view.series + "/" + comparison.result_file;
    if (const auto doc = optional_json(path)) {
      if (comparison.described_from == "gate-comparison") {
        std::vector<std::string> references;
        for (const ComparisonReference &reference : comparison.references)
          references.push_back(reference.run);
        rows = rows_from_gate_comparison(doc->root(), references);
      } else {
        rows = rows_from_comparison_file(doc->root());
      }
    } else {
      problem = "brak albo nieczytelny " + comparison.result_file;
    }
  } else if (comparison.candidates.size() != 1) {
    problem = "kilku kandydatów bez pliku porównania: wartości tylko na "
              "stronie iteracji";
  } else {
    const auto candidate =
        values_of(run_by_name(comparison.candidates.front()), comparison.basis);
    std::vector<MetricValues> references;
    for (const ComparisonReference &reference : comparison.references) {
      const auto values =
          values_of(run_by_name(reference.run), comparison.basis);
      if (!values) {
        problem = reference.run + ": brak przypisanych metryk (" +
                  comparison.basis + ")";
        break;
      }
      references.push_back(*values);
    }
    if (!candidate)
      problem = comparison.candidates.front() + ": brak przypisanych metryk (" +
                comparison.basis + ")";
    if (problem.empty())
      rows = rows_from_metrics(*candidate, references);
  }
  out.key("problem").string(problem);
  out.key("rows");
  write_rows(out, rows);
  out.end_object();
}

void write_iteration(JsonWriter &out, const Model &model,
                     const IterationView &view, bool full) {
  out.begin_object();
  out.key("series").string(view.series);
  out.key("slug").string(view.slug);
  out.key("area").string(view.area);
  out.key("date").string(view.date);
  out.key("title").string(view.title);
  out.key("verdict").string(view.verdict);
  out.key("verdict_section").string(view.verdict_section);
  out.key("described").boolean(view.description.has_value());
  out.key("description_error").string(view.description_error);
  out.key("origin").string(
      view.description ? relative_to(model.index.root, view.description->origin)
                       : "");
  out.key("notes").begin_array();
  if (view.description) {
    for (const std::string &note : view.description->notes)
      out.string(note);
  }
  out.end_array();
  out.key("variants").begin_array();
  if (view.description) {
    for (const Variant &variant : view.description->variants) {
      out.begin_object();
      out.key("id").string(variant.id);
      out.key("ecu_sha256").string(variant.ecu_sha256);
      out.key("adjustometer_sha256").string(variant.adjustometer_sha256);
      out.key("defines").string(variant.defines);
      out.end_object();
    }
  }
  out.end_array();
  out.key("runs").begin_array();
  for (size_t index : view.runs)
    out.string(model.index.runs[index].id);
  out.end_array();
  out.key("comparisons").begin_array();
  if (view.description) {
    for (const Comparison &comparison : view.description->comparisons) {
      write_comparison(out, model, view, comparison);
    }
  }
  out.end_array();
  if (full) {
    out.key("report").string(view.report_md);
    out.key("base_commit").string(view.base_commit);
    out.key("metrics_files").begin_array();
    for (size_t index : view.metrics)
      write_metrics_file(out, model.index.metrics[index], false);
    out.end_array();
  }
  out.end_object();
}

void write_run_entry(JsonWriter &out, const Model &model, const Run &run) {
  const RunRole *role = model.role_of(run);
  out.begin_object();
  out.key("slug").string(run_slug(run.id));
  out.key("series").string(run.series);
  out.key("name").string(run_name(run));
  out.key("raw_of").string(run.raw_of);
  out.key("kind").string(role ? role->kind : "");
  out.key("repeat");
  if (role && role->repeat)
    out.integer(*role->repeat);
  else
    out.null();
  out.key("variant").string(role ? role->variant : "");
  out.key("revision").integer(run.summary.revision);
  out.key("duration").number(run.summary.last_t - run.summary.first_t);
  out.key("tests").begin_array();
  for (const std::string &test : run.summary.tests)
    out.string(test);
  out.end_array();
  out.key("steps").integer(int64_t(run.summary.steps));
  out.key("metrics").begin_object();
  for (const char *basis : {"filtered", "raw"}) {
    const MetricsFile *file = first_basis(run_metrics(model, run), basis);
    if (!file || !is_bound(file->status) || !file->doc)
      continue;
    out.key(basis).begin_object();
    out.key("file").string(file->rel);
    out.key("values");
    write_values(out, flatten_metrics(file->doc->root()));
    out.end_object();
  }
  out.end_object();
  out.end_object();
}

} // namespace

void write_shared_pages(const Model &model, const std::string &site_dir,
                        const std::string &web_dir) {
  for (const char *asset : {"viewer.js", "viewer.css", "i18n.js"}) {
    write_output(site_dir + "/assets/" + asset,
                 read_file(web_dir + "/" + asset, size_t(16) << 20));
  }
  // Catalog for the overview and the A/B page.
  JsonWriter catalog(1u << 16);
  catalog.begin_object();
  catalog.key("baseline").string(model.baseline);
  catalog.key("generator").integer(kGeneratorVersion);
  catalog.key("parser").integer(kParserVersion);
  catalog.key("iterations").begin_array();
  for (const IterationView &view : model.iterations)
    write_iteration(catalog, model, view, false);
  catalog.end_array();
  catalog.key("runs").begin_object();
  for (const Run &run : model.index.runs) {
    catalog.key(run.id);
    write_run_entry(catalog, model, run);
  }
  catalog.end_object();
  catalog.key("skipped").integer(int64_t(model.index.skipped.size()));
  catalog.end_object();
  write_output(site_dir + "/data/catalog.js",
               js_put("", "catalog", catalog.finish()));

  write_output(site_dir + "/index.html",
               page("VP37 measurements", "", "overview",
                    "{\"kind\":\"overview\"}", {"data/catalog.js"}));
  write_output(site_dir + "/compare.html",
               page("A/B comparison", "", "compare", "{\"kind\":\"compare\"}",
                    {"data/catalog.js"}));

  std::set<std::string> keep;
  for (const IterationView &view : model.iterations) {
    JsonWriter out(1u << 16);
    out.begin_object();
    out.key("kind").string("iteration");
    out.key("iteration");
    write_iteration(out, model, view, true);
    out.key("runs").begin_object();
    for (size_t index : view.runs) {
      const Run &run = model.index.runs[index];
      out.key(run.id);
      write_run_entry(out, model, run);
    }
    out.end_object();
    out.end_object();
    const std::string name = view.slug + ".html";
    keep.insert(name);
    write_output(site_dir + "/it/" + name, page(base_name(view.series), "../",
                                                "iteration", out.finish(), {}));
  }
  remove_stale(site_dir + "/it", keep);
}

} // namespace vp37
