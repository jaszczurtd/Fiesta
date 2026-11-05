#ifndef VP37V_LIVE_FILE_H
#define VP37V_LIVE_FILE_H

#include "jb/status.h"
#include <cstdint>
#include <memory>
#include <string>

namespace vp37 {
// One read-only regular-file handle. Identity and size come from the handle,
// so a replacement between open and read cannot mix two files.
class LiveFile {
public:
  LiveFile();
  ~LiveFile();
  LiveFile(const LiveFile &) = delete;
  LiveFile &operator=(const LiveFile &) = delete;
  hal_status_t open(const std::string &path);
  hal_status_t read(uint64_t offset, void *data, size_t capacity, size_t &read);
  uint64_t device = 0, identity = 0, size = 0;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace vp37
#endif
