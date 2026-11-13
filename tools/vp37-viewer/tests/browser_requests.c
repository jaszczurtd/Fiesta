/* Browser-sized requests over real loopback sockets; independent of firmware.
 */
#include "browser_requests.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/websocket/hal_websocket.h"
#include "hal/system/hal_system.h"
#include <stdio.h>
#include <string.h>

#define BROWSER_AGENT                                                          \
  "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "            \
  "(KHTML, like Gecko) Code/1.144.1 Chrome/150.0.7871.250 "                    \
  "Electron/43.8.3 Safari/537.36\r\n"
#define BROWSER_LANG                                                           \
  "Accept-Encoding: gzip, deflate, br, zstd\r\nAccept-Language: pl\r\n"

/* Complete header sets used by the embedded browser, with a fixed sample key.
 */
static const char page_request[] =
    "GET / HTTP/1.1\r\nHost: 127.0.0.1:18037\r\n"
    "Connection: keep-alive\r\nCache-Control: max-age=0\r\n"
    "Upgrade-Insecure-Requests: 1\r\n" BROWSER_AGENT
    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,"
    "image/avif,image/webp,image/apng,*/*;q=0.8,"
    "application/signed-exchange;v=b3;q=0.7\r\n"
    "Sec-Fetch-Site: none\r\nSec-Fetch-Mode: navigate\r\n"
    "Sec-Fetch-User: ?1\r\nSec-Fetch-Dest: document\r\n" BROWSER_LANG
    "If-None-Match: \"not-the-current-page\"\r\n\r\n";
static const char status_request[] =
    "GET /api/status HTTP/1.1\r\nHost: 127.0.0.1:18037\r\n"
    "Connection: keep-alive\r\nsec-ch-ua-platform: \"Linux\"\r\n" BROWSER_AGENT
    "sec-ch-ua: \"Not;A=Brand\";v=\"8\", \"Chromium\";v=\"150\"\r\n"
    "sec-ch-ua-mobile: ?0\r\nAccept: */*\r\n"
    "Sec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: cors\r\n"
    "Sec-Fetch-Dest: empty\r\nReferer: http://127.0.0.1:18037/\r\n" BROWSER_LANG
    "\r\n";
static const char websocket_request[] =
    "GET /ws HTTP/1.1\r\nHost: 127.0.0.1:18038\r\nConnection: Upgrade\r\n"
    "Pragma: no-cache\r\nCache-Control: no-cache\r\n" BROWSER_AGENT
    "Upgrade: websocket\r\nOrigin: http://127.0.0.1:18037\r\n"
    "Sec-WebSocket-Version: 13\r\n" BROWSER_LANG
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; "
    "client_max_window_bits\r\n\r\n";

