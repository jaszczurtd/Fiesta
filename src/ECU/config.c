
#include "config.h"
#include "tests.h"
#include <hal/hal_serial_session.h>

const char *err = "ERR";

static hal_serial_session_t s_configSession;

static void configSession_onUnknownLine(const char *line, void *user) {
  (void)user;
  /* Forward non-protocol command lines (PID tuning, etc.) to test fixtures.
   * This keeps the bootstrap session parser as the sole consumer of raw
   * serial bytes; secondary consumers receive whole lines only after HELLO
   * and friends have been handled. */
  tickTestsHandleSerialLine(line);
}

void configSessionInit(void) {
  hal_serial_session_init(&s_configSession, MODULE_NAME, FW_VERSION, BUILD_ID);
  hal_serial_session_set_unknown_handler(&s_configSession,
                                         &configSession_onUnknownLine,
                                         NULL);
}

void configSessionTick(void) {
  hal_serial_session_poll(&s_configSession);
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_configSession);
}

uint32_t configSessionId(void) {
  return hal_serial_session_id(&s_configSession);
}
