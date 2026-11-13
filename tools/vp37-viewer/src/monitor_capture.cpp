#include "monitor_capture.h"
#include "console_stream.h"
#include "jb/file.h"
#include "jb/process.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

namespace vp37 {
namespace {
namespace fs = std::filesystem;
struct Capture {
  LiveServer &server;
  uint16_t http_port;
  const volatile std::sig_atomic_t *stopped;
  std::ofstream log;
  jb_file_lock_t *lock = nullptr;
  vp37_console_stream_t console{};
  // Reserve room for the timestamp/prefix inside the parser's 64 KiB limit.
  std::array<char, 65536 - 128> line{};
  hal_status_t status = HAL_OK;
  bool connected_header = false;

  Capture(LiveServer &value, uint16_t port,
          const volatile std::sig_atomic_t *stop)
      : server(value), http_port(port), stopped(stop) {}
  ~Capture() { (void)jb_file_lock_release(&lock); }

  static hal_status_t record(void *value, const char *data, size_t size) {
    auto &self = *static_cast<Capture *>(value);
    if (size == 0)
      return HAL_OK;
    try {
      const std::string_view text(data, size);
      const bool show_help = self.connected_header && text.size() == 80 &&
                             text.find_first_not_of('-') == text.npos;
      self.connected_header = text.substr(0, 13) == "Connected to ";
      const bool labeled = text.substr(0, 4) == "ECU:";
      const bool telemetry = labeled || text.substr(0, 5) == "VP37 " ||
                             text.substr(0, 6) == "TEST: ";
      const double now =
          std::chrono::duration<double>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
      char prefix[96];
      const int count =
          std::snprintf(prefix, sizeof prefix, "%.6f %s", now,
                        labeled ? "" : (telemetry ? "ECU: " : "HOST MONITOR "));
      if (count < 0 || static_cast<size_t>(count) >= sizeof prefix)
        return HAL_EOVERFLOW;
      self.log.write(prefix, count);
      self.log.write(data, static_cast<std::streamsize>(size));
      self.log.put('\n');
      self.log.flush();
      if (!self.log)
        return HAL_EIO;
      // Connection/upload diagnostics stay visible without flooding the
      // terminal.
      if (!telemetry) {
        if (std::fwrite(data, 1, size, stdout) != size ||
            std::fputc('\n', stdout) == EOF)
          return HAL_EIO;
        if (show_help &&
            std::printf(
                "\nVP37 live viewer is running. Keep this task open.\n"
                "URL: http://127.0.0.1:%u/live\n"
                "Chrome: open the URL above.\n"
                "VS Code: Ctrl+Shift+P -> Open Integrated Browser, then paste "
                "the URL.\n"
                "Next start: Tasks: Run Task -> VP37: Live viewer "
                "(builds/uploads ECU BENCH and starts capture).\n\n",
                unsigned(self.http_port)) < 0)
          return HAL_EIO;
        if (std::fflush(stdout) != 0)
          return HAL_EIO;
      }
      return HAL_OK;
    } catch (...) {
      return HAL_EIO;
    }
  }

  static hal_status_t output(void *value, jb_process_stream_t stream,
                             const unsigned char *data, size_t size) {
    auto &self = *static_cast<Capture *>(value);
    if (stream == JB_PROCESS_STDERR)
      return std::fwrite(data, 1, size, stderr) == size ? HAL_OK : HAL_EIO;
    return vp37_console_stream_feed(&self.console, data, size);
  }

  static bool canceled(void *value) {
    auto &self = *static_cast<Capture *>(value);
    if (*self.stopped != 0)
      return true;
    try {
      self.status = self.server.poll();
    } catch (...) {
      self.status = HAL_EIO;
    }
    return self.status != HAL_OK;
  }
};
} // namespace

hal_status_t capture_monitor(LiveServer &server, const std::string &project,
                             const std::string &log, uint16_t http_port,
                             const volatile std::sig_atomic_t *stopped) {
  auto capture = std::make_unique<Capture>(server, http_port, stopped);
  const auto path = fs::absolute(fs::u8path(log));
  fs::create_directories(path.parent_path());
  const auto lock_path = path.generic_u8string() + ".capture.lock";
  auto status = jb_file_lock_acquire(lock_path.c_str(), 0, nullptr, nullptr,
                                     &capture->lock);
  if (status != HAL_OK)
    return status;
  capture->log.open(path, std::ios::binary | std::ios::app);
  if (!capture->log)
    return HAL_EIO;
  status = server.begin_capture();
  if (status != HAL_OK)
    return status;
  capture->log.put('\n');
  constexpr char start[] = "Live capture started";
  status = Capture::record(capture.get(), start, sizeof start - 1);
  if (status != HAL_OK)
    return status;
  status = vp37_console_stream_init(&capture->console, capture->line.data(),
                                    capture->line.size(), Capture::record,
                                    capture.get());
  if (status != HAL_OK)
    return status;
  const auto project_arg =
      "-DPROJECT=" + fs::absolute(fs::u8path(project)).generic_u8string();
  const char *arguments[] = {project_arg.c_str(), "-P", VP37V_MONITOR_SCRIPT,
                             nullptr};
  jb_process_options_t options{};
  options.executable = VP37V_CMAKE;
  options.arguments = arguments;
  const auto cwd = fs::absolute(fs::u8path(project)).generic_u8string();
  options.working_directory = cwd.c_str();
  options.output = Capture::output;
  options.canceled = Capture::canceled;
  options.context = capture.get();
  jb_process_result_t result{};
  status = jb_process_run(&options, &result);
  capture->log.close();
  if (!capture->log)
    return HAL_EIO;
  const auto released = jb_file_lock_release(&capture->lock);
  if (released != HAL_OK)
    return released;
  if (capture->status != HAL_OK)
    return capture->status;
  if (status != HAL_OK)
    return status;
  if (!result.exited || result.exit_code != 0 || result.term_signal != 0) {
    std::fprintf(stderr, "Monitor exited with code %u, signal %d\n",
                 result.exit_code, result.term_signal);
    return HAL_EIO;
  }
  std::puts("Monitor stopped; live session ended.");
  return HAL_OK;
}
} // namespace vp37
