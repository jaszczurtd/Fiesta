#include "live_server.h"
#include "hal/network/http/hal_http_files.h"
#include "hal/network/websocket/hal_websocket.h"
#include "hal/system/hal_system.h"
#include "jb/file.h"
#include "json_io.h"
#include "live_file.h"
#include "log_source.h"
#include "telemetry.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

namespace vp37 {
namespace {
namespace fs = std::filesystem;
constexpr size_t kBatchBytes = 32768;
static_assert(kBatchBytes == HAL_WEBSOCKET_MAX_PAYLOAD_SIZE,
              "WebSocket batch limit");
constexpr uint32_t kBatchMs = 40;
volatile std::sig_atomic_t stopped = 0;
void stop_signal(int) { stopped = 1; }

struct FileMount {
  std::string root, path, etag;
  jb_file_data_t bytes{};
  ~FileMount() { jb_file_release(&bytes); }

  static hal_status_t stat(const char *name, hal_http_file_info_t *info,
                           void *user) {
    auto &self = *static_cast<FileMount *>(user);
    jb_file_release(&self.bytes);
    try {
      std::error_code error;
      const std::string resolved =
          fs::weakly_canonical(fs::u8path(name), error).generic_u8string();
      if (error || resolved.compare(0, self.root.size(), self.root) != 0 ||
          resolved.size() <= self.root.size() ||
          resolved[self.root.size()] != '/')
        return HAL_EPERM;
      const auto length = fs::file_size(fs::u8path(resolved), error);
      if (!error && length >= HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE) {
        *info = {};
        info->exists = true;
        info->size = HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE;
        return HAL_OK; // The HAL file handler returns HTTP 413.
      }
      const hal_status_t status =
          jb_file_read(resolved.c_str(),
                       HAL_HTTP_SERVER_RESPONSE_BUFFER_SIZE - 1, &self.bytes);
      if (status != HAL_OK)
        return status;
      self.path = name;
      const std::string_view data(
          self.bytes.length ? reinterpret_cast<const char *>(self.bytes.data)
                            : "",
          self.bytes.length);
      self.etag = "\"" + sha256_hex(data).substr(0, 32) + "\"";
      *info = {};
      info->exists = true;
      info->size = self.bytes.length;
      info->content_type = hal_http_files_content_type_for_path(name);
      info->etag = self.etag.c_str();
      return HAL_OK;
    } catch (const std::bad_alloc &) {
      return HAL_ENOMEM;
    } catch (...) {
      return HAL_EIO;
    }
  }
  static hal_status_t read(const char *name, size_t offset, void *data,
                           size_t capacity, size_t *count, void *user) {
    auto &self = *static_cast<FileMount *>(user);
    *count = 0;
    if (self.path != name || offset > self.bytes.length)
      return HAL_EIO;
    *count = std::min(capacity, self.bytes.length - offset);
    if (*count)
      std::memcpy(data, self.bytes.data + offset, *count);
    return HAL_OK;
  }
  hal_status_t mount(const char *prefix, const std::string &directory) {
    std::error_code error;
    root = fs::canonical(fs::u8path(directory), error).generic_u8string();
    if (error || !fs::is_directory(fs::u8path(root), error))
      return HAL_ENOENT;
    // Keep a separator boundary for every child, including filesystem roots.
    while (!root.empty() && root.back() == '/')
      root.pop_back();
    hal_http_files_config_t config{};
    config.url_prefix = prefix;
    config.fs_root = root.c_str();
    config.stat = stat;
    config.read = read;
    config.user = this;
    return hal_http_files_mount(&config);
  }
};

hal_net_endpoint_t endpoint(uint16_t port) {
  hal_net_endpoint_t result{};
  result.family = HAL_NET_AF_INET;
  result.addr_len = HAL_NET_IPV4_ADDR_LEN;
  result.addr[0] = 127;
  result.addr[3] = 1;
  result.port = port;
  return result;
}
} // namespace

struct LiveServer::State {
  ServeOptions options;
  FileMount assets, pages;
  bool started = false, present = false;
  uint64_t device = 0, identity = 0, offset = 0, epoch = 0, sequence = 0;
  uint32_t last_batch = 0;
  std::array<char, 65536> input{};
  std::array<char, 64> anchor{};
  size_t anchor_size = 0;
  std::string batch;
  std::unique_ptr<TelemetryParser> parser;

