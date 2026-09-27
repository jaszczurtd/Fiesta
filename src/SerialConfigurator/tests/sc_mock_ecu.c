/*
 * Mock ECU for host tests; see sc_mock_ecu.h.
 */

#include "sc_mock_ecu.h"

#include "sc_protocol.h"

#include <stdio.h>
#include <string.h>

MockEcu s_ecu;

void sc_mock_ecu_reset(void) {
  memset(&s_ecu, 0, sizeof(s_ecu));
  s_ecu.tests_compiled = true;
  s_ecu.cyclic_passes = 4;
  s_ecu.status_reply = "SC_OK TEST_STATUS state=idle runs=0";
}

static bool mock_list(void *ctx, ScTransportCandidateList *list, char *err,
                      size_t err_size) {
  (void)ctx;
  (void)err;
  (void)err_size;
  list->count = 1u;
  list->truncated = false;
  (void)snprintf(list->paths[0], sizeof(list->paths[0]), "%s", MOCK_PATH);
  return true;
}

static bool mock_resolve(void *ctx, const char *candidate, char *out,
                         size_t out_size, char *err, size_t err_size) {
  (void)ctx;
  (void)err;
  (void)err_size;
  (void)snprintf(out, out_size, "%s", candidate);
  return true;
}

static bool mock_hello(void *ctx, const char *path, char *response,
                       size_t response_size, char *err, size_t err_size) {
  (void)ctx;
  (void)path;
  (void)err;
  (void)err_size;
  s_ecu.authenticated = false; /* HELLO opens a new, unauthenticated session */
  (void)snprintf(response, response_size,
                 "OK HELLO module=" SC_MODULE_TOKEN_ECU
                 " proto=1 session=7 fw=v1 build=b1 uid=E661A4D1234567AB");
  return true;
}

static void reply(char *response, size_t size, const char *text) {
  (void)snprintf(response, size, "%s", text);
}

/** @brief Authenticated command: false (and the refusal) when not. */
static bool gate(char *response, size_t size) {
  if (!s_ecu.authenticated) {
    reply(response, size, SC_STATUS_NOT_AUTHORIZED);
    return false;
  }
  return true;
}

/** @brief One queued request: BUSY while the mock slot is still full. */
static bool take_request(char *response, size_t size) {
  if (s_ecu.busy_replies != 0u) {
    s_ecu.busy_replies--;
    reply(response, size, SC_REPLY_NOT_READY_BUSY);
    return false;
  }
  return true;
}

