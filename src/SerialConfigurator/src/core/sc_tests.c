/**
 * @file sc_tests.c
 * @brief Host side of the ECU functional tests (SC_TEST_* commands).
 *
 * Reads go through the core's command dispatcher, like the other read-only
 * endpoints. Actions go straight to the transport on the device path, like
 * the parameter writes, because they share the session that
 * sc_core_authenticate opened on the same cached port.
 */

#include "sc_tests.h"

#include <hal/core/hal_array.h>

#include <stdio.h>
#include <string.h>

#include "sc_protocol.h"
#include "sc_text.h"
#include "sc_time.h"
#include "sc_transport.h"

/* ── Reply parsing ──────────────────────────────────────────────────────── */

/** @brief Split "key=value" in place; false when either side is empty. */
static bool split_pair(char *token, const char **out_key,
                       const char **out_value) {
  char *equals = strchr(token, '=');
  if (equals == NULL || equals == token) {
    return false;
  }
  *equals = '\0';
  *out_key = token;
  *out_value = equals + 1;
  return true;
}

static bool parse_i32(const char *text, int32_t *out) {
  int64_t value = 0;
  if (!sc_text_parse_i64(text, &value) || value < INT32_MIN ||
      value > INT32_MAX) {
    return false;
  }
  *out = (int32_t)value;
  return true;
}

static bool parse_u32(const char *text, uint32_t *out) {
  int64_t value = 0;
  if (!sc_text_parse_i64(text, &value) || value < 0 ||
      value > (int64_t)UINT32_MAX) {
    return false;
  }
  *out = (uint32_t)value;
  return true;
}

/** @brief Whether @p result is "SC_OK <topic> ..."; writes the reason when
 *         it is not. */
static bool expect_ok(const ScCommandResult *result, const char *topic,
                      char *error, size_t error_size) {
  if (result == NULL) {
    sc_text_set_error(error, error_size, "missing reply");
    return false;
  }
  if (result->status != SC_COMMAND_STATUS_OK ||
      strcmp(result->topic, topic) != 0) {
    char message[256];
    (void)snprintf(message, sizeof(message), "expected %s %.32s, got: %.160s",
                   SC_STATUS_OK, topic, result->response);
    sc_text_set_error(error, error_size, message);
    return false;
  }
  return true;
}

/**
 * @brief Split a comma list into fixed-size slots.
 * @return Number of items, or SIZE_MAX when one is empty or there are more
 *         than @p capacity.
 */
static size_t split_list(const char *list, char *items, size_t item_size,
                         size_t capacity) {
  size_t count = 0u;
  const char *cursor = list;
  while (cursor[0] != '\0') {
    const char *comma = strchr(cursor, ',');
    const char *end = (comma != NULL) ? comma : cursor + strlen(cursor);
    if (end == cursor || count >= capacity) {
      return SIZE_MAX;
    }
    sc_text_copy_span(&items[count * item_size], item_size, cursor, end);
    count++;
    cursor = (comma != NULL) ? comma + 1 : end;
    if (comma != NULL && cursor[0] == '\0') {
      return SIZE_MAX;
    }
  }
  return count;
}

static void parse_fail(char *error, size_t error_size, const char *what,
                       const char *token) {
  char message[128];
  (void)snprintf(message, sizeof(message), "%s: bad token '%.64s'", what,
                 token);
  sc_text_set_error(error, error_size, message);
}

