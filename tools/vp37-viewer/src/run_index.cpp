#include "run_index.h"

#include <cmath>
#include <filesystem>

namespace vp37 {

namespace fs = std::filesystem;

namespace {

constexpr double kWindowTolerance = 0.001;

bool starts_with(const std::string &text, const std::string &prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string &text, const std::string &suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string file_name(const std::string &rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

std::string parent_of(const std::string &rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? std::string() : rel.substr(0, slash);
}

bool marker_at(const RunSummary &summary, const std::string &marker, double t) {
  for (const auto &[text, at] : summary.host_marks) {
    if (text == marker && std::fabs(at - t) <= kWindowTolerance)
      return true;
  }
  return false;
}

} // namespace

const std::vector<std::string> &data_areas() {
  static const std::vector<std::string> areas = {
      "vp37_implementation_tests", "measurements", "archive/measurements"};
  return areas;
}

const char *binding_status_text(BindingStatus status) {
  switch (status) {
  case BindingStatus::bound:
    return "przypisane";
  case BindingStatus::bound_no_window:
    return "przypisane, plik nie zapisał okna analizy";
  case BindingStatus::no_source:
    return "nieprzypisane: plik nie ma pola source";
  case BindingStatus::source_missing:
    return "nieprzypisane: log ze źródła nie istnieje";
  case BindingStatus::not_a_run:
    return "nieprzypisane: źródło nie jest logiem przebiegu VP37";
  case BindingStatus::window_mismatch:
    return "nieprzypisane: okno analizy nie pasuje do logu";
  case BindingStatus::sha_mismatch:
    return "nieprzypisane: log różni się od opisanego w runs.json";
  case BindingStatus::unreadable:
    return "nieczytelny plik metryk";
  }
  return "?";
}

bool is_bound(BindingStatus status) {
  return status == BindingStatus::bound ||
         status == BindingStatus::bound_no_window;
}

const Run *Index::find(const std::string &id) const {
  const auto found = run_by_id.find(id);
  return found == run_by_id.end() ? nullptr : &runs[found->second];
}

std::string relative_to(const std::string &root, const std::string &path) {
  const std::string prefix = root + "/";
  return starts_with(path, prefix) ? path.substr(prefix.size()) : path;
}

std::string series_of(const std::string &rel) {
  for (const std::string &area : data_areas()) {
    const std::string prefix = area + "/";
    if (!starts_with(rel, prefix))
      continue;
    // archive/measurements is a longer prefix of nothing else, measurements
    // must not claim archive paths: the prefix check above is exact.
    const std::string rest = rel.substr(prefix.size());
    const size_t slash = rest.find('/');
    return slash == std::string::npos ? area : prefix + rest.substr(0, slash);
  }
  return parent_of(rel);
}

std::vector<std::string> source_candidates(const std::string &root,
                                           const std::string &metrics_rel,
                                           const std::string &source) {
  std::vector<std::string> bases; // absolute paths before the .gz variant
  const auto add_tail = [&](std::string tail) {
    // A series may have moved into the archive since its metrics were written.
    std::vector<std::string> tails = {tail};
    for (size_t i = 0, n = tails.size(); i < n; ++i) {
      if (starts_with(tails[i], "measurements/"))
        tails.push_back("archive/" + tails[i]);
    }
    for (const std::string &t : tails)
      bases.push_back(root + "/" + t);
  };
  // Prefer the caller's tree. Match a complete data-area component, including
  // archive/measurements before its shorter measurements suffix.
  bool relocated = false;
  for (auto area = data_areas().rbegin(); area != data_areas().rend(); ++area) {
    const std::string marker = *area + "/";
    size_t at = source.find(marker);
    while (at != std::string::npos && at != 0 && source[at - 1] != '/') {
      at = source.find(marker, at + 1);
    }
    if (at != std::string::npos) {
      add_tail(source.substr(at));
      relocated = true;
      break;
    }
  }
  if ((!source.empty() && source.front() == '/') ||
      (source.size() >= 3 && source[1] == ':' && source[2] == '/')) {
    bases.push_back(source);
  } else if (!relocated) {
    // Relative to the metrics file directory or to one of its parents (the
    // analyzer ran in the iteration directory).
    std::string dir = parent_of(metrics_rel);
    while (true) {
      bases.push_back(root + "/" + (dir.empty() ? source : dir + "/" + source));
      if (dir.empty())
        break;
      dir = parent_of(dir);
    }
  }
  std::vector<std::string> out;
  for (const std::string &base : bases) {
    out.push_back(base);
    if (!ends_with(base, ".gz"))
      out.push_back(base + ".gz");
  }
  return out;
}

std::vector<std::string> moved_run_candidates(const std::string &root,
                                              const std::string &metrics_rel,
                                              const std::string &source) {
  std::vector<std::string> out;
  const size_t last = source.rfind('/');
  if (last == std::string::npos || last == 0)
    return out;
  const size_t before = source.rfind('/', last - 1);
  const std::string tail =
      source.substr(before == std::string::npos ? 0 : before + 1);
  std::string dir = parent_of(metrics_rel);
  while (!dir.empty()) {
    const std::string base = root + "/" + dir + "/" + tail;
    out.push_back(base);
    if (!ends_with(base, ".gz"))
      out.push_back(base + ".gz");
    dir = parent_of(dir);
  }
  return out;
}

std::optional<Window> read_window(const Json &metrics) {
  const Json node = metrics["analysis_window"];
  if (!node.is_object())
    return std::nullopt;
  Window window;
  window.kind = node["kind"].str();
  window.start_s = node["start_s"].number().value_or(NAN);
  window.end_s = node["end_s"].number().value_or(NAN);
  window.start_marker = node["start_marker"].str();
  window.end_marker = node["end_marker"].str();
  return window;
}

bool window_matches(const Window &window, const RunSummary &summary,
                    std::string &reason) {
  if (window.kind == "whole_log") {
    // Older analyzers wrote null times for the whole log.
    const bool open = std::isnan(window.start_s) && std::isnan(window.end_s);
    if (open ||
        (std::fabs(window.start_s - summary.first_t) <= kWindowTolerance &&
         std::fabs(window.end_s - summary.last_t) <= kWindowTolerance)) {
      return true;
    }
    reason = "okno whole_log ma inne granice niż log";
    return false;
  }
  if (window.kind == "gate_markers") {
    if (!marker_at(summary, window.start_marker, window.start_s)) {
      reason = "brak '" + window.start_marker + "' w chwili " +
               std::to_string(window.start_s);
      return false;
    }
    if (!marker_at(summary, window.end_marker, window.end_s)) {
      reason = "brak '" + window.end_marker + "' w chwili " +
               std::to_string(window.end_s);
      return false;
    }
    return true;
  }
  reason = "nieznany rodzaj okna '" + window.kind + "'";
  return false;
}

void bind_metrics(Index &index) {
  for (Run &run : index.runs)
    run.metrics.clear();
  for (size_t i = 0; i < index.metrics.size(); ++i) {
    MetricsFile &file = index.metrics[i];
    file.run_id.clear();
    file.resolved_rel.clear();
    file.reason.clear();
    if (!file.doc) {
      file.status = BindingStatus::unreadable;
      continue;
    }
    const Json root = file.doc->root();
    file.source = root["source"].str();
    file.window = read_window(root);
    if (file.source.empty()) {
      file.status = BindingStatus::no_source;
      continue;
    }
    std::string found;
    bool moved = false;
    for (const auto &list :
         {source_candidates(index.root, file.rel, file.source),
          moved_run_candidates(index.root, file.rel, file.source)}) {
      for (const std::string &candidate : list) {
        std::error_code error;
        if (fs::is_regular_file(candidate, error)) {
          found = candidate;
          break;
        }
      }
      if (!found.empty())
        break;
      moved = true;
    }
    if (found.empty()) {
      file.status = BindingStatus::source_missing;
      file.reason = file.source;
      continue;
    }
    file.resolved_rel = relative_to(
        index.root, fs::path(found).lexically_normal().generic_string());
    const auto run_at = index.run_by_file.find(file.resolved_rel);
    if (run_at == index.run_by_file.end()) {
      file.status = BindingStatus::not_a_run;
      file.reason = file.resolved_rel;
      continue;
    }
    Run &run = index.runs[run_at->second];
    if (file.window) {
      if (!window_matches(*file.window, run.summary, file.reason)) {
        file.status = BindingStatus::window_mismatch;
        continue;
      }
      file.status = BindingStatus::bound;
    } else {
      file.status = BindingStatus::bound_no_window;
    }
    if (moved)
      file.reason = "katalog przebiegu przeniesiony: log znaleziony jako " +
                    file.resolved_rel;
    file.run_id = run.id;
    file.basis = (!run.raw_of.empty() ||
                  file_name(file.rel).find("raw") != std::string::npos)
                     ? "raw"
                     : "filtered";
    run.metrics.push_back(i);
  }
}

} // namespace vp37