static bool exchange(uint16_t port, const char *request, size_t length,
                     bool fragmented, const char *expected, bool upgrade,
                     hal_status_t (*poll_server)(void *), void *server) {
  static char response[4096];
  hal_tcp_socket_t socket = NULL;
  hal_net_endpoint_t address = {0};
  address.family = HAL_NET_AF_INET;
  address.addr_len = HAL_NET_IPV4_ADDR_LEN;
  address.addr[0] = 127;
  address.addr[3] = 1;
  address.port = port;
  size_t used = 0;
  bool passed = false;
  response[0] = '\0';
  do {
    if (hal_tcp_socket_open_ex(&socket) != HAL_OK ||
        hal_tcp_socket_connect_ex(socket, &address, 1000) != HAL_OK)
      break;
    size_t prefix = fragmented ? 510u : 0u;
    if (prefix) {
      if (hal_tcp_socket_send(socket, request, prefix) != (int)prefix ||
          poll_server(server) != HAL_OK)
        break;
      size_t got = 0;
      hal_status_t status = hal_tcp_socket_recv_ex(
          socket, response, sizeof(response) - 1, 0, &got);
      if (got || (status != HAL_EAGAIN && status != HAL_ETIMEOUT))
        break; /* An incomplete request must not produce a response. */
    }
    if (hal_tcp_socket_send(socket, request + prefix, length - prefix) !=
        (int)(length - prefix))
      break;
    for (unsigned i = 0; i < 1000 && used < sizeof(response) - 1; ++i) {
      if (poll_server(server) != HAL_OK)
        break;
      size_t got = 0;
      hal_status_t status = hal_tcp_socket_recv_ex(
          socket, response + used, sizeof(response) - used - 1, 0, &got);
      used += got;
      response[used] = '\0';
      if (strstr(response, "\r\n\r\n") &&
          (strncmp(expected, "HTTP/", 5) == 0 || strstr(response, expected)) &&
          (!upgrade || strstr(response, "\"type\":\"hello\""))) {
        passed = strncmp(response, expected, strlen(expected)) == 0 ||
                 (strncmp(response, "HTTP/1.1 200 OK", 15) == 0 &&
                  strstr(response, expected));
        if (upgrade)
          passed = passed &&
                   strstr(response, "Sec-WebSocket-Accept: "
                                    "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != NULL &&
                   hal_websocket_client_count() == 1;
        break;
      }
      if ((status == HAL_OK && !got) ||
          (status != HAL_OK && status != HAL_EAGAIN &&
           status != HAL_ETIMEOUT)) {
        /* Closing with unread request bytes can reset TCP before 413 arrives.
         * At the exact 4095-byte boundary the response remains mandatory. */
        passed = used == 0 && length > 4095 &&
                 strcmp(expected, "HTTP/1.1 413 Payload Too Large") == 0 &&
                 (status == HAL_EIO || status == HAL_OK);
        break;
      }
      hal_delay_ms(2);
    }
  } while (false);
  hal_tcp_socket_close(socket);
  /* EOF must release the slot, including after an oversized handshake. */
  for (unsigned i = 0; i < 20; ++i) {
    if (poll_server(server) != HAL_OK)
      passed = false;
    hal_delay_ms(2);
  }
  passed = passed && hal_websocket_client_count() == 0;
  if (!passed)
    fprintf(stderr,
            "browser request: port=%u bytes=%zu fragmented=%d, "
            "expected %s, received %.100s\n",
            (unsigned)port, length, (int)fragmented, expected, response);
  return passed;
}

bool vp37v_test_browser_requests(uint16_t http_port, uint16_t websocket_port,
                                 hal_status_t (*poll_server)(void *),
                                 void *server) {
  static char padded[8193];
  const char *requests[] = {page_request, status_request, websocket_request};
  bool passed = true;
  const char *asset_requests[] = {
      "GET /assets/viewer.js HTTP/1.1\r\nHost: localhost\r\n\r\n",
      "GET /live-assets/live.js HTTP/1.1\r\nHost: localhost\r\n\r\n"};
  const char *asset_contents[] = {"saved site asset", "Live adapter"};
  for (size_t i = 0; i < COUNTOF(asset_requests); ++i)
    if (!exchange(http_port, asset_requests[i], strlen(asset_requests[i]),
                  false, asset_contents[i], false, poll_server, server))
      passed = false;
  for (size_t kind = 0; kind < COUNTOF(requests); ++kind) {
    const bool websocket = kind == 2;
    const uint16_t port = websocket ? websocket_port : http_port;
    const char *accepted =
        websocket ? "HTTP/1.1 101 Switching Protocols" : "HTTP/1.1 200 OK";
    const char *request = requests[kind];
    const size_t length = strlen(request);
    /* Test each listener even if the other's configuration regresses. */
    for (unsigned fragmented = 0; fragmented < 2; ++fragmented)
      if (!exchange(port, request, length, fragmented != 0, accepted, websocket,
                    poll_server, server))
        passed = false;
    const size_t sizes[] = {4094, 4095, 4096, 8192};
    for (size_t i = 0; i < COUNTOF(sizes); ++i) {
      const size_t total = sizes[i];
      const char padding_header[] = "X-Padding: ";
      const size_t prefix = length - 2;
      memcpy(padded, request, prefix);
      memcpy(padded + prefix, padding_header, sizeof(padding_header) - 1);
      const size_t start = prefix + sizeof(padding_header) - 1;
      memset(padded + start, 'x', total - start - 4);
      memcpy(padded + total - 4, "\r\n\r\n", 5);
      /* HAL reserves a terminator and rejects a full request buffer. */
      const bool fits = total == 4094;
      if (!exchange(port, padded, total, false,
                    fits ? accepted : "HTTP/1.1 413 Payload Too Large",
                    fits && websocket, poll_server, server))
        passed = false;
    }
    /* Rejected requests must not prevent the next browser from connecting. */
    if (!exchange(port, request, length, false, accepted, websocket,
                  poll_server, server))
      passed = false;
  }
  const char *live_paths[] = {"/live", "/api/fields", "/live-assets/live.js"};
  for (size_t i = 0; i < COUNTOF(live_paths); ++i) {
    const int length =
        snprintf(padded, sizeof(padded),
                 "GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n", live_paths[i]);
    if (length <= 0 || (size_t)length >= sizeof(padded) ||
        !exchange(http_port, padded, (size_t)length, false, "HTTP/1.1 200 OK",
                  false, poll_server, server))
      passed = false;
  }
  return passed;
}

bool vp37v_test_capture_start(const char *path,
                              hal_status_t (*begin_capture)(void *),
                              hal_status_t (*poll_server)(void *),
                              void *server) {
  FILE *file = NULL;
#ifdef _WIN32
  if (fopen_s(&file, path, "wb") != 0)
    return false;
#else
  file = fopen(path, "wb");
#endif
  if (!file)
    return false;
  /* Old data must not even reach the parser. Finite without the line limit. */
  for (unsigned i = 0; i < 70000; ++i)
    fputc('x', file);
  fputs("\nold partial record", file);
  bool ok = !ferror(file);
  ok = fclose(file) == 0 && ok;
  if (!ok || begin_capture(server) != HAL_OK)
    return false;
#ifdef _WIN32
  if (fopen_s(&file, path, "ab") != 0)
    return false;
#else
  file = fopen(path, "ab");
#endif
  if (!file)
    return false;
  fputs("\n1000 ECU: VP37 C us:2400000000 adj:7001\n"
        "1001 ECU: VP37 C us:3000000 adj:7002\n"
        "1001 ECU: VP37 ADJ d:7002\n",
        file);
  ok = !ferror(file);
  ok = fclose(file) == 0 && ok;
  for (unsigned i = 0; i < 20 && ok; ++i) {
    ok = poll_server(server) == HAL_OK;
    hal_delay_ms(5);
  }
  if (!ok)
    fprintf(stderr, "capture start replayed old file contents\n");
  return ok;
}

bool vp37v_test_restart_packet(const char *messages) {
  const bool ok =
      strstr(messages, "\"kind\":\"C\",\"t\":1001,\"restart\":true") &&
      strstr(messages, "\"kind\":\"ADJ\",\"t\":1001,");
  if (!ok)
    fprintf(stderr, "MCU restart did not align control and ADJ times\n");
  return ok;
}
