#ifndef VP37V_RECORDING_CACHE_H
#define VP37V_RECORDING_CACHE_H

// Parsed recordings on disk, one file per log content and parser version:
// <cache>/rec/<sha[0:2]>/<sha>-p<kParserVersion>.bin. A parser change gives
// new names, so stale entries are never read.

#include <optional>
#include <string>

#include "telemetry.h"

namespace vp37 {

std::string recording_cache_path(const std::string &cache_dir,
                                 const std::string &log_sha);
std::string serialize_recording(const Recording &recording);
// nullopt for a missing, foreign or damaged file.
std::optional<Recording> deserialize_recording(const std::string &bytes);

} // namespace vp37

#endif