static bool mock_command(void *ctx, const char *path, const char *command,
                         char *response, size_t size, char *err,
                         size_t err_size) {
  (void)ctx;
  (void)path;
  (void)err;
  (void)err_size;
  s_ecu.sent++;
  if (strcmp(command, SC_CMD_GET_META) == 0) {
    reply(response, size,
          "SC_OK META module=" SC_MODULE_TOKEN_ECU
          " proto=1 session=7 fw=v1 build=b1 uid=E661A4");
    return true;
  }
  if (strcmp(command, SC_CMD_AUTH_BEGIN) == 0) {
    reply(response, size,
          "SC_OK AUTH_CHALLENGE 101112131415161718191a1b1c1d1e1f");
    return true;
  }
  if (strncmp(command, SC_CMD_AUTH_PROVE " ", 14u) == 0) {
    s_ecu.authenticated = true;
    reply(response, size, SC_REPLY_AUTH_OK);
    return true;
  }
  if (!s_ecu.tests_compiled) {
    reply(response, size, SC_STATUS_UNKNOWN_CMD);
    return true;
  }
  if (strcmp(command, SC_CMD_TEST_LIST) == 0) {
    reply(response, size,
          s_ecu.production ? "SC_OK TEST_LIST count=0 names="
                           : "SC_OK TEST_LIST count=2 names=pot,cyclic");
  } else if (strcmp(command, SC_CMD_TEST_INFO " cyclic") == 0) {
    reply(response, size,
          "SC_OK TEST_INFO name=cyclic seq=1 "
          "params=cyclic_passes,cyclic_cycles");
  } else if (strcmp(command, SC_CMD_TEST_INFO " pot") == 0) {
    /* Parameters left out: the catalog must take a test without any. */
    reply(response, size, "SC_OK TEST_INFO name=pot seq=0 params=");
  } else if (strcmp(command, SC_CMD_TEST_PARAM " cyclic_passes") == 0) {
    (void)snprintf(response, size,
                   "SC_OK TEST_PARAM id=cyclic_passes test=cyclic value=%ld "
                   "min=1 max=100 default=4 unit=count",
                   (long)s_ecu.cyclic_passes);
  } else if (strcmp(command, SC_CMD_TEST_PARAM " cyclic_cycles") == 0) {
    reply(response, size,
          "SC_OK TEST_PARAM id=cyclic_cycles test=cyclic value=6 min=1 "
          "max=50 default=6 unit=count");
  } else if (strcmp(command, SC_CMD_TEST_STATUS) == 0) {
    reply(response, size, s_ecu.status_reply);
  } else if (strncmp(command, SC_CMD_TEST_SET " ", 12u) == 0) {
    if (!gate(response, size)) {
      return true;
    }
    char id[32] = {0};
    long value = 0;
    if (sscanf(command + 12, "%31s %ld", id, &value) != 2 ||
        strcmp(id, "cyclic_passes") != 0) {
      (void)snprintf(response, size, "SC_INVALID_PARAM_ID id=%s", id);
    } else if (value < 1 || value > 100) {
      reply(response, size,
            "SC_BAD_REQUEST out_of_range id=cyclic_passes min=1 max=100");
    } else {
      s_ecu.cyclic_passes = (int32_t)value;
      (void)snprintf(s_ecu.last_set, sizeof(s_ecu.last_set), "%s %ld", id,
                     value);
      (void)snprintf(response, size, "SC_OK TEST_SET id=%s value=%ld", id,
                     value);
    }
  } else if (strncmp(command, SC_CMD_TEST_RUN " ", 12u) == 0) {
    const char *name = command + 12;
    if (!gate(response, size)) {
      return true;
    }
    if (strcmp(name, "cyclic") != 0 && strcmp(name, "pot") != 0 &&
        strcmp(name, SC_TEST_SEQUENCE) != 0) {
      (void)snprintf(response, size, "SC_BAD_REQUEST unknown_test name=%s",
                     name);
    } else if (s_ecu.engine_running) {
      reply(response, size, SC_REPLY_NOT_READY_ENGINE_RUNNING);
    } else if (take_request(response, size)) {
      (void)snprintf(s_ecu.last_run, sizeof(s_ecu.last_run), "%s", name);
      (void)snprintf(response, size, "SC_OK TEST_RUN name=%s", name);
    }
  } else if (strcmp(command, SC_CMD_TEST_STOP) == 0) {
    if (gate(response, size) && take_request(response, size)) {
      s_ecu.stops++;
      reply(response, size, SC_REPLY_TEST_STOP);
    }
  } else if (strcmp(command, SC_CMD_TEST_SKIP) == 0) {
    if (gate(response, size) && take_request(response, size)) {
      s_ecu.skips++;
      reply(response, size, SC_REPLY_TEST_SKIP);
    }
  } else {
    reply(response, size, SC_STATUS_UNKNOWN_CMD);
  }
  return true;
}

const ScTransportOps k_mock_ecu_ops = {
    .list_candidates = mock_list,
    .resolve_device_path = mock_resolve,
    .send_hello = mock_hello,
    .send_sc_command = mock_command,
};

/** @brief Core with the mock ECU detected at module index 0. */
bool sc_mock_ecu_detected_core(ScCore *core) {
  sc_core_init(core);
  ScTransport transport;
  sc_transport_init_custom(&transport, &k_mock_ecu_ops, NULL);
  sc_core_set_transport(core, &transport);
  char log[2048] = {0};
  sc_core_detect_modules(core, log, sizeof(log));
  const ScModuleStatus *ecu = sc_core_module_status(core, 0u);
  return ecu != NULL && ecu->detected;
}
