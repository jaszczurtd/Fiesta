#ifndef VP37V_BROWSER_REQUESTS_H
#define VP37V_BROWSER_REQUESTS_H

#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool vp37v_test_browser_requests(uint16_t http_port, uint16_t websocket_port,
                                 hal_status_t (*poll_server)(void *),
                                 void *server);
bool vp37v_test_capture_start(const char *path,
                              hal_status_t (*begin_capture)(void *),
                              hal_status_t (*poll_server)(void *),
                              void *server);
bool vp37v_test_restart_packet(const char *messages);

#ifdef __cplusplus
}
#endif
#endif