bool sc_tests_parse_list(const ScCommandResult *result, ScTestCatalog *out,
                         char *error, size_t error_size) {
  if (out == NULL) {
    sc_text_set_error(error, error_size, "null argument");
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!expect_ok(result, SC_REPLY_TAG_TEST_LIST, error, error_size)) {
    return false;
  }
  out->supported = true;
  uint32_t count = 0u;
  bool seen_count = false;
  bool seen_names = false;
  const char *cursor = result->details;
  char token[SC_HELLO_RESPONSE_MAX];
  while (sc_text_next_token(&cursor, token, sizeof(token))) {
    const char *key = NULL;
    const char *value = NULL;
    const bool pair = split_pair(token, &key, &value);
    if (pair && strcmp(key, "count") == 0 && parse_u32(value, &count)) {
      seen_count = true;
    } else if (pair && strcmp(key, "names") == 0) {
      char names[SC_TESTS_MAX][SC_TEST_ID_MAX];
      const size_t parsed =
          split_list(value, &names[0][0], SC_TEST_ID_MAX, COUNTOF(names));
      if (parsed == SIZE_MAX) {
        parse_fail(error, error_size, "test list", value);
        return false;
      }
      for (size_t i = 0u; i < parsed; ++i) {
        sc_text_copy(out->tests[i].name, sizeof(out->tests[i].name), names[i]);
      }
      out->count = parsed;
      seen_names = true;
    } else if (!pair) {
      parse_fail(error, error_size, "test list", token);
      return false;
    } else {
      /* Unknown keys are left for newer firmware. */
    }
  }
  if (!seen_count || !seen_names || count != out->count) {
    sc_text_set_error(error, error_size,
                      "test list: count and names disagree or are missing");
    return false;
  }
  return true;
}

bool sc_tests_parse_info(const ScCommandResult *result, ScTestEntry *entry,
                         char *error, size_t error_size) {
  if (entry == NULL) {
    sc_text_set_error(error, error_size, "null argument");
    return false;
  }
  if (!expect_ok(result, SC_REPLY_TAG_TEST_INFO, error, error_size)) {
    return false;
  }
  bool seen_name = false;
  bool seen_seq = false;
  bool seen_params = false;
  const char *cursor = result->details;
  char token[SC_HELLO_RESPONSE_MAX];
  while (sc_text_next_token(&cursor, token, sizeof(token))) {
    const char *key = NULL;
    const char *value = NULL;
    if (!split_pair(token, &key, &value)) {
      parse_fail(error, error_size, "test info", token);
      return false;
    }
    uint32_t flag = 0u;
    if (strcmp(key, "name") == 0 && value[0] != '\0') {
      sc_text_copy(entry->name, sizeof(entry->name), value);
      seen_name = true;
    } else if (strcmp(key, "seq") == 0 && parse_u32(value, &flag) &&
               flag <= 1u) {
      entry->in_sequence = (flag == 1u);
      seen_seq = true;
    } else if (strcmp(key, "params") == 0) {
      char ids[SC_TESTS_PARAMS_MAX][SC_TEST_ID_MAX];
      const size_t parsed =
          split_list(value, &ids[0][0], SC_TEST_ID_MAX, COUNTOF(ids));
      if (parsed == SIZE_MAX) {
        parse_fail(error, error_size, "test info", value);
        return false;
      }
      for (size_t i = 0u; i < parsed; ++i) {
        memset(&entry->params[i], 0, sizeof(entry->params[i]));
        sc_text_copy(entry->params[i].id, sizeof(entry->params[i].id), ids[i]);
      }
      entry->param_count = parsed;
      seen_params = true;
    } else if (strcmp(key, "name") == 0 || strcmp(key, "seq") == 0) {
      parse_fail(error, error_size, "test info", value);
      return false;
    } else {
      /* Unknown keys are left for newer firmware. */
    }
  }
  if (!seen_name || !seen_seq || !seen_params) {
    sc_text_set_error(error, error_size, "test info: missing field");
    return false;
  }
  return true;
}

