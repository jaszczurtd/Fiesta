#include "live_file.h"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vp37 {
struct LiveFile::State {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~State() {
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
  }
#else
  int fd = -1;
  ~State() {
    if (fd >= 0)
      close(fd);
  }
#endif
};
LiveFile::LiveFile() : state_(new State) {}
LiveFile::~LiveFile() = default;

hal_status_t LiveFile::open(const std::string &path) {
#ifdef _WIN32
  if (state_->handle != INVALID_HANDLE_VALUE)
    return HAL_ESTATE;
  state_->handle =
      CreateFileW(std::filesystem::u8path(path).c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (state_->handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
               ? HAL_ENOENT
               : HAL_EIO;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (GetFileType(state_->handle) != FILE_TYPE_DISK ||
      !GetFileInformationByHandle(state_->handle, &info))
    return HAL_EIO;
  if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    return HAL_ETYPE;
  device = info.dwVolumeSerialNumber;
  identity = (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
  size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
#else
  if (state_->fd >= 0)
    return HAL_ESTATE;
  state_->fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (state_->fd < 0)
    return errno == ENOENT ? HAL_ENOENT : HAL_EIO;
  struct stat info {};
  if (fstat(state_->fd, &info) != 0)
    return HAL_EIO;
  if (!S_ISREG(info.st_mode) || info.st_size < 0)
    return HAL_ETYPE;
  device = static_cast<uint64_t>(info.st_dev);
  identity = static_cast<uint64_t>(info.st_ino);
  size = static_cast<uint64_t>(info.st_size);
#endif
  return HAL_OK;
}

hal_status_t LiveFile::read(uint64_t offset, void *data, size_t capacity,
                            size_t &count) {
  count = 0;
  if (offset > uint64_t(std::numeric_limits<int64_t>::max()))
    return HAL_EOVERFLOW;
#ifdef _WIN32
  LARGE_INTEGER at{};
  at.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(state_->handle, at, nullptr, FILE_BEGIN))
    return HAL_EIO;
  DWORD got = 0;
  const DWORD length = static_cast<DWORD>(std::min(capacity, size_t(MAXDWORD)));
  if (!ReadFile(state_->handle, data, length, &got, nullptr))
    return HAL_EIO;
  count = got;
#else
  static_assert(sizeof(off_t) >= sizeof(int64_t),
                "64-bit file offsets required");
  ssize_t got;
  do {
    got = pread(state_->fd, data, capacity, static_cast<off_t>(offset));
  } while (got < 0 && errno == EINTR);
  if (got < 0)
    return HAL_EIO;
  count = static_cast<size_t>(got);
#endif
  return HAL_OK;
}
} // namespace vp37
