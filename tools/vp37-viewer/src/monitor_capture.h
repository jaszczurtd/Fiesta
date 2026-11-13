#ifndef VP37V_MONITOR_CAPTURE_H
#define VP37V_MONITOR_CAPTURE_H

#include "live_server.h"
#include <csignal>

namespace vp37 {
// Own the monitor child, append its output and poll the server on this thread.
// Returns HAL_ECANCELED for a requested stop; child failures are errors.
hal_status_t capture_monitor(LiveServer &server, const std::string &project,
                             const std::string &log, uint16_t http_port,
                             const volatile std::sig_atomic_t *stopped);
} // namespace vp37
#endif
