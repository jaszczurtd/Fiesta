#ifndef VP37V_BROWSER_VIEW_H
#define VP37V_BROWSER_VIEW_H
#include "hal/core/hal_status.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool vp37v_browser_fixture(uint16_t port, hal_status_t (*poll_server)(void *),
                           void *server, const char *web, const char *messages,
                           const char *output);
#ifdef __cplusplus
}
#endif
#endif
