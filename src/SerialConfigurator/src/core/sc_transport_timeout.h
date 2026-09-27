#ifndef SC_TRANSPORT_TIMEOUT_H
#define SC_TRANSPORT_TIMEOUT_H

#include "../config.h"
#include "sc_protocol.h"

#include <stdbool.h>
#include <string.h>

/* Deadlines and retry policy of the default SC transport. */

static inline int sc_transport_command_timeout_ms(const char *command,
                                                  int attempt) {
  if (command != NULL && strcmp(command, SC_CMD_COMMIT_PARAMS) == 0) {
    return attempt == 0 ? SC_TRANSPORT_COMMIT_PRIMARY_TIMEOUT_MS
                        : SC_TRANSPORT_COMMIT_RETRY_TIMEOUT_MS;
  }
  return attempt == 0 ? SC_TRANSPORT_PRIMARY_TIMEOUT_MS
                      : SC_TRANSPORT_RETRY_TIMEOUT_MS;
}

/**
 * @brief Whether a reply says the module lost its session and wants HELLO.
 *
 * Only "SC_NOT_READY HELLO_REQUIRED" does. Other SC_NOT_READY replies
 * (STORAGE_RECOVERY, BUSY, ENGINE_RUNNING) come from a live session: a new
 * HELLO would only drop its authentication before the retry.
 */
static inline bool sc_transport_reply_requires_hello(const char *reply) {
  static const char k_hello_required[] = SC_REPLY_NOT_READY_HELLO_REQUIRED;
  const size_t length = sizeof(k_hello_required) - 1u;
  return reply != NULL && strncmp(reply, k_hello_required, length) == 0 &&
         (reply[length] == '\0' || reply[length] == ' ' ||
          reply[length] == '\r' || reply[length] == '\n');
}

#endif
