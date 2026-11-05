#ifndef VP37V_LOG_SOURCE_H
#define VP37V_LOG_SOURCE_H

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vp37 {

struct Error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Whole regular file; throws Error naming the path on failure.
std::string read_file(const std::string &path, size_t max_bytes);
// Publishes through jb_file_write_atomic; an identical file is left alone.
// Returns true when the file changed.
bool write_file(const std::string &path, std::string_view data);
// Regenerable output (site/, cache/): written through a temporary file and
// renamed, without the flushes of write_file(), which cost minutes over
// tens of thousands of page data files.
void write_output(const std::string &path, std::string_view data);

// One gzip member (RFC 1952) with CRC-32 and length checks. Trailing data
// after the member (a second member) is rejected rather than ignored.
std::string gunzip(std::string_view data, size_t max_output);

// Text of a collector log, decompressing `.log.gz`.
std::string read_log(const std::string &path);

std::string sha256_hex(std::string_view data);

constexpr size_t kMaxLogBytes = size_t(1) << 30;

} // namespace vp37

#endif