bool sc_tests_parse_param(const ScCommandResult *result, ScTestParam *param,
                          char *error, size_t error_size) {
  if (param == NULL) {
    sc_text_set_error(error, error_size, "null argument");
    return false;
  }
  memset(param, 0, sizeof(*param));
  if (!expect_ok(result, SC_REPLY_TAG_TEST_PARAM, error, error_size)) {
    return false;
  }
  static const char *const k_required[] = {
      "id", "test", "value", "min", "max", "default", "unit",
  };
  bool seen[COUNTOF(k_required)] = {false};
  const char *cursor = result->details;
  char token[SC_HELLO_RESPONSE_MAX];
  while (sc_text_next_token(&cursor, token, sizeof(token))) {
    const char *key = NULL;
    const char *value = NULL;
    if (!split_pair(token, &key, &value)) {
      parse_fail(error, error_size, "test param", token);
      return false;
    }
    bool ok = true;
    size_t slot = COUNTOF(k_required);
    for (size_t i = 0u; i < COUNTOF(k_required); ++i) {
      if (strcmp(key, k_required[i]) == 0) {
        slot = i;
      }
    }
    switch (slot) {
    case 0u:
      sc_text_copy(param->id, sizeof(param->id), value);
      break;
    case 1u:
      sc_text_copy(param->test, sizeof(param->test), value);
      break;
    case 2u:
      ok = parse_i32(value, &param->value);
      break;
    case 3u:
      ok = parse_i32(value, &param->min);
      break;
    case 4u:
      ok = parse_i32(value, &param->max);
      break;
    case 5u:
      ok = parse_i32(value, &param->default_value);
      break;
    case 6u:
      sc_text_copy(param->unit, sizeof(param->unit), value);
      break;
    default:
      /* Unknown keys are left for newer firmware. */
      break;
    }
    if (!ok || (slot < COUNTOF(k_required) && value[0] == '\0')) {
      parse_fail(error, error_size, "test param", value);
      return false;
    }
    if (slot < COUNTOF(k_required)) {
      seen[slot] = true;
    }
  }
  for (size_t i = 0u; i < COUNTOF(seen); ++i) {
    if (!seen[i]) {
      char message[64];
      (void)snprintf(message, sizeof(message), "test param: missing %s",
                     k_required[i]);
      sc_text_set_error(error, error_size, message);
      return false;
    }
  }
  if (param->min > param->max) {
    sc_text_set_error(error, error_size, "test param: min above max");
    return false;
  }
  return true;
}

/** @brief Parse "seq=<i>/<n>". */
static bool parse_seq(const char *value, unsigned *out_index,
                      unsigned *out_count) {
  const char *slash = strchr(value, '/');
  if (slash == NULL) {
    return false;
  }
  char index_text[16];
  sc_text_copy_span(index_text, sizeof(index_text), value, slash);
  uint32_t index = 0u;
  uint32_t count = 0u;
  if (!parse_u32(index_text, &index) || !parse_u32(slash + 1, &count) ||
      index > count) {
    return false;
  }
  *out_index = (unsigned)index;
  *out_count = (unsigned)count;
  return true;
}

