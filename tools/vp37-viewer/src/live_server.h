#ifndef VP37V_LIVE_SERVER_H
#define VP37V_LIVE_SERVER_H

#include "hal/core/hal_status.h"
#include <cstdint>
#include <memory>
#include <string>

namespace vp37 {
struct ServeOptions {
  std::string log, web, site;
  uint16_t http_port = 8037, websocket_port = 8038;
};

// Single-threaded, one instance per process (the HAL servers are singletons).
// poll reads at most 64 KiB; it never finishes a line at a temporary EOF.
class LiveServer {
public:
  explicit LiveServer(ServeOptions options);
  ~LiveServer();
  LiveServer(const LiveServer &) = delete;
  LiveServer &operator=(const LiveServer &) = delete;
  hal_status_t start();
  hal_status_t poll();

private:
  struct State;
  std::unique_ptr<State> state_;
};
int serve_command(int argc, char **argv, const std::string &web);
} // namespace vp37
#endif