  explicit State(ServeOptions value) : options(std::move(value)) {}
  ~State() {
    if (started) {
      hal_websocket_server_stop();
      hal_http_server_stop();
      hal_http_files_clear();
      hal_http_server_clear_routes();
      (void)hal_websocket_server_set_callbacks(nullptr, nullptr);
    }
  }
  void send(hal_websocket_client_t client, const std::string &message) {
    if (hal_websocket_send_text(client, message.c_str()) != HAL_OK)
      (void)hal_websocket_close(client, 1011);
  }
  void broadcast(const std::string &message) {
    for (hal_websocket_client_t i = 0; i < HAL_WEBSOCKET_MAX_CLIENTS; ++i)
      if (hal_websocket_client_is_connected(i))
        send(i, message);
  }
  std::string state_message(const char *type) const {
    JsonWriter json(16);
    json.begin_object()
        .key("type")
        .string(type)
        .key("protocol")
        .integer(1)
        .key("epoch")
        .integer(static_cast<int64_t>(epoch))
        .key("next")
        .integer(static_cast<int64_t>(sequence))
        .key("present")
        .boolean(present)
        .key("ws_port")
        .integer(options.websocket_port);
    return json.end_object().finish();
  }
  void flush() {
    if (batch.empty())
      return;
    const std::string message =
        "{\"type\":\"samples\",\"epoch\":" + std::to_string(epoch) +
        ",\"seq\":" + std::to_string(sequence++) + ",\"records\":[" + batch +
        "]}";
    broadcast(message);
    batch.clear();
    last_batch = hal_millis();
  }
  static hal_status_t record(void *user, const TelemetryEvent &event) {
    auto &self = *static_cast<State *>(user);
    JsonWriter json(512);
    json.begin_object().key("kind").string(event.kind).key("t").number(event.t);
    if (event.values) {
      json.key("values").begin_object();
      for (size_t i = 0; i < event.values->keys.size(); ++i)
        json.key(event.values->keys[i]).number(event.values->columns[i][0]);
      json.end_object();
    } else
      json.key("text").string(event.text);
    const std::string row = json.end_object().finish();
    if (row.size() > kBatchBytes - 256)
      return HAL_EOVERFLOW;
    if (self.batch.size() + row.size() + 1 > kBatchBytes - 256)
      self.flush();
    if (!self.batch.empty())
      self.batch += ',';
    self.batch += row;
    return HAL_OK;
  }
  void reset(bool exists) {
    flush();
    ++epoch;
    present = exists;
    offset = 0;
    anchor_size = 0;
    parser.reset(new TelemetryParser(record, this));
    broadcast(state_message("reset"));
  }
  hal_status_t tail() {
    LiveFile file;
    hal_status_t status = file.open(options.log);
    if (status == HAL_ENOENT) {
      if (present)
        reset(false);
      return HAL_OK;
    }
    if (status != HAL_OK)
      return status;
    bool changed = !present || file.device != device ||
                   file.identity != identity || file.size < offset;
    if (!changed && anchor_size) {
      size_t count = 0;
      status =
          file.read(offset - anchor_size, input.data(), anchor_size, count);
      if (status != HAL_OK)
        return status;
      changed = count != anchor_size ||
                std::memcmp(input.data(), anchor.data(), anchor_size) != 0;
    }
    if (changed) {
      reset(true);
      device = file.device;
      identity = file.identity;
    }
    size_t count = 0;
    status = file.read(offset, input.data(), input.size(), count);
    if (status != HAL_OK)
      return status;
    if (!count)
      return HAL_OK;
    status = parser->feed(std::string_view(input.data(), count));
    if (status != HAL_OK)
      return status;
    offset += count;
    // Save bytes from the actual read, not a second read racing with truncate.
    const size_t keep =
        std::min(anchor_size, anchor.size() - std::min(count, anchor.size()));
    if (keep)
      std::memmove(anchor.data(), anchor.data() + anchor_size - keep, keep);
    const size_t latest = std::min(count, anchor.size());
    std::memcpy(anchor.data() + keep, input.data() + count - latest, latest);
    anchor_size = keep + latest;
    return HAL_OK;
  }
  static void connected(hal_websocket_client_t client, void *user) {
    auto &self = *static_cast<State *>(user);
    try {
      self.send(client, self.state_message("hello"));
    } catch (...) {
      (void)hal_websocket_close(client, 1011);
    }
  }
  static void received(hal_websocket_client_t client,
                       hal_websocket_message_type_t, const uint8_t *, size_t,
                       void *) {
    (void)hal_websocket_close(client, 1008); // Read-only endpoint.
  }
  static hal_status_t status_route(const hal_http_request_t *,
                                   hal_http_response_t *response, void *user) {
    try {
      const std::string text =
          static_cast<State *>(user)->state_message("status");
      hal_status_t status =
          hal_http_response_set_content_type(response, "application/json");
      if (status == HAL_OK)
        status =
            hal_http_response_set_header(response, "Cache-Control", "no-store");
      return status == HAL_OK
                 ? hal_http_response_write_str(response, text.c_str())
                 : status;
    } catch (...) {
      return HAL_ENOMEM;
    }
  }
};

LiveServer::LiveServer(ServeOptions options)
    : state_(new State(std::move(options))) {}
LiveServer::~LiveServer() = default;
hal_status_t LiveServer::start() {
  auto &s = *state_;
  if (s.started || hal_http_server_is_running() ||
      hal_websocket_server_is_running())
    return HAL_EBUSY;
  if (s.options.log.empty() || s.options.http_port == 0 ||
      s.options.websocket_port == 0 ||
      s.options.http_port == s.options.websocket_port)
    return HAL_EINVAL;
  // Own the singleton state from here, including cleanup after partial startup.
  s.started = true;
  hal_http_server_clear_routes();
  hal_http_files_clear();
  hal_status_t status = s.assets.mount("/assets", s.options.web);
  if (status == HAL_OK && !s.options.site.empty())
    status = s.pages.mount("/", s.options.site);
  if (status == HAL_OK)
    status = hal_http_server_route(HAL_HTTP_METHOD_GET, "/api/status",
                                   State::status_route, &s);
  if (status == HAL_OK && s.options.site.empty())
    status = hal_http_server_route(HAL_HTTP_METHOD_GET, "/",
                                   State::status_route, &s);
  hal_websocket_callbacks_t callbacks{};
  callbacks.on_connect = State::connected;
  callbacks.on_message = State::received;
  if (status == HAL_OK)
    status = hal_websocket_server_set_callbacks(&callbacks, &s);
  const auto http = endpoint(s.options.http_port),
             ws = endpoint(s.options.websocket_port);
  if (status == HAL_OK)
    status = hal_http_server_start_ex(&http);
  if (status == HAL_OK)
    status = hal_websocket_server_start_ex(&ws, "/ws");
  s.last_batch = hal_millis();
  return status;
}
hal_status_t LiveServer::poll() {
  auto &s = *state_;
  if (!s.started)
    return HAL_ESTATE;
  hal_http_server_poll();
  hal_websocket_server_poll();
  const hal_status_t status = s.tail();
  if (status == HAL_OK && hal_elapsed_u32(hal_millis(), s.last_batch, kBatchMs))
    s.flush();
  return status;
}

int serve_command(int argc, char **argv, const std::string &web) {
  ServeOptions options;
  options.web = web;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (i + 1 == argc)
      throw Error("serve: missing value for " + arg);
    const std::string value = argv[++i];
    if (arg == "--log")
      options.log = value;
    else if (arg == "--site")
      options.site = value;
    else if (arg == "--port" || arg == "--ws-port") {
      unsigned port = 0;
      const auto parsed =
          std::from_chars(value.data(), value.data() + value.size(), port);
      if (parsed.ec != std::errc() ||
          parsed.ptr != value.data() + value.size() || port == 0 ||
          port > 65535)
        throw Error("serve: invalid port " + value);
      (arg == "--port" ? options.http_port : options.websocket_port) =
          static_cast<uint16_t>(port);
    } else
      throw Error("serve: unknown option " + arg);
  }
  if (options.log.empty())
    throw Error("serve: --log is required");
  LiveServer server(options);
  const hal_status_t status = server.start();
  if (status != HAL_OK)
    throw Error(std::string("serve: startup: ") + hal_status_to_string(status));
  stopped = 0;
  const auto previous_int = std::signal(SIGINT, stop_signal);
  const auto previous_term = std::signal(SIGTERM, stop_signal);
  std::printf("HTTP http://127.0.0.1:%u/ WebSocket ws://127.0.0.1:%u/ws\n",
              unsigned(options.http_port), unsigned(options.websocket_port));
  std::fflush(stdout);
  hal_status_t result = HAL_OK;
  while (!stopped && result == HAL_OK) {
    result = server.poll();
    hal_delay_ms(5);
  }
  (void)std::signal(SIGINT, previous_int);
  (void)std::signal(SIGTERM, previous_term);
  if (result != HAL_OK)
    throw Error(std::string("serve: log: ") + hal_status_to_string(result));
  return 0;
}
} // namespace vp37
