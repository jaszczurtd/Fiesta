#include "config.h"
#include <hal/hal_serial_session.h>

static hal_serial_session_t s_configSession;

void configSessionInit(void) {
  hal_serial_session_init(&s_configSession);
}

void configSessionTick(void) {
  hal_serial_session_poll(&s_configSession, MODULE_NAME);
}

bool configSessionActive(void) {
  return hal_serial_session_is_active(&s_configSession);
}

uint32_t configSessionId(void) {
  return hal_serial_session_id(&s_configSession);
}