bool sc_tests_parse_status(const ScCommandResult *result, ScTestStatus *out,
                           char *error, size_t error_size) {
  if (out == NULL) {
    sc_text_set_error(error, error_size, "null argument");
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!expect_ok(result, SC_REPLY_TAG_TEST_STATUS, error, error_size)) {
    return false;
  }
  bool seen_state = false;
  bool seen_demand = false;
  bool seen_position = false;
  const char *cursor = result->details;
  char token[SC_HELLO_RESPONSE_MAX];
  while (sc_text_next_token(&cursor, token, sizeof(token))) {
    const char *key = NULL;
    const char *value = NULL;
    if (!split_pair(token, &key, &value) || value[0] == '\0') {
      parse_fail(error, error_size, "test status", token);
      return false;
    }
    bool ok = true;
    if (strcmp(key, SC_TEST_KEY_STATE) == 0) {
      ok = strcmp(value, SC_TEST_STATE_RUNNING) == 0 ||
           strcmp(value, SC_TEST_STATE_IDLE) == 0;
      out->running = strcmp(value, SC_TEST_STATE_RUNNING) == 0;
      seen_state = ok;
    } else if (strcmp(key, SC_TEST_KEY_RUNS) == 0) {
      ok = parse_u32(value, &out->runs);
    } else if (strcmp(key, SC_TEST_KEY_TEST) == 0) {
      sc_text_copy(out->test, sizeof(out->test), value);
    } else if (strcmp(key, SC_TEST_KEY_SOURCE) == 0) {
      sc_text_copy(out->source, sizeof(out->source), value);
    } else if (strcmp(key, SC_TEST_KEY_ELAPSED_MS) == 0) {
      ok = parse_u32(value, &out->elapsed_ms);
    } else if (strcmp(key, SC_TEST_KEY_SEQ) == 0) {
      ok = parse_seq(value, &out->seq_index, &out->seq_count);
    } else if (strcmp(key, SC_TEST_KEY_DEMAND_X10) == 0) {
      ok = parse_i32(value, &out->demand_x10);
      seen_demand = ok;
    } else if (strcmp(key, SC_TEST_KEY_POSITION_X10) == 0) {
      ok = parse_i32(value, &out->position_x10);
      seen_position = ok;
    } else if (strcmp(key, SC_TEST_KEY_LAST) == 0) {
      sc_text_copy(out->last, sizeof(out->last), value);
      out->has_last = true;
    } else if (strcmp(key, SC_TEST_KEY_RESULT) == 0) {
      sc_text_copy(out->result, sizeof(out->result), value);
    } else if (out->field_count < COUNTOF(out->fields)) {
      ScTestField *field = &out->fields[out->field_count];
      sc_text_copy(field->key, sizeof(field->key), key);
      field->is_text = !parse_i32(value, &field->value);
      if (field->is_text) {
        sc_text_copy(field->text, sizeof(field->text), value);
      }
      out->field_count++;
    } else {
      /* More progress values than the host keeps: the rest is dropped. */
    }
    if (!ok) {
      parse_fail(error, error_size, "test status", value);
      return false;
    }
  }
  if (!seen_state) {
    sc_text_set_error(error, error_size, "test status: missing state");
    return false;
  }
  out->drive_valid = seen_demand && seen_position;
  return true;
}

bool sc_tests_status_field(const ScTestStatus *status, const char *key,
                           int32_t *out_value) {
  if (status == NULL || key == NULL) {
    return false;
  }
  for (size_t i = 0u; i < status->field_count; ++i) {
    const ScTestField *field = &status->fields[i];
    if (!field->is_text && strcmp(field->key, key) == 0) {
      if (out_value != NULL) {
        *out_value = field->value;
      }
      return true;
    }
  }
  return false;
}

/* ── Reads through the core ─────────────────────────────────────────────── */

/** @brief Send one read command; the transport log becomes the error. */
static bool send_read(ScCore *core, size_t module_index, const char *command,
                      ScCommandResult *result, char *error, size_t error_size) {
  char log[SC_RUNTIME_COMMAND_LOG_MAX];
  log[0] = '\0';
  if (!sc_core_send_sc_command(core, module_index, command, result, log,
                               sizeof(log))) {
    sc_text_set_error(error, error_size, log[0] != '\0' ? log : command);
    return false;
  }
  return true;
}

bool sc_tests_load_catalog(ScCore *core, size_t module_index,
                           ScTestCatalog *out, char *error, size_t error_size) {
  if (out == NULL) {
    sc_text_set_error(error, error_size, "null argument");
    return false;
  }
  memset(out, 0, sizeof(*out));
  ScCommandResult result;
  if (!send_read(core, module_index, SC_CMD_TEST_LIST, &result, error,
                 error_size)) {
    return false;
  }
  if (result.status == SC_COMMAND_STATUS_UNKNOWN_CMD) {
    return true; /* Firmware without functional tests. */
  }
  if (!sc_tests_parse_list(&result, out, error, error_size)) {
    return false;
  }
  for (size_t t = 0u; t < out->count; ++t) {
    ScTestEntry *entry = &out->tests[t];
    char command[SC_HELLO_RESPONSE_MAX];
    (void)snprintf(command, sizeof(command), "%s %s", SC_CMD_TEST_INFO,
                   entry->name);
    if (!send_read(core, module_index, command, &result, error, error_size) ||
        !sc_tests_parse_info(&result, entry, error, error_size)) {
      return false;
    }
    for (size_t p = 0u; p < entry->param_count; ++p) {
      ScTestParam *param = &entry->params[p];
      (void)snprintf(command, sizeof(command), "%s %s", SC_CMD_TEST_PARAM,
                     param->id);
      char id[SC_TEST_ID_MAX];
      sc_text_copy(id, sizeof(id), param->id);
      if (!send_read(core, module_index, command, &result, error, error_size) ||
          !sc_tests_parse_param(&result, param, error, error_size)) {
        return false;
      }
      if (strcmp(id, param->id) != 0) {
        sc_text_set_error(error, error_size,
                          "test param: reply names another parameter");
        return false;
      }
    }
  }
  return true;
}

