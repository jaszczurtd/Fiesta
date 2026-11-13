#ifndef VP37V_SITE_H
#define VP37V_SITE_H

// Static pages: overview (index.html), one page per iteration, one page per
// run, and the A/B comparison. Pages render in the browser from embedded
// JSON and VP37.put() data files; nothing is fetched.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "run_index.h"
#include "runs_model.h"
#include "telemetry.h"

namespace vp37 {

// Raised whenever page output changes for the same inputs; part of every
// page stamp.
constexpr int kGeneratorVersion = 5;

struct IterationView {
  std::string series, slug, area, date, title;
  std::optional<IterationDescription> description;
  std::string description_error;
  std::string report_md, report_sha, verdict, verdict_section;
  std::string base_commit, base_commit_sha;
  std::vector<size_t> runs;    // indices into Index::runs
  std::vector<size_t> metrics; // metrics files lying in the iteration
};

struct Model {
  Index index;
  std::vector<IterationView> iterations;
  std::map<std::string, size_t> iteration_by_series;
  std::string baseline; // newest vp37-baseline-*.md, e.g. "rev148 (2026-10-18)"

  const IterationView *iteration_of(const Run &run) const;
  // Role of the run in its iteration description; nullptr if not described.
  const RunRole *role_of(const Run &run) const;
};

std::string run_slug(const std::string &id);
std::string iteration_slug(const std::string &series);

// Model from an index: iterations, descriptions, reports, SHA checks of
// described logs (a mismatch unbinds the run's metrics).
void build_model(Model &model, const std::string &descriptions_dir);

// Everything a run page depends on, hashed into its stamp.
std::string run_stamp(const Model &model, const Run &run);
void write_run_pages(const Model &model, const Run &run,
                     const Recording &recording, const std::string &site_dir);
void write_shared_pages(const Model &model, const std::string &site_dir,
                        const std::string &web_dir);

std::string html_escape(const std::string &text);

} // namespace vp37

#endif
