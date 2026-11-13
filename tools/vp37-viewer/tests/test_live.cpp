// Real loopback traffic and temporary files; no ECU, serial port or firmware.
#include "browser_requests.h"
#include "browser_view.h"
#include "builder.h"
#include "console_capture.h"
#include "hal/core/jh_endian.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/websocket/hal_websocket.h"
#include "hal/system/hal_system.h"
#include "json_io.h"
#include "live_server.h"
#include "log_source.h"
#include "telemetry.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vp37;
namespace fs = std::filesystem;
namespace {
void check(bool ok, const char *expression, int line) {
  if (!ok)
    throw std::runtime_error(std::string(expression) + " at " +
                             std::to_string(line));
}
#define CHECK(x) check((x), #x, __LINE__)

hal_net_endpoint_t local(uint16_t port) {
  hal_net_endpoint_t result{};
  result.family = HAL_NET_AF_INET;
  result.addr_len = HAL_NET_IPV4_ADDR_LEN;
  result.addr[0] = 127;
  result.addr[3] = 1;
  result.port = port;
  return result;
}
struct Client {
  hal_tcp_socket_t socket = nullptr;
  std::string input;
  bool upgraded = false;
  bool closed = false;
  std::vector<std::string> messages;
  ~Client() { hal_tcp_socket_close(socket); }
  void connect(uint16_t port, const std::string &request) {
    CHECK(hal_tcp_socket_open_ex(&socket) == HAL_OK);
    const auto address = local(port);
    CHECK(hal_tcp_socket_connect_ex(socket, &address, 1000) == HAL_OK);
    CHECK(hal_tcp_socket_send(socket, request.data(), request.size()) ==
          static_cast<int>(request.size()));
  }
  void receive(bool websocket = true) {
    if (closed)
      return;
    char buffer[4096];
    for (;;) {
      size_t got = 0;
      const auto status =
          hal_tcp_socket_recv_ex(socket, buffer, sizeof buffer, 0, &got);
      if (status == HAL_EAGAIN || status == HAL_ETIMEOUT)
        break;
      if ((status == HAL_OK && !got) || (!websocket && status == HAL_EIO)) {
        closed = true;
        break;
      }
      if (status != HAL_OK) {
        std::fprintf(stderr, "receive: %s websocket=%d buffered=%zu: %.100s\n",
                     hal_status_to_string(status), int(websocket), input.size(),
                     input.c_str());
        CHECK(status == HAL_OK);
      }
      input.append(buffer, got);
    }
    if (!websocket)
      return;
    if (!upgraded) {
      const auto end = input.find("\r\n\r\n");
      if (end == std::string::npos)
        return;
      CHECK(input.find("101 Switching Protocols") != std::string::npos);
      input.erase(0, end + 4);
      upgraded = true;
    }
    while (input.size() >= 2) {
      const auto *data = reinterpret_cast<const uint8_t *>(input.data());
      size_t length = data[1] & 127u, header = 2;
      CHECK(length != 127); // The 64-bit length marker exceeds our frame limit.
      if (length == 126) {
        if (input.size() < 4)
          return;
        length = jh_load_be16(data + 2);
        header = 4;
      }
      CHECK(length <= 32768);
      if (input.size() < length + header)
        return;
      if ((data[0] & 15u) == 1)
        messages.push_back(input.substr(header, length));
      input.erase(0, header + length);
    }
  }
  void websocket(uint16_t port) {
    connect(port,
            "GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
  }
};
void pump(LiveServer &server, const std::vector<Client *> &clients,
          unsigned cycles = 15) {
  for (unsigned i = 0; i < cycles; ++i) {
    CHECK(server.poll() == HAL_OK);
    for (Client *client : clients)
      client->receive();
    hal_delay_ms(5);
  }
}
void append(const std::string &path, const std::string &text,
            bool truncate = false) {
  std::ofstream output(fs::u8path(path),
                       std::ios::binary |
                           (truncate ? std::ios::trunc : std::ios::app));
  output << text;
  output.close();
  CHECK(bool(output));
}
std::vector<double> values(const Client &client) {
  std::vector<double> result;
  for (const auto &message : client.messages) {
    const auto doc = JsonDoc::parse(message, "websocket");
    for (const Json row : doc->root()["records"].items()) {
      const auto value = row["values"]["adj"].number();
      if (value)
        result.push_back(*value);
    }
  }
  return result;
}
size_t messages_of(const Client &client, const char *type) {
  size_t count = 0;
  for (const auto &message : client.messages)
    count += JsonDoc::parse(message, "websocket")->root()["type"].str() == type;
  return count;
}
void parser_stream() {
  struct Capture {
    size_t count = 0;
    double last = 0;
  } capture;
  TelemetryParser parser(
      [](void *user, const TelemetryEvent &event) {
        auto &out = *static_cast<Capture *>(user);
        if (event.values) {
          CHECK(event.values->rows() == 1);
          CHECK(event.values->keys.size() == 2);
          out.last = event.values->columns[1][0];
        }
        ++out.count;
        return HAL_OK;
      },
      &capture);
  for (unsigned i = 0; i < 10000; ++i) {
    // A new unknown key on each line must not accumulate a schema/history.
    const std::string row = std::to_string(i) +
                            " ECU: VP37 C us:" + std::to_string(i * 1000) +
                            " x" + std::to_string(i) + ":42\n";
    CHECK(parser.feed(row) == HAL_OK);
  }
  CHECK(capture.count == 10000 && capture.last == 42);
  CHECK(parser.feed("10001 ECU: VP37 trace end samples:1024\n") == HAL_OK);
  CHECK(capture.count == 10001);
  TelemetryParser failed([](void *, const TelemetryEvent &) { return HAL_EIO; },
                         nullptr);
  CHECK(failed.feed("1 ECU: VP37 C adj:1\n") == HAL_EIO);
  CHECK(failed.feed("2 ECU: VP37 C adj:2\n") == HAL_EIO);
  std::vector<double> times;
  TelemetryParser clock(
      [](void *user, const TelemetryEvent &event) {
        static_cast<std::vector<double> *>(user)->push_back(event.t);
        return HAL_OK;
      },
      &times);
  CHECK(clock.feed("1 ECU: VP37 C us:100 adj:1\n2 ECU: VP37 C us:200 adj:2\n"
                   "3 ECU: VP37 T us:150 adj:1.5\n") == HAL_OK);
  CHECK(times.size() == 3);
  CHECK(std::abs(times[0] - 1.0) < 1e-9);
  CHECK(std::abs(times[1] - 1.0001) < 1e-9);
  CHECK(std::abs(times[2] - 1.00005) < 1e-9);
  TelemetryParser limited([](void *, const TelemetryEvent &) { return HAL_OK; },
                          nullptr);
  std::string wide = "1 ECU: VP37 CFG";
  for (unsigned i = 0; i < 257; ++i)
    wide += " k" + std::to_string(i) + ":1";
  CHECK(limited.feed(wide + "\n") == HAL_EOVERFLOW);
}

void network() {
  const std::string work = VP37V_LIVE_WORK;
  fs::remove_all(work);
  BuildOptions pages;
  pages.root = std::string(VP37V_FIXTURES) + "/tree";
  pages.site = work + "/pages";
  pages.cache = work + "/cache";
  pages.descriptions = work + "/descriptions";
  pages.web = VP37V_WEB;
  pages.jobs = 1;
  pages.quiet = true;
  CHECK(build_site(pages).run_pages_written > 0);
  // Stand in for a site generated by an older viewer with its own asset API.
  append(pages.site + "/assets/viewer.js", "// saved site asset\n", true);
  const std::string overview_path =
      "/data/" +
      run_slug("vp37_implementation_tests/2026-01-01-fixture-gate/"
               "candidate-x1/candidate-x1") +
      "/overview.js";
  const std::string overview = read_file(pages.site + overview_path, 1u << 24);
  CHECK(overview.size() >
        262144); // The previous limit rejected this real export.
  constexpr size_t file_limit = 524288;
  const std::string largest(file_limit - 1, 'y');
  append(work + "/pages/large.js", std::string(60000, 'x'), true);
  append(work + "/pages/largest.js", largest, true);
  append(work + "/pages/oversized.js", std::string(file_limit, 'x'), true);
  ServeOptions options;
  options.log = work + "/live log.log";
  options.site = work + "/pages";
  options.web = VP37V_WEB;
  std::unique_ptr<LiveServer> server;
  // Find two available ports without relying on a fixed port or skipped test.
  const auto ticks =
      std::chrono::steady_clock::now().time_since_epoch().count();
  for (unsigned i = 0; i < 100; ++i) {
    options.http_port =
        static_cast<uint16_t>(20000 + (uint64_t(ticks) + i * 2) % 10000);
    options.websocket_port = static_cast<uint16_t>(options.http_port + 1);
    server.reset(new LiveServer(options));
    const auto status = server->start();
    if (status == HAL_OK)
      break;
    CHECK(status == HAL_EBUSY);
    server.reset();
  }
  CHECK(bool(server));
  CHECK(vp37v_test_browser_requests(
      options.http_port, options.websocket_port,
      [](void *context) { return static_cast<LiveServer *>(context)->poll(); },
      server.get()));
  Client first, second;
  first.websocket(options.websocket_port);
  second.websocket(options.websocket_port);
  pump(*server, {&first, &second});
  CHECK(messages_of(first, "hello") == 1);
  CHECK(values(first).empty());
  // A 127-byte payload uses marker 126 and a 16-bit length. It is not the
  // marker 127 that introduces a 64-bit length.
  std::string boundary = "{\"type\":\"frame-boundary\",\"padding\":\"";
  boundary.append(127 - boundary.size() - 2, 'x');
  boundary += "\"}";
  size_t sent = 0;
  CHECK(hal_websocket_broadcast_text(boundary.c_str(), &sent) == HAL_OK);
  CHECK(sent == 2);
  pump(*server, {&first, &second});
  CHECK(first.messages.back() == boundary &&
        second.messages.back() == boundary);
  append(options.log,
         "1 ECU: VP37 C us:100 adj:1\n2 ECU: VP37 C us:200 adj:", true);
  pump(*server, {&first, &second});
  CHECK(values(first) == std::vector<double>({1}));
  append(options.log, "2\r\n");
  pump(*server, {&first, &second});
  CHECK(values(first) == std::vector<double>({1, 2}));
  CHECK(values(second) == values(first));
  pump(*server, {&first, &second});
  CHECK(values(first).size() == 2); // EOF is not finish, no duplicate delivery.

  append(options.log, "3 ECU: VP37 C adj:3\n", true);
  pump(*server, {&first, &second});
  CHECK(values(first) == std::vector<double>({1, 2, 3}));
  // Truncate and regrow beyond the previous offset between two polls.
  append(options.log,
         "4 ECU: VP37 C adj:4 comment:longer-than-the-previous-file\n", true);
  pump(*server, {&first, &second});
  CHECK(values(first) == std::vector<double>({1, 2, 3, 4}));
  write_file(options.log, "5 ECU: VP37 C adj:5\n"); // Atomic replacement.
  pump(*server, {&first, &second});
  CHECK(values(first) == std::vector<double>({1, 2, 3, 4, 5}));
  append(options.log, "6 ECU: VP37 C adj:");
  pump(*server, {&first, &second});
  fs::remove(options.log);
  pump(*server, {&first, &second});
  append(options.log, "7 ECU: VP37 C adj:7\n", true);
  pump(*server, {&first, &second});
  CHECK(values(first) == std::vector<double>({1, 2, 3, 4, 5, 7}));
  CHECK(messages_of(first, "reset") == 6);

  // New clients receive a hello, then current traffic; no implicit replay.
  Client resumed, fourth;
  resumed.websocket(options.websocket_port);
  fourth.websocket(options.websocket_port);
  pump(*server, {&first, &second, &resumed, &fourth});
  append(options.log, "8 ECU: VP37 C adj:8\n");
  pump(*server, {&first, &second, &resumed, &fourth});
  CHECK(values(resumed) == std::vector<double>({8}));
  CHECK(values(fourth) == values(resumed));
  CHECK(hal_websocket_client_count() == 4);
  {
    Client excess;
    excess.websocket(options.websocket_port);
    for (unsigned i = 0; i < 10; ++i) {
      CHECK(server->poll() == HAL_OK);
      excess.receive(false);
      hal_delay_ms(5);
    }
    // HAL may reset a rejected connection with its unread request still queued.
    CHECK(excess.closed || excess.input.find("503") != std::string::npos);
    CHECK(excess.input.find("101 Switching Protocols") == std::string::npos);
    CHECK(hal_websocket_client_count() == 4);
  }
  for (const std::string &path : std::vector<std::string>{
           overview_path, "/", "/large.js", "/largest.js", "/oversized.js",
           "/assets/viewer.js", "/api/status", "/../live.log", "invalid"}) {
    Client http;
    http.connect(options.http_port,
                 "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
    for (unsigned i = 0; i < 2500 && !http.closed; ++i) {
      CHECK(server->poll() == HAL_OK);
      http.receive(false);
      hal_delay_ms(2);
    }
    const char *expected = path == "/../live.log" || path == "invalid"
                               ? "400 Bad Request"
                           : path == "/oversized.js" ? "413 Payload Too Large"
                                                     : "200 OK";
    CHECK(http.input.find(expected) != std::string::npos);
    CHECK(http.closed);
    const auto body = http.input.find("\r\n\r\n");
    CHECK(body != std::string::npos);
    if (path == overview_path)
      CHECK(http.input.substr(body + 4) == overview);
    if (path == "/large.js")
      CHECK(http.input.substr(body + 4) == std::string(60000, 'x'));
    if (path == "/largest.js")
      CHECK(http.input.substr(body + 4) == largest);
  }
  // Continuous growth is delivered exactly once to clients that keep reading.
  first.messages.clear();
  for (unsigned i = 0; i < 200; ++i) {
    append(options.log, std::to_string(i + 10) +
                            " ECU: VP37 C adj:" + std::to_string(i) + "\n");
    pump(*server, {&first, &second, &resumed, &fourth}, 1);
  }
  pump(*server, {&first, &second, &resumed, &fourth});
  const auto received = values(first);
  CHECK(received.size() == 200);
  for (size_t i = 0; i < received.size(); ++i)
    CHECK(received[i] == double(i));
  // Stop reading three clients. Exhaust their TCP buffers while keeping the
  // first client drained, then verify the server still delivers new samples.
  std::string pressure;
  for (unsigned i = 0; i < 15; ++i)
    pressure += "500 ECU: VP37 note " + std::string(4000, 'x') + "\n";
  for (unsigned i = 0; i < 512 && hal_websocket_client_count() > 1; ++i) {
    append(options.log, pressure);
    CHECK(server->poll() == HAL_OK);
    first.receive();
    first.messages.clear();
  }
  CHECK(hal_websocket_client_count() == 1);
  append(options.log, "501 ECU: VP37 C adj:999\n");
  pump(*server, {&first});
  CHECK(values(first) == std::vector<double>({999}));
  hal_tcp_socket_close(first.socket);
  first.socket = nullptr;
  pump(*server, {});
  CHECK(hal_websocket_client_count() == 0);
  Client reconnected;
  reconnected.websocket(options.websocket_port);
  pump(*server, {&reconnected});
  CHECK(messages_of(reconnected, "hello") == 1);
  append(options.log, "502 ECU: VP37 C adj:1000\n");
  pump(*server, {&reconnected});
  CHECK(values(reconnected) == std::vector<double>({1000}));
  CHECK(vp37v_test_capture_start(
      options.log.c_str(),
      [](void *context) {
        return static_cast<LiveServer *>(context)->begin_capture();
      },
      [](void *context) { return static_cast<LiveServer *>(context)->poll(); },
      server.get()));
  pump(*server, {&reconnected});
  std::string restart_messages;
  for (const auto &message : reconnected.messages)
    restart_messages += message;
  CHECK(vp37v_test_restart_packet(restart_messages.c_str()));
  // Unterminated input is bounded and fails visibly instead of dropping bytes.
  append(options.log, std::string(65537, 'x'));
  CHECK(server->poll() == HAL_OK);
  CHECK(server->poll() == HAL_EOVERFLOW);
}
} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "monitor")
    return vp37v_monitor_fixture(argc, argv);
  if (argc == 5 && std::string(argv[1]) == "--check-monitor-help")
    return vp37v_test_monitor_help(argv[2], argv[3], argv[4]) ? 0 : 1;
  try {
    if (argc == 3 && std::string(argv[1]) == "--browser-fixture") {
      ServeOptions options;
      options.web = VP37V_WEB;
      options.log = std::string(VP37V_FIXTURES) + "/live.log";
      std::unique_ptr<LiveServer> server;
      for (unsigned i = 0; i < 100; ++i) {
        options.http_port =
            static_cast<uint16_t>(28000 + (hal_millis() % 10000) * 2);
        options.websocket_port = static_cast<uint16_t>(options.http_port + 1);
        server = std::make_unique<LiveServer>(options);
        if (server->start() == HAL_OK)
          break;
        server.reset();
        hal_delay_ms(1);
      }
      CHECK(server != nullptr);
      Client client;
      client.websocket(options.websocket_port);
      pump(*server, {&client});
      std::string messages = "[";
      for (const auto &message : client.messages) {
        if (messages.size() > 1)
          messages += ',';
        messages += message;
      }
      messages += ']';
      return vp37v_browser_fixture(
                 options.http_port,
                 [](void *value) {
                   return static_cast<LiveServer *>(value)->poll();
                 },
                 server.get(), VP37V_WEB, messages.c_str(), argv[2])
                 ? 0
                 : 1;
    }
    CHECK(argc == 1);
    CHECK(vp37v_test_console_capture());
    CHECK(vp37v_test_sample_clock());
    parser_stream();
    network();
    std::puts("live parser, tail and loopback tests passed");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "live test: %s\n", error.what());
    return 1;
  }
}
