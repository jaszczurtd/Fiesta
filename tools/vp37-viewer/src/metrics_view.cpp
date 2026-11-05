#include "metrics_view.h"

#include <cmath>
#include <set>

namespace vp37 {

namespace {

struct Section {
  const char *name, *label;
  std::vector<const char *> fields;
};

const std::vector<Section> &sections() {
  static const std::vector<Section> list = {
      {"cyclic_profiles",
       "cyclic",
       {"median_abs_err_hz", "p95_abs_err_hz", "max_abs_err_hz"}},
      {"top_steps",
       "top",
       {"early_ringing_error_rms_hz", "late_ringing_error_rms_hz",
        "settled_error_rms_hz"}},
      {"topzero_steps",
       "topzero",
       {"early_ringing_error_rms_hz", "late_ringing_error_rms_hz",
        "settled_error_rms_hz"}}};
  return list;
}

const std::vector<Section> &flat() {
  static const std::vector<Section> list = {
      {"random",
       "random",
       {"median_abs_err_hz", "p95_abs_err_hz", "max_abs_err_hz"}},
      {"control_period_us", "control_period_us", {"median", "p95", "max"}},
      {"feedback_age_us", "feedback_age_us", {"median", "p95", "max"}}};
  return list;
}

void put(MetricValues &values, const std::string &label,
         std::optional<double> value) {
  for (auto &[name, stored] : values) {
    if (name == label) {
      if (value)
        stored = *value;
      return;
    }
  }
  if (value)
    values.emplace_back(label, *value);
}

void erase(MetricValues &values, const std::string &label) {
  for (auto it = values.begin(); it != values.end(); ++it) {
    if (it->first == label) {
      values.erase(it);
      return;
    }
  }
}

} // namespace

MetricValues flatten_metrics(const Json &metrics) {
  MetricValues values;
  for (const Section &section : sections()) {
    for (const Json &entry : metrics[section.name].items()) {
      const std::string key = entry.key();
      // 0 % is the mechanical rest: reported, marked like the gate does.
      const std::string mark = key == "0%" ? " [spoczynek]" : "";
      for (const char *field : section.fields) {
        put(values, std::string(section.label) + " " + key + " " + field + mark,
            entry[field].number());
      }
    }
  }
  for (const Section &section : flat()) {
    const Json entry = metrics[section.name];
    for (const char *field : section.fields) {
      put(values, std::string(section.label) + " " + field,
          entry[field].number());
    }
  }
  for (const Json &hold : metrics["manual_holds"].items()) {
    const std::string key = hold.key();
    const Json stats = hold["tracking_error_hz"];
    for (const char *field : {"mean", "sd", "p95"}) {
      const auto value = stats[field].number();
      put(values, key + " " + field,
          value ? std::optional<double>(std::fabs(*value)) : value);
    }
    // abs(p95(error)) is not p95(abs(error)) for a negative error: the gate
    // replaces it with the absolute error percentile.
    erase(values, key + " p95");
    put(values, key + " p95_abs_error_hz",
        hold["absolute_error_hz"]["p95"].number());
  }
  return values;
}

std::optional<double> metric_value(const MetricValues &values,
                                   const std::string &label) {
  for (const auto &[name, value] : values) {
    if (name == label)
      return value;
  }
  return std::nullopt;
}

std::string gate_status(const std::optional<double> &candidate,
                        const std::vector<std::optional<double>> &references) {
  if (!candidate || references.empty())
    return "missing";
  bool worse = false;
  for (const auto &reference : references) {
    if (!reference)
      return "missing";
    if (*candidate - *reference > 0)
      worse = true;
  }
  return worse ? "worse" : "no_increase";
}

std::optional<double>
change_percent(const std::optional<double> &candidate,
               const std::vector<std::optional<double>> &references) {
  std::optional<double> worst;
  if (!candidate)
    return worst;
  for (const auto &reference : references) {
    if (!reference || *reference == 0)
      continue;
    const double change =
        (*candidate - *reference) / std::fabs(*reference) * 100.0;
    if (!worst || change > *worst)
      worst = change;
  }
  return worst;
}

std::vector<ComparisonRow> rows_from_comparison_file(const Json &comparison) {
  std::vector<ComparisonRow> rows;
  for (const Json &item : comparison["differences"].items()) {
    ComparisonRow row;
    row.metric = item["metric"].str();
    for (const Json &reference : item["references"].items()) {
      row.references.push_back(reference.number());
    }
    row.candidate = item["candidate"].number();
    row.status = item["status"].str("missing");
    row.change_percent = change_percent(row.candidate, row.references);
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<ComparisonRow>
rows_from_gate_comparison(const Json &comparison,
                          const std::vector<std::string> &references) {
  std::vector<ComparisonRow> rows;
  for (const Json &item : comparison["rows"].items()) {
    ComparisonRow row;
    row.metric = item["metric"].str();
    for (const std::string &reference : references)
      row.references.push_back(item[reference].number());
    row.candidate = item["candidate_mean"].number();
    row.status = gate_status(row.candidate, row.references);
    row.change_percent = change_percent(row.candidate, row.references);
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<ComparisonRow>
rows_from_metrics(const MetricValues &candidate,
                  const std::vector<MetricValues> &references) {
  std::vector<std::string> labels;
  std::set<std::string> seen;
  const auto collect = [&](const MetricValues &values) {
    for (const auto &[label, value] : values) {
      if (seen.insert(label).second)
        labels.push_back(label);
    }
  };
  collect(candidate);
  for (const MetricValues &reference : references)
    collect(reference);
  std::vector<ComparisonRow> rows;
  for (const std::string &label : labels) {
    ComparisonRow row;
    row.metric = label;
    row.candidate = metric_value(candidate, label);
    for (const MetricValues &reference : references) {
      row.references.push_back(metric_value(reference, label));
    }
    row.status = gate_status(row.candidate, row.references);
    row.change_percent = change_percent(row.candidate, row.references);
    rows.push_back(std::move(row));
  }
  return rows;
}

} // namespace vp37