bool sc_tests_get_status(ScCore *core, size_t module_index, ScTestStatus *out,
                         char *error, size_t error_size) {
  ScCommandResult result;
  return send_read(core, module_index, SC_CMD_TEST_STATUS, &result, error,
                   error_size) &&
         sc_tests_parse_status(&result, out, error, error_size);
}

/* ── Authenticated actions ──────────────────────────────────────────────── */

const char *sc_test_action_status_name(ScTestActionStatus status) {
  switch (status) {
  case SC_TEST_ACTION_OK:
    return "ok";
  case SC_TEST_ACTION_ERR_NULL_ARG:
    return "null_arg";
  case SC_TEST_ACTION_ERR_TRANSPORT:
    return "transport";
  case SC_TEST_ACTION_ERR_NOT_AUTHORIZED:
    return "not_authorized";
  case SC_TEST_ACTION_ERR_UNKNOWN_TEST:
    return "unknown_test";
  case SC_TEST_ACTION_ERR_INVALID_PARAM:
    return "invalid_param";
  case SC_TEST_ACTION_ERR_OUT_OF_RANGE:
    return "out_of_range";
  case SC_TEST_ACTION_ERR_BUSY:
    return "busy";
  case SC_TEST_ACTION_ERR_ENGINE_RUNNING:
    return "engine_running";
  case SC_TEST_ACTION_ERR_UNEXPECTED_REPLY:
    return "unexpected_reply";
  default:
    return "unknown";
  }
}

/** @brief Map a reply to an action status; OK only for "SC_OK <tag>". */
static ScTestActionStatus classify_reply(const char *reply,
                                         const char *ok_tag) {
  static const char k_ok[] = SC_STATUS_OK " ";
  static const char k_bad[] = SC_STATUS_BAD_REQUEST " ";
  if (strncmp(reply, k_ok, sizeof(k_ok) - 1u) == 0 &&
      sc_text_starts_with_token(reply + sizeof(k_ok) - 1u, ok_tag)) {
    return SC_TEST_ACTION_OK;
  }
  if (sc_text_starts_with_token(reply, SC_REPLY_NOT_READY_BUSY)) {
    return SC_TEST_ACTION_ERR_BUSY;
  }
  if (sc_text_starts_with_token(reply, SC_REPLY_NOT_READY_ENGINE_RUNNING)) {
    return SC_TEST_ACTION_ERR_ENGINE_RUNNING;
  }
  if (sc_text_starts_with_token(reply, SC_STATUS_NOT_AUTHORIZED)) {
    return SC_TEST_ACTION_ERR_NOT_AUTHORIZED;
  }
  if (sc_text_starts_with_token(reply, SC_STATUS_INVALID_PARAM_ID)) {
    return SC_TEST_ACTION_ERR_INVALID_PARAM;
  }
  if (strncmp(reply, k_bad, sizeof(k_bad) - 1u) == 0) {
    const char *reason = reply + sizeof(k_bad) - 1u;
    if (sc_text_starts_with_token(reason, SC_REPLY_REASON_UNKNOWN_TEST)) {
      return SC_TEST_ACTION_ERR_UNKNOWN_TEST;
    }
    if (sc_text_starts_with_token(reason, SC_REPLY_REASON_OUT_OF_RANGE)) {
      return SC_TEST_ACTION_ERR_OUT_OF_RANGE;
    }
  }
  return SC_TEST_ACTION_ERR_UNEXPECTED_REPLY;
}

