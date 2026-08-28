#ifndef SC_TRANSPORT_TIMEOUT_H
#define SC_TRANSPORT_TIMEOUT_H

#include "../config.h"
#include "sc_protocol.h"

#include <string.h>

static inline int sc_transport_command_timeout_ms(const char *command,
                                                  int attempt) {
  if (command != NULL && strcmp(command, SC_CMD_COMMIT_PARAMS) == 0) {
    return attempt == 0 ? SC_TRANSPORT_COMMIT_PRIMARY_TIMEOUT_MS
                        : SC_TRANSPORT_COMMIT_RETRY_TIMEOUT_MS;
  }
  return attempt == 0 ? SC_TRANSPORT_PRIMARY_TIMEOUT_MS
                      : SC_TRANSPORT_RETRY_TIMEOUT_MS;
}

#endif