/**
 * @brief Send one authenticated test command and classify the reply.
 *
 * BUSY means an earlier request still waits for the controller core, so
 * the command is repeated a few times before BUSY is reported.
 */
static ScTestActionStatus send_action(const ScTransport *transport,
                                      const char *device_path,
                                      const char *command, const char *ok_tag,
                                      char *error, size_t error_size) {
  if (transport == NULL || transport->ops == NULL ||
      transport->ops->send_sc_command == NULL || device_path == NULL) {
    sc_text_set_error(error, error_size, "null argument");
    return SC_TEST_ACTION_ERR_NULL_ARG;
  }
  ScTestActionStatus status = SC_TEST_ACTION_ERR_BUSY;
  char reply[SC_HELLO_RESPONSE_MAX];
  reply[0] = '\0';
  for (unsigned attempt = 0u;
       attempt <= SC_TESTS_BUSY_RETRIES && status == SC_TEST_ACTION_ERR_BUSY;
       ++attempt) {
    if (attempt != 0u) {
      sc_time_sleep_ms(SC_TESTS_BUSY_RETRY_DELAY_MS);
    }
    char tx_error[256];
    tx_error[0] = '\0';
    if (!sc_transport_send_sc_command(transport, device_path, command, reply,
                                      sizeof(reply), tx_error,
                                      sizeof(tx_error))) {
      char message[256];
      (void)snprintf(message, sizeof(message), "%.64s transport failed: %.160s",
                     command, tx_error);
      sc_text_set_error(error, error_size, message);
      return SC_TEST_ACTION_ERR_TRANSPORT;
    }
    status = classify_reply(reply, ok_tag);
  }
  if (status != SC_TEST_ACTION_OK) {
    char message[256];
    (void)snprintf(message, sizeof(message), "firmware: %.200s", reply);
    sc_text_set_error(error, error_size, message);
  }
  return status;
}

/** @brief Whether @p text is a single wire token that fits a command. */
static bool valid_token(const char *text) {
  return text != NULL && text[0] != '\0' && strlen(text) < SC_TEST_ID_MAX &&
         strpbrk(text, " \t\r\n") == NULL;
}

ScTestActionStatus sc_tests_set_param(const ScTransport *transport,
                                      const char *device_path, const char *id,
                                      int32_t value, char *error,
                                      size_t error_size) {
  if (!valid_token(id)) {
    sc_text_set_error(error, error_size, "invalid parameter id");
    return SC_TEST_ACTION_ERR_NULL_ARG;
  }
  char command[SC_HELLO_RESPONSE_MAX];
  (void)snprintf(command, sizeof(command), "%s %s %ld", SC_CMD_TEST_SET, id,
                 (long)value);
  return send_action(transport, device_path, command, SC_REPLY_TAG_TEST_SET,
                     error, error_size);
}

ScTestActionStatus sc_tests_run(const ScTransport *transport,
                                const char *device_path, const char *name,
                                char *error, size_t error_size) {
  if (!valid_token(name)) {
    sc_text_set_error(error, error_size, "invalid test name");
    return SC_TEST_ACTION_ERR_NULL_ARG;
  }
  char command[SC_HELLO_RESPONSE_MAX];
  (void)snprintf(command, sizeof(command), "%s %s", SC_CMD_TEST_RUN, name);
  return send_action(transport, device_path, command, SC_REPLY_TAG_TEST_RUN,
                     error, error_size);
}

ScTestActionStatus sc_tests_stop(const ScTransport *transport,
                                 const char *device_path, char *error,
                                 size_t error_size) {
  return send_action(transport, device_path, SC_CMD_TEST_STOP,
                     SC_REPLY_TAG_TEST_STOP, error, error_size);
}

ScTestActionStatus sc_tests_skip(const ScTransport *transport,
                                 const char *device_path, char *error,
                                 size_t error_size) {
  return send_action(transport, device_path, SC_CMD_TEST_SKIP,
                     SC_REPLY_TAG_TEST_SKIP, error, error_size);
}
