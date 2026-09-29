#include "sc_command_handlers.h"

#include "sc_param_handlers.h"
#include "sc_protocol.h"
#include "sc_session_vocabulary.h"

#include <hal/core/hal_array.h>
#include <hal/security/hal_crypto.h>
#include <hal/serial/hal_serial.h>
#include <hal/serial/hal_serial_session.h>
#include <hal/system/hal_system.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SC_COMMAND_BUILD_B64_SIZE 32u
#define SC_COMMAND_META_RESPONSE_SIZE 256u
#define SC_COMMAND_SMALL_RESPONSE_SIZE 96u
#define SC_COMMAND_REBOOT_DELAY_MS 50u

#define SC_COMMAND_TEST_RESPONSE_SIZE 250u

#define SC_COMMAND_REGISTERED_META UINT32_C(0x0001)
#define SC_COMMAND_REGISTERED_PARAM_LIST UINT32_C(0x0002)
#define SC_COMMAND_REGISTERED_VALUES UINT32_C(0x0004)
#define SC_COMMAND_REGISTERED_GET_PARAM UINT32_C(0x0008)
#define SC_COMMAND_REGISTERED_GPS UINT32_C(0x0010)
#define SC_COMMAND_REGISTERED_SET_PARAM UINT32_C(0x0020)
#define SC_COMMAND_REGISTERED_COMMIT UINT32_C(0x0040)
#define SC_COMMAND_REGISTERED_REVERT UINT32_C(0x0080)
#define SC_COMMAND_REGISTERED_REBOOT UINT32_C(0x0100)
#define SC_COMMAND_REGISTERED_TEST_LIST UINT32_C(0x0200)
#define SC_COMMAND_REGISTERED_TEST_INFO UINT32_C(0x0400)
#define SC_COMMAND_REGISTERED_TEST_PARAM UINT32_C(0x0800)
#define SC_COMMAND_REGISTERED_TEST_SET UINT32_C(0x1000)
#define SC_COMMAND_REGISTERED_TEST_RUN UINT32_C(0x2000)
#define SC_COMMAND_REGISTERED_TEST_STOP UINT32_C(0x4000)
#define SC_COMMAND_REGISTERED_TEST_SKIP UINT32_C(0x8000)
#define SC_COMMAND_REGISTERED_TEST_STATUS UINT32_C(0x10000)

typedef struct {
  hal_command_response_t *response;
  hal_status_t status;
} sc_command_emit_context_t;

typedef struct {
  const uint8_t *data;
  size_t length;
  size_t offset;
} sc_command_argument_cursor_t;

static hal_status_t response_begin(hal_command_response_t *response) {
  hal_status_t status =
      hal_command_response_set_encoding(response, HAL_COMMAND_ENCODING_TEXT);
  if (status == HAL_OK) {
    status = hal_command_response_set_content_type(response, "text/plain");
  }
  return status;
}

static hal_status_t response_write(hal_command_response_t *response,
                                   const char *payload) {
  hal_status_t status = response_begin(response);
  if (status == HAL_OK) {
    status = hal_command_response_write_str(response, payload);
  }
  return status;
}

static hal_status_t response_set_status(hal_command_response_t *response,
                                        hal_status_t status) {
  return status == HAL_OK
             ? HAL_OK
             : hal_command_response_set_status(response, status, NULL);
}

static hal_status_t response_write_with_status(hal_command_response_t *response,
                                               const char *payload,
                                               hal_status_t response_status) {
  const hal_status_t write_status = response_write(response, payload);
  return write_status == HAL_OK ? response_set_status(response, response_status)
                                : write_status;
}

static bool format_fits(int written, size_t capacity) {
  return written >= 0 && (size_t)written < capacity;
}

static void emit_to_response(const char *payload, void *user) {
  sc_command_emit_context_t *context = (sc_command_emit_context_t *)user;
  if (context == NULL || context->response == NULL ||
      context->status != HAL_OK) {
    return;
  }
  context->status = response_write(context->response, payload);
}

static hal_status_t emit_param_reply(
    hal_command_response_t *response,
    void (*emit_fn)(const sc_param_descriptor_t *, size_t, sc_emit_fn, void *),
    const sc_param_descriptor_t *params, size_t param_count) {
  sc_command_emit_context_t emit_context = {
      .response = response,
      .status = HAL_OK,
  };
  emit_fn(params, param_count, emit_to_response, &emit_context);
  return emit_context.status;
}

static bool arguments_begin(const hal_command_request_t *request,
                            sc_command_argument_cursor_t *cursor) {
  if (request == NULL || cursor == NULL ||
      request->encoding != HAL_COMMAND_ENCODING_TEXT ||
      (request->arguments == NULL && request->arguments_length != 0u)) {
    return false;
  }
  cursor->data = request->arguments;
  cursor->length = request->arguments_length;
  cursor->offset = 0u;
  return true;
}

static void arguments_skip_spaces(sc_command_argument_cursor_t *cursor) {
  while (cursor->offset < cursor->length &&
         cursor->data[cursor->offset] == (uint8_t)' ') {
    ++cursor->offset;
  }
}

static bool arguments_next_token(sc_command_argument_cursor_t *cursor,
                                 char *out, size_t out_size,
                                 bool *out_too_long) {
  if (cursor == NULL || out == NULL || out_size == 0u || out_too_long == NULL) {
    return false;
  }
  out[0] = '\0';
  *out_too_long = false;
  arguments_skip_spaces(cursor);
  const size_t start = cursor->offset;
  while (cursor->offset < cursor->length &&
         cursor->data[cursor->offset] != (uint8_t)' ') {
    ++cursor->offset;
  }
  const size_t token_length = cursor->offset - start;
  if (token_length == 0u) {
    return false;
  }
  if (token_length >= out_size) {
    *out_too_long = true;
    return false;
  }
  (void)memcpy(out, &cursor->data[start], token_length);
  out[token_length] = '\0';
  return true;
}

static bool arguments_finished(sc_command_argument_cursor_t *cursor) {
  arguments_skip_spaces(cursor);
  return cursor->offset == cursor->length;
}

/** @brief Parse a whole decimal token inside [@p min_value, @p max_value]. */
static bool parse_int_in(const char *text, int64_t min_value, int64_t max_value,
                         int64_t *out_value) {
  if (text == NULL || out_value == NULL || text[0] == '\0') {
    return false;
  }

  size_t offset = 0u;
  bool negative = false;
  if (text[offset] == '-' || text[offset] == '+') {
    negative = text[offset] == '-';
    ++offset;
  }
  if (text[offset] == '\0') {
    return false;
  }

  const int64_t limit = negative ? -min_value : max_value;
  int64_t value = 0;
  while (text[offset] != '\0') {
    const char digit = text[offset];
    if (digit < '0' || digit > '9') {
      return false;
    }
    const int64_t digit_value = (int64_t)(digit - '0');
    if (value > ((limit - digit_value) / 10)) {
      return false;
    }
    value = (value * 10) + digit_value;
    ++offset;
  }

  *out_value = negative ? -value : value;
  return true;
}

static bool parse_i16(const char *text, int16_t *out_value) {
  int64_t value = 0;
  if ((out_value == NULL) ||
      !parse_int_in(text, INT16_MIN, INT16_MAX, &value)) {
    return false;
  }
  *out_value = (int16_t)value;
  return true;
}

static bool parse_i32(const char *text, int32_t *out_value) {
  int64_t value = 0;
  if ((out_value == NULL) ||
      !parse_int_in(text, INT32_MIN, INT32_MAX, &value)) {
    return false;
  }
  *out_value = (int32_t)value;
  return true;
}

static bool request_has_no_arguments(const hal_command_request_t *request) {
  return request != NULL && request->encoding == HAL_COMMAND_ENCODING_TEXT &&
         request->arguments_length == 0u;
}

static bool
request_has_malformed_no_arg_command(const hal_command_request_t *request) {
  if (request == NULL || request->command == NULL ||
      request->arguments_length == 0u) {
    return false;
  }
  return strcmp(request->command, SC_CMD_GET_META) == 0 ||
         strcmp(request->command, SC_CMD_GET_PARAM_LIST) == 0 ||
         strcmp(request->command, SC_CMD_GET_VALUES) == 0 ||
         strcmp(request->command, SC_CMD_GET_GPS) == 0 ||
         strcmp(request->command, SC_CMD_COMMIT_PARAMS) == 0 ||
         strcmp(request->command, SC_CMD_REVERT_PARAMS) == 0 ||
         strcmp(request->command, SC_CMD_REBOOT_BOOTLOADER) == 0 ||
         strcmp(request->command, SC_CMD_TEST_LIST) == 0 ||
         strcmp(request->command, SC_CMD_TEST_STOP) == 0 ||
         strcmp(request->command, SC_CMD_TEST_SKIP) == 0 ||
         strcmp(request->command, SC_CMD_TEST_STATUS) == 0;
}

static hal_status_t handle_meta(const hal_command_request_t *request,
                                hal_command_response_t *response, void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }

  char uid_hex[HAL_DEVICE_UID_HEX_BUF_SIZE] = {0};
  if (!hal_get_device_uid_hex(uid_hex, sizeof(uid_hex))) {
    uid_hex[0] = '\0';
  }

  char build_b64[SC_COMMAND_BUILD_B64_SIZE] = {0};
  size_t build_b64_length = 0u;
  const uint8_t *build_bytes = (const uint8_t *)service->config.build_id;
  if (!hal_base64_encode(build_bytes, strlen(service->config.build_id),
                         build_b64, sizeof(build_b64), &build_b64_length)) {
    build_b64[0] = '\0';
  }
  (void)build_b64_length;

  char payload[SC_COMMAND_META_RESPONSE_SIZE] = {0};
  const int written = snprintf(
      payload, sizeof(payload), SC_REPLY_META_FMT, service->config.module_token,
      (unsigned)HAL_SERIAL_SESSION_PROTOCOL_VERSION,
      (unsigned long)request->session_id, service->config.firmware_version,
      build_b64, uid_hex[0] != '\0' ? uid_hex : HAL_SERIAL_SESSION_UNKNOWN);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write(response, payload);
}

static hal_status_t handle_param_list(const hal_command_request_t *request,
                                      hal_command_response_t *response,
                                      void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }
  return emit_param_reply(response, sc_param_reply_get_param_list,
                          service->config.params, service->config.param_count);
}

static hal_status_t handle_values(const hal_command_request_t *request,
                                  hal_command_response_t *response,
                                  void *user) {
  sc_command_service_t *service = (sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }
  if (service->config.refresh != NULL) {
    service->config.refresh(service->config.user);
  }

  sc_command_emit_context_t emit_context = {
      .response = response,
      .status = HAL_OK,
  };
  sc_param_reply_get_values_i16(
      service->config.params, service->config.param_count,
      service->config.active_values, emit_to_response, &emit_context);
  return emit_context.status;
}

static hal_status_t handle_get_param(const hal_command_request_t *request,
                                     hal_command_response_t *response,
                                     void *user) {
  sc_command_service_t *service = (sc_command_service_t *)user;
  sc_command_argument_cursor_t cursor = {0};
  char param_id[SC_PARAM_ID_MAX] = {0};
  bool too_long = false;

  if (!arguments_begin(request, &cursor) ||
      !arguments_next_token(&cursor, param_id, sizeof(param_id), &too_long)) {
    if (too_long) {
      return response_write_with_status(
          response, SC_STATUS_BAD_REQUEST " param_id_too_long", HAL_EINVAL);
    }
    return response_write_with_status(
        response,
        SC_STATUS_BAD_REQUEST " expected=" SC_CMD_GET_PARAM "_<param_id>",
        HAL_EINVAL);
  }
  if (!arguments_finished(&cursor)) {
    return response_write_with_status(
        response,
        SC_STATUS_BAD_REQUEST " expected=" SC_CMD_GET_PARAM "_<param_id>",
        HAL_EINVAL);
  }
  if (service->config.refresh != NULL) {
    service->config.refresh(service->config.user);
  }

  sc_command_emit_context_t emit_context = {
      .response = response,
      .status = HAL_OK,
  };
  sc_param_reply_get_param(service->config.params, service->config.param_count,
                           service->config.active_values, param_id,
                           emit_to_response, &emit_context);
  if (emit_context.status != HAL_OK) {
    return emit_context.status;
  }
  const hal_status_t result_status =
      sc_param_find_by_id(service->config.params, service->config.param_count,
                          param_id) == NULL
          ? HAL_ENOENT
          : HAL_OK;
  return response_set_status(response, result_status);
}

static hal_status_t handle_set_param(const hal_command_request_t *request,
                                     hal_command_response_t *response,
                                     void *user) {
  sc_command_service_t *service = (sc_command_service_t *)user;
  if (service->config.writes_ready != NULL &&
      !service->config.writes_ready(service->config.user)) {
    return response_write_with_status(
        response, SC_REPLY_NOT_READY_STORAGE_RECOVERY, HAL_EUNINIT);
  }
  sc_command_argument_cursor_t cursor = {0};
  char param_id[SC_PARAM_ID_MAX] = {0};
  char value_text[16] = {0};
  bool too_long = false;

  if (!arguments_begin(request, &cursor) ||
      !arguments_next_token(&cursor, param_id, sizeof(param_id), &too_long)) {
    if (too_long) {
      return response_write_with_status(
          response, SC_STATUS_BAD_REQUEST " param_id_too_long", HAL_EINVAL);
    }
    return response_write_with_status(response,
                                      SC_STATUS_BAD_REQUEST
                                      " expected=" SC_CMD_SET_PARAM
                                      " <param_id> <value>",
                                      HAL_EINVAL);
  }
  if (!arguments_next_token(&cursor, value_text, sizeof(value_text),
                            &too_long)) {
    if (too_long) {
      return response_write_with_status(
          response, SC_STATUS_BAD_REQUEST " value_not_int16", HAL_EINVAL);
    }
    return response_write_with_status(response,
                                      SC_STATUS_BAD_REQUEST
                                      " expected=" SC_CMD_SET_PARAM
                                      " <param_id> <value>",
                                      HAL_EINVAL);
  }
  if (!arguments_finished(&cursor)) {
    return response_write_with_status(response,
                                      SC_STATUS_BAD_REQUEST
                                      " expected=" SC_CMD_SET_PARAM
                                      " <param_id> <value>",
                                      HAL_EINVAL);
  }

  int16_t value = 0;
  if (!parse_i16(value_text, &value)) {
    return response_write_with_status(
        response, SC_STATUS_BAD_REQUEST " value_not_int16", HAL_EINVAL);
  }

  const sc_param_descriptor_t *descriptor = sc_param_find_by_id(
      service->config.params, service->config.param_count, param_id);
  hal_status_t result_status = HAL_OK;
  if (descriptor == NULL) {
    result_status = HAL_ENOENT;
  } else if (descriptor->kind != SC_PARAM_KIND_SCALAR_I16) {
    result_status = HAL_EINVAL;
  } else if ((descriptor->flags & SC_PARAM_FLAG_READ_ONLY) != 0u) {
    result_status = HAL_EPERM;
  } else if (!sc_param_validate_range(descriptor, value)) {
    result_status = HAL_EINVAL;
  }

  sc_command_emit_context_t emit_context = {
      .response = response,
      .status = HAL_OK,
  };
  const bool applied = sc_param_reply_set_param(
      service->config.params, service->config.param_count,
      service->config.staging_values, service->config.active_values, param_id,
      value, emit_to_response, &emit_context);
  if (applied && service->config.set_applied != NULL) {
    service->config.set_applied(service->config.user);
  }
  if (emit_context.status != HAL_OK) {
    return emit_context.status;
  }
  return response_set_status(response, result_status);
}

static hal_status_t handle_commit(const hal_command_request_t *request,
                                  hal_command_response_t *response,
                                  void *user) {
  sc_command_service_t *service = (sc_command_service_t *)user;
  if (service->config.writes_ready != NULL &&
      !service->config.writes_ready(service->config.user)) {
    return response_write_with_status(
        response, SC_REPLY_NOT_READY_STORAGE_RECOVERY, HAL_EUNINIT);
  }
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }

  const char *reason = NULL;
  size_t count = 0u;
  hal_status_t commit_status =
      service->config.commit(service->config.user, &reason, &count);
  if (commit_status == HAL_OK && reason != NULL) {
    commit_status = HAL_EINTERNAL;
  }
  if (commit_status != HAL_OK) {
    char payload[SC_COMMAND_SMALL_RESPONSE_SIZE] = {0};
    const int written =
        snprintf(payload, sizeof(payload), SC_REPLY_COMMIT_FAILED_FMT,
                 reason != NULL ? reason : "unknown");
    if (!format_fits(written, sizeof(payload))) {
      return HAL_EOVERFLOW;
    }
    return response_write_with_status(response, payload, commit_status);
  }

  char payload[64] = {0};
  const int written = snprintf(payload, sizeof(payload),
                               SC_REPLY_PARAMS_COMMITTED_FMT, (unsigned)count);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write(response, payload);
}

static hal_status_t handle_revert(const hal_command_request_t *request,
                                  hal_command_response_t *response,
                                  void *user) {
  sc_command_service_t *service = (sc_command_service_t *)user;
  if (service->config.writes_ready != NULL &&
      !service->config.writes_ready(service->config.user)) {
    return response_write_with_status(
        response, SC_REPLY_NOT_READY_STORAGE_RECOVERY, HAL_EUNINIT);
  }
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }
  service->config.revert(service->config.user);
  return response_write(response, SC_REPLY_PARAMS_REVERTED);
}

static hal_status_t handle_gps(const hal_command_request_t *request,
                               hal_command_response_t *response, void *user) {
  sc_command_service_t *service = (sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }

  sc_command_gps_snapshot_t snapshot = {0};
  service->config.read_gps(service->config.user, &snapshot);
  char payload[128] = {0};
  const int written =
      snprintf(payload, sizeof(payload), SC_REPLY_GPS_FMT,
               (unsigned)(snapshot.available ? 1u : 0u), (long)snapshot.lat_e6,
               (long)snapshot.lon_e6, (int)snapshot.speed_kmh_x10,
               (unsigned long)snapshot.epoch);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write(response, payload);
}

static hal_status_t handle_reboot(const hal_command_request_t *request,
                                  hal_command_response_t *response,
                                  void *user) {
  sc_command_service_t *service = (sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }
  service->reboot_pending = true;
  return response_write(response, SC_REPLY_REBOOT_OK);
}

/* ── Functional tests ──────────────────────────────────────────────── */

/** @brief Append @p text; false once the payload no longer fits. */
static bool payload_append(char *payload, size_t capacity, size_t *length,
                           const char *text) {
  const size_t text_length = strlen(text);
  if ((*length >= capacity) || (text_length >= (capacity - *length))) {
    return false;
  }
  (void)memcpy(&payload[*length], text, text_length + 1u);
  *length += text_length;
  return true;
}

/** @brief Append " <key>=<value>". */
static bool payload_append_pair(char *payload, size_t capacity, size_t *length,
                                const char *key, const char *value) {
  return payload_append(payload, capacity, length, " ") &&
         payload_append(payload, capacity, length, key) &&
         payload_append(payload, capacity, length, "=") &&
         payload_append(payload, capacity, length, value);
}

/** @brief Append " <key>=<signed number>". */
static bool payload_append_signed(char *payload, size_t capacity,
                                  size_t *length, const char *key, long value) {
  char text[24] = {0};
  const int written = snprintf(text, sizeof(text), "%ld", value);
  return format_fits(written, sizeof(text)) &&
         payload_append_pair(payload, capacity, length, key, text);
}

/** @brief Append " <key>=<unsigned number>". */
static bool payload_append_unsigned(char *payload, size_t capacity,
                                    size_t *length, const char *key,
                                    unsigned long value) {
  char text[24] = {0};
  const int written = snprintf(text, sizeof(text), "%lu", value);
  return format_fits(written, sizeof(text)) &&
         payload_append_pair(payload, capacity, length, key, text);
}

/** @brief Append @p item to a comma-separated list. */
static bool payload_append_item(char *payload, size_t capacity, size_t *length,
                                const char *item) {
  return ((*length == 0u) || payload_append(payload, capacity, length, ",")) &&
         payload_append(payload, capacity, length, item);
}

/** @brief Read exactly one argument token, answering the expected form when
 * it is missing, too long or followed by more. */
static bool test_single_argument(const hal_command_request_t *request,
                                 hal_command_response_t *response,
                                 const char *expected, char *out,
                                 size_t out_size, hal_status_t *out_status) {
  sc_command_argument_cursor_t cursor = {0};
  bool too_long = false;
  if (arguments_begin(request, &cursor) &&
      arguments_next_token(&cursor, out, out_size, &too_long) &&
      arguments_finished(&cursor)) {
    return true;
  }
  char payload[SC_COMMAND_SMALL_RESPONSE_SIZE] = {0};
  const int written = snprintf(payload, sizeof(payload),
                               SC_REPLY_BAD_REQUEST_EXPECTED_FMT, expected);
  *out_status = format_fits(written, sizeof(payload))
                    ? response_write_with_status(response, payload, HAL_EINVAL)
                    : HAL_EOVERFLOW;
  return false;
}

/** @brief Index of the named test, or the test count when there is none. */
static size_t test_find(const sc_command_service_t *service, const char *name,
                        sc_command_test_info_t *out_info) {
  const sc_command_test_ops_t *ops = service->config.tests;
  const size_t count = ops->count(service->config.user);
  for (size_t index = 0u; index < count; ++index) {
    if (ops->info(service->config.user, index, out_info) &&
        (out_info->name != NULL) && (strcmp(out_info->name, name) == 0)) {
      return index;
    }
  }
  return count;
}

static hal_status_t write_unknown_test(hal_command_response_t *response,
                                       const char *name) {
  char payload[SC_COMMAND_SMALL_RESPONSE_SIZE] = {0};
  const int written =
      snprintf(payload, sizeof(payload), SC_REPLY_TEST_UNKNOWN_FMT, name);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write_with_status(response, payload, HAL_ENOENT);
}

static hal_status_t write_invalid_test_param(hal_command_response_t *response,
                                             const char *id) {
  char payload[SC_COMMAND_SMALL_RESPONSE_SIZE] = {0};
  const int written =
      snprintf(payload, sizeof(payload), SC_REPLY_INVALID_PARAM_ID_FMT, id);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write_with_status(response, payload, HAL_ENOENT);
}

/** @brief Reply to a queued test request: its own payload, or the reason the
 * module refused it. */
static hal_status_t write_test_request_result(hal_command_response_t *response,
                                              hal_status_t status,
                                              const char *ok_payload,
                                              const char *name) {
  if (status == HAL_OK) {
    return response_write(response, ok_payload);
  }
  if (status == HAL_ENOENT) {
    return write_unknown_test(response, name);
  }
  if (status == HAL_EPERM) {
    return response_write_with_status(
        response, SC_REPLY_NOT_READY_ENGINE_RUNNING, HAL_EPERM);
  }
  if (status == HAL_EBUSY) {
    return response_write_with_status(response, SC_REPLY_NOT_READY_BUSY,
                                      HAL_EBUSY);
  }
  return status;
}

static hal_status_t handle_test_list(const hal_command_request_t *request,
                                     hal_command_response_t *response,
                                     void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }
  const sc_command_test_ops_t *ops = service->config.tests;
  const size_t count = ops->count(service->config.user);
  char names[SC_COMMAND_TEST_RESPONSE_SIZE] = {0};
  size_t length = 0u;
  for (size_t index = 0u; index < count; ++index) {
    sc_command_test_info_t info = {0};
    if (!ops->info(service->config.user, index, &info) || (info.name == NULL) ||
        !payload_append_item(names, sizeof(names), &length, info.name)) {
      return HAL_EOVERFLOW;
    }
  }
  char payload[SC_COMMAND_TEST_RESPONSE_SIZE] = {0};
  const int written = snprintf(payload, sizeof(payload), SC_REPLY_TEST_LIST_FMT,
                               (unsigned)count, names);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write(response, payload);
}

static hal_status_t handle_test_info(const hal_command_request_t *request,
                                     hal_command_response_t *response,
                                     void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  char name[SC_TEST_ID_MAX] = {0};
  hal_status_t status = HAL_OK;
  if (!test_single_argument(request, response, SC_CMD_TEST_INFO " <test>", name,
                            sizeof(name), &status)) {
    return status;
  }
  sc_command_test_info_t info = {0};
  const size_t index = test_find(service, name, &info);
  if (index >= service->config.tests->count(service->config.user)) {
    return write_unknown_test(response, name);
  }
  char params[SC_COMMAND_TEST_RESPONSE_SIZE] = {0};
  size_t length = 0u;
  for (size_t param = 0u; param < info.param_count; ++param) {
    sc_command_test_param_t detail = {0};
    if (!service->config.tests->param_at(service->config.user, index, param,
                                         &detail) ||
        (detail.id == NULL) ||
        !payload_append_item(params, sizeof(params), &length, detail.id)) {
      return HAL_EOVERFLOW;
    }
  }
  char payload[SC_COMMAND_TEST_RESPONSE_SIZE] = {0};
  const int written = snprintf(payload, sizeof(payload), SC_REPLY_TEST_INFO_FMT,
                               info.name, info.in_sequence ? 1u : 0u, params);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write(response, payload);
}

static hal_status_t handle_test_param(const hal_command_request_t *request,
                                      hal_command_response_t *response,
                                      void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  char id[SC_TEST_ID_MAX] = {0};
  hal_status_t status = HAL_OK;
  if (!test_single_argument(request, response, SC_CMD_TEST_PARAM " <id>", id,
                            sizeof(id), &status)) {
    return status;
  }
  sc_command_test_param_t detail = {0};
  if (!service->config.tests->param(service->config.user, id, &detail)) {
    return write_invalid_test_param(response, id);
  }
  char payload[SC_COMMAND_TEST_RESPONSE_SIZE] = {0};
  const int written =
      snprintf(payload, sizeof(payload), SC_REPLY_TEST_PARAM_FMT, detail.id,
               detail.test, (long)detail.value, (long)detail.min,
               (long)detail.max, (long)detail.default_value, detail.unit);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write(response, payload);
}

static hal_status_t handle_test_set(const hal_command_request_t *request,
                                    hal_command_response_t *response,
                                    void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  static const char k_expected[] =
      SC_STATUS_BAD_REQUEST " expected=" SC_CMD_TEST_SET " <id> <value>";
  sc_command_argument_cursor_t cursor = {0};
  char id[SC_TEST_ID_MAX] = {0};
  char value_text[16] = {0};
  bool too_long = false;
  if (!arguments_begin(request, &cursor) ||
      !arguments_next_token(&cursor, id, sizeof(id), &too_long) ||
      !arguments_next_token(&cursor, value_text, sizeof(value_text),
                            &too_long) ||
      !arguments_finished(&cursor)) {
    return response_write_with_status(response, k_expected, HAL_EINVAL);
  }
  int32_t value = 0;
  if (!parse_i32(value_text, &value)) {
    return response_write_with_status(
        response, SC_STATUS_BAD_REQUEST " value_not_int32", HAL_EINVAL);
  }
  sc_command_test_param_t detail = {0};
  if (!service->config.tests->param(service->config.user, id, &detail)) {
    return write_invalid_test_param(response, id);
  }
  const hal_status_t status =
      service->config.tests->set_param(service->config.user, id, value);
  char payload[SC_COMMAND_SMALL_RESPONSE_SIZE] = {0};
  int written = 0;
  if (status == HAL_OK) {
    written = snprintf(payload, sizeof(payload), SC_REPLY_TEST_SET_FMT, id,
                       (long)value);
  } else if (status == HAL_EINVAL) {
    written = snprintf(payload, sizeof(payload),
                       SC_REPLY_BAD_REQUEST_OUT_OF_RANGE_FMT, id,
                       (int)detail.min, (int)detail.max);
  } else {
    return status;
  }
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return response_write_with_status(response, payload, status);
}

static hal_status_t handle_test_run(const hal_command_request_t *request,
                                    hal_command_response_t *response,
                                    void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  char name[SC_TEST_ID_MAX] = {0};
  hal_status_t status = HAL_OK;
  if (!test_single_argument(request, response,
                            SC_CMD_TEST_RUN " <test|" SC_TEST_SEQUENCE ">",
                            name, sizeof(name), &status)) {
    return status;
  }
  char payload[SC_COMMAND_SMALL_RESPONSE_SIZE] = {0};
  const int written =
      snprintf(payload, sizeof(payload), SC_REPLY_TEST_RUN_FMT, name);
  if (!format_fits(written, sizeof(payload))) {
    return HAL_EOVERFLOW;
  }
  return write_test_request_result(
      response, service->config.tests->run(service->config.user, name), payload,
      name);
}

static hal_status_t handle_test_stop(const hal_command_request_t *request,
                                     hal_command_response_t *response,
                                     void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }
  return write_test_request_result(
      response, service->config.tests->stop(service->config.user),
      SC_REPLY_TEST_STOP, "");
}

static hal_status_t handle_test_skip(const hal_command_request_t *request,
                                     hal_command_response_t *response,
                                     void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }
  return write_test_request_result(
      response, service->config.tests->skip(service->config.user),
      SC_REPLY_TEST_SKIP, "");
}

static hal_status_t handle_test_status(const hal_command_request_t *request,
                                       hal_command_response_t *response,
                                       void *user) {
  const sc_command_service_t *service = (const sc_command_service_t *)user;
  if (!request_has_no_arguments(request)) {
    return response_write_with_status(response, SC_STATUS_UNKNOWN_CMD,
                                      HAL_EINVAL);
  }
  sc_command_test_status_t snapshot = {0};
  service->config.tests->status(service->config.user, &snapshot);

  char payload[SC_COMMAND_TEST_RESPONSE_SIZE] = {0};
  size_t length = 0u;
  const bool running = snapshot.active != NULL;
  bool fits =
      payload_append(payload, sizeof(payload), &length,
                     SC_REPLY_TEST_STATUS_HEAD) &&
      payload_append_pair(payload, sizeof(payload), &length, SC_TEST_KEY_STATE,
                          running ? SC_TEST_STATE_RUNNING
                                  : SC_TEST_STATE_IDLE) &&
      payload_append_unsigned(payload, sizeof(payload), &length,
                              SC_TEST_KEY_RUNS, (unsigned long)snapshot.runs);
  if (fits && running) {
    fits = payload_append_pair(payload, sizeof(payload), &length,
                               SC_TEST_KEY_TEST, snapshot.active) &&
           payload_append_pair(
               payload, sizeof(payload), &length, SC_TEST_KEY_SOURCE,
               (snapshot.source != NULL) ? snapshot.source
                                         : SC_TEST_SOURCE_CONSOLE) &&
           payload_append_unsigned(payload, sizeof(payload), &length,
                                   SC_TEST_KEY_ELAPSED_MS,
                                   (unsigned long)snapshot.elapsed_ms);
  }
  if (fits && running && (snapshot.seq_count != 0u)) {
    char step[16] = {0};
    const int written =
        snprintf(step, sizeof(step), "%u/%u", (unsigned)snapshot.seq_index,
                 (unsigned)snapshot.seq_count);
    fits = format_fits(written, sizeof(step)) &&
           payload_append_pair(payload, sizeof(payload), &length,
                               SC_TEST_KEY_SEQ, step);
  }
  if (fits && snapshot.drive_valid) {
    fits = payload_append_signed(payload, sizeof(payload), &length,
                                 SC_TEST_KEY_DEMAND_X10,
                                 (long)snapshot.demand_x10) &&
           payload_append_signed(payload, sizeof(payload), &length,
                                 SC_TEST_KEY_POSITION_X10,
                                 (long)snapshot.position_x10);
  }
  for (size_t index = 0u; fits && (index < snapshot.field_count) &&
                          (index < SC_COMMAND_TEST_FIELDS_MAX);
       ++index) {
    const sc_command_test_field_t *field = &snapshot.fields[index];
    fits = (field->text != NULL)
               ? payload_append_pair(payload, sizeof(payload), &length,
                                     field->key, field->text)
               : payload_append_signed(payload, sizeof(payload), &length,
                                       field->key, (long)field->value);
  }
  if (fits && (snapshot.last != NULL)) {
    fits = payload_append_pair(payload, sizeof(payload), &length,
                               SC_TEST_KEY_LAST, snapshot.last) &&
           payload_append_pair(payload, sizeof(payload), &length,
                               SC_TEST_KEY_RESULT,
                               (snapshot.result != NULL) ? snapshot.result
                                                         : SC_TEST_RESULT_DONE);
  }
  if (!fits) {
    return HAL_EOVERFLOW;
  }
  return response_write(response, payload);
}

/** @brief The SC_TEST_* commands: reads need no authentication, everything
 * that changes a test or moves the actuator does. */
typedef struct {
  const char *name;
  hal_command_handler_t handler;
  hal_command_security_flags_t security;
  uint32_t registered_bit;
} sc_test_command_t;

static const sc_test_command_t k_test_commands[] = {
    {SC_CMD_TEST_LIST, handle_test_list, 0u, SC_COMMAND_REGISTERED_TEST_LIST},
    {SC_CMD_TEST_INFO, handle_test_info, 0u, SC_COMMAND_REGISTERED_TEST_INFO},
    {SC_CMD_TEST_PARAM, handle_test_param, 0u,
     SC_COMMAND_REGISTERED_TEST_PARAM},
    {SC_CMD_TEST_STATUS, handle_test_status, 0u,
     SC_COMMAND_REGISTERED_TEST_STATUS},
    {SC_CMD_TEST_SET, handle_test_set, HAL_COMMAND_SECURITY_AUTHENTICATED,
     SC_COMMAND_REGISTERED_TEST_SET},
    {SC_CMD_TEST_RUN, handle_test_run, HAL_COMMAND_SECURITY_AUTHENTICATED,
     SC_COMMAND_REGISTERED_TEST_RUN},
    {SC_CMD_TEST_STOP, handle_test_stop, HAL_COMMAND_SECURITY_AUTHENTICATED,
     SC_COMMAND_REGISTERED_TEST_STOP},
    {SC_CMD_TEST_SKIP, handle_test_skip, HAL_COMMAND_SECURITY_AUTHENTICATED,
     SC_COMMAND_REGISTERED_TEST_SKIP},
};

static hal_status_t register_command(sc_command_service_t *service,
                                     const char *name,
                                     hal_command_security_flags_t security,
                                     hal_command_handler_t handler,
                                     uint32_t registered_bit) {
  const hal_command_definition_t definition = {
      .name = name,
      .allowed_sources = service->config.allowed_sources,
      .required_security = security,
      .handler = handler,
      .user = service,
  };
  const hal_status_t status =
      hal_command_router_register_unique(service->router, &definition);
  if (status == HAL_OK) {
    service->registered_commands |= registered_bit;
  }
  return status;
}

static hal_status_t unregister_command(sc_command_service_t *service,
                                       const char *name,
                                       hal_command_handler_t handler,
                                       uint32_t registered_bit) {
  if ((service->registered_commands & registered_bit) == 0u) {
    return HAL_OK;
  }
  const hal_status_t status = hal_command_router_unregister_if_matches(
      service->router, name, handler, service);
  if (status == HAL_OK || status == HAL_ENOENT) {
    service->registered_commands &= ~registered_bit;
    return HAL_OK;
  }
  return status;
}

static void unregister_and_record(sc_command_service_t *service,
                                  const char *name,
                                  hal_command_handler_t handler,
                                  uint32_t registered_bit,
                                  hal_status_t *first_error) {
  const hal_status_t status =
      unregister_command(service, name, handler, registered_bit);
  if (*first_error == HAL_OK && status != HAL_OK) {
    *first_error = status;
  }
}

static hal_status_t unregister_commands(sc_command_service_t *service) {
  hal_status_t first_error = HAL_OK;
  unregister_and_record(service, SC_CMD_GET_META, handle_meta,
                        SC_COMMAND_REGISTERED_META, &first_error);
  unregister_and_record(service, SC_CMD_GET_PARAM_LIST, handle_param_list,
                        SC_COMMAND_REGISTERED_PARAM_LIST, &first_error);
  unregister_and_record(service, SC_CMD_GET_VALUES, handle_values,
                        SC_COMMAND_REGISTERED_VALUES, &first_error);
  unregister_and_record(service, SC_CMD_GET_PARAM, handle_get_param,
                        SC_COMMAND_REGISTERED_GET_PARAM, &first_error);
  unregister_and_record(service, SC_CMD_GET_GPS, handle_gps,
                        SC_COMMAND_REGISTERED_GPS, &first_error);
  unregister_and_record(service, SC_CMD_SET_PARAM, handle_set_param,
                        SC_COMMAND_REGISTERED_SET_PARAM, &first_error);
  unregister_and_record(service, SC_CMD_COMMIT_PARAMS, handle_commit,
                        SC_COMMAND_REGISTERED_COMMIT, &first_error);
  unregister_and_record(service, SC_CMD_REVERT_PARAMS, handle_revert,
                        SC_COMMAND_REGISTERED_REVERT, &first_error);
  unregister_and_record(service, SC_CMD_REBOOT_BOOTLOADER, handle_reboot,
                        SC_COMMAND_REGISTERED_REBOOT, &first_error);
  for (size_t index = 0u; index < COUNTOF(k_test_commands); ++index) {
    /* A plain copy: the table row is const, the arguments need not be. */
    sc_test_command_t command = k_test_commands[index];
    unregister_and_record(service, command.name, command.handler,
                          command.registered_bit, &first_error);
  }
  return first_error;
}

static bool test_config_valid(const sc_command_service_config_t *config) {
  const sc_command_test_ops_t *ops = config->tests;
  return (ops == NULL) ||
         ((ops->count != NULL) && (ops->info != NULL) &&
          (ops->param_at != NULL) && (ops->param != NULL) &&
          (ops->set_param != NULL) && (ops->run != NULL) &&
          (ops->stop != NULL) && (ops->skip != NULL) && (ops->status != NULL));
}

static bool write_config_valid(const sc_command_service_config_t *config) {
  const bool has_staging = config->staging_values != NULL;
  const bool has_commit = config->commit != NULL;
  const bool has_revert = config->revert != NULL;
  const bool writes_enabled = has_staging && has_commit && has_revert;
  const bool writes_disabled = !has_staging && !has_commit && !has_revert;
  return (writes_enabled || writes_disabled) &&
         (config->set_applied == NULL || writes_enabled) &&
         (config->writes_ready == NULL || writes_enabled);
}

hal_status_t
sc_command_service_init(sc_command_service_t *service,
                        hal_command_router_t router,
                        const sc_command_service_config_t *config) {
  if (service == NULL || config == NULL || config->module_token == NULL ||
      config->firmware_version == NULL || config->build_id == NULL ||
      config->params == NULL || config->active_values == NULL ||
      config->param_count == 0u ||
      config->allowed_sources !=
          HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION) ||
      !write_config_valid(config) || !test_config_valid(config)) {
    return HAL_EINVAL;
  }
  if (service->initialized) {
    return HAL_EBUSY;
  }

  hal_command_router_t selected_router = router;
  hal_status_t status = HAL_OK;
  if (selected_router == NULL) {
    status = hal_command_router_default(&selected_router);
  }
  if (status != HAL_OK) {
    return status;
  }

  (void)memset(service, 0, sizeof(*service));
  service->config = *config;
  service->router = selected_router;
  service->initialized = true;

  status = register_command(service, SC_CMD_GET_META, 0u, handle_meta,
                            SC_COMMAND_REGISTERED_META);
  if (status == HAL_OK) {
    status =
        register_command(service, SC_CMD_GET_PARAM_LIST, 0u, handle_param_list,
                         SC_COMMAND_REGISTERED_PARAM_LIST);
  }
  if (status == HAL_OK) {
    status = register_command(service, SC_CMD_GET_VALUES, 0u, handle_values,
                              SC_COMMAND_REGISTERED_VALUES);
  }
  if (status == HAL_OK) {
    status = register_command(service, SC_CMD_GET_PARAM, 0u, handle_get_param,
                              SC_COMMAND_REGISTERED_GET_PARAM);
  }
  if (status == HAL_OK && service->config.read_gps != NULL) {
    status = register_command(service, SC_CMD_GET_GPS, 0u, handle_gps,
                              SC_COMMAND_REGISTERED_GPS);
  }

  const bool writes_enabled = service->config.staging_values != NULL;
  if (status == HAL_OK && writes_enabled) {
    status = register_command(
        service, SC_CMD_SET_PARAM, HAL_COMMAND_SECURITY_AUTHENTICATED,
        handle_set_param, SC_COMMAND_REGISTERED_SET_PARAM);
  }
  if (status == HAL_OK && writes_enabled) {
    status = register_command(service, SC_CMD_COMMIT_PARAMS,
                              HAL_COMMAND_SECURITY_AUTHENTICATED, handle_commit,
                              SC_COMMAND_REGISTERED_COMMIT);
  }
  if (status == HAL_OK && writes_enabled) {
    status = register_command(service, SC_CMD_REVERT_PARAMS,
                              HAL_COMMAND_SECURITY_AUTHENTICATED, handle_revert,
                              SC_COMMAND_REGISTERED_REVERT);
  }
  if (status == HAL_OK) {
    status = register_command(service, SC_CMD_REBOOT_BOOTLOADER,
                              HAL_COMMAND_SECURITY_AUTHENTICATED, handle_reboot,
                              SC_COMMAND_REGISTERED_REBOOT);
  }
  for (size_t index = 0u;
       (status == HAL_OK) && (service->config.tests != NULL) &&
       (index < COUNTOF(k_test_commands));
       ++index) {
    sc_test_command_t command = k_test_commands[index];
    status = register_command(service, command.name, command.security,
                              command.handler, command.registered_bit);
  }
  if (status != HAL_OK) {
    (void)unregister_commands(service);
    if (service->registered_commands == 0u) {
      (void)memset(service, 0, sizeof(*service));
    }
  }
  return status;
}

hal_status_t sc_command_service_deinit(sc_command_service_t *service) {
  if (service == NULL) {
    return HAL_EINVAL;
  }
  if (!service->initialized) {
    return HAL_EUNINIT;
  }

  const hal_status_t status = unregister_commands(service);
  if (service->registered_commands == 0u) {
    (void)memset(service, 0, sizeof(*service));
  }
  return status;
}

hal_command_router_t
sc_command_service_router(const sc_command_service_t *service) {
  return service != NULL ? service->router : NULL;
}

hal_status_t
sc_command_format_serial_response(const hal_command_request_t *request,
                                  const hal_command_response_t *response,
                                  char *output, size_t output_capacity,
                                  size_t *out_length, void *user) {
  (void)user;
  if (response == NULL || output == NULL || out_length == NULL) {
    return HAL_EINVAL;
  }
  *out_length = 0u;

  const char *payload = SC_STATUS_BAD_REQUEST;
  if (request_has_malformed_no_arg_command(request)) {
    payload = SC_STATUS_UNKNOWN_CMD;
  } else if (response->status == HAL_ENOENT) {
    payload = SC_STATUS_UNKNOWN_CMD;
  } else if (response->status == HAL_EAUTH || response->status == HAL_EPERM) {
    payload = SC_STATUS_NOT_AUTHORIZED;
  }

  const size_t length = strlen(payload);
  if (length > output_capacity) {
    return HAL_EOVERFLOW;
  }
  (void)memcpy(output, payload, length);
  *out_length = length;
  return HAL_OK;
}

bool sc_command_allow_inactive_reboot(const hal_command_request_t *request,
                                      void *user) {
  (void)user;
  return request != NULL && request->command != NULL &&
         strcmp(request->command, SC_CMD_REBOOT_BOOTLOADER) == 0 &&
         request->arguments_length == 0u;
}

void sc_command_reply_legacy_unknown(const char *line, void *session_user) {
  (void)line;
  hal_serial_session_println((hal_serial_session_t *)session_user,
                             "ERR UNKNOWN");
}

void sc_command_service_process_deferred(sc_command_service_t *service) {
  if (service == NULL || !service->reboot_pending) {
    return;
  }
  service->reboot_pending = false;
  hal_delay_ms(SC_COMMAND_REBOOT_DELAY_MS);
  (void)hal_enter_bootloader();
}

hal_status_t sc_config_session_stop(sc_config_session_t *session,
                                    const char *log_name) {
  hal_status_t status = HAL_EINVAL;
  if ((session != NULL) && (log_name != NULL)) {
    status = HAL_OK;
    if (session->commands.initialized) {
      status = hal_serial_commands_deinit(&session->commands);
      if (status != HAL_OK) {
        hal_derr("%s SC adapter detach failed: %s", log_name,
                 hal_status_to_string(status));
      }
    }
    if ((status == HAL_OK) && session->service.initialized) {
      status = sc_command_service_deinit(&session->service);
      if (status != HAL_OK) {
        hal_derr("%s SC service detach failed: %s", log_name,
                 hal_status_to_string(status));
      }
    }
  }
  return status;
}

hal_status_t sc_config_session_start(sc_config_session_t *session,
                                     const sc_command_service_config_t *config,
                                     hal_serial_session_unknown_cb_t fallback,
                                     void *fallback_user,
                                     const char *log_name) {
  hal_status_t status = HAL_EINVAL;
  if ((session != NULL) && (config != NULL) && (log_name != NULL)) {
    hal_serial_session_init_with_vocabulary(
        &session->session, config->module_token, config->firmware_version,
        config->build_id, &fiesta_default_vocabulary);
    status = sc_command_service_init(&session->service, NULL, config);
    if (status == HAL_OK) {
      hal_serial_commands_config_t adapter =
          hal_serial_commands_config_defaults(&session->session);
      adapter.router = sc_command_service_router(&session->service);
      adapter.command_prefix = SC_COMMAND_PREFIX;
      adapter.formatter = sc_command_format_serial_response;
      adapter.allow_inactive = sc_command_allow_inactive_reboot;
      if (fallback != NULL) {
        adapter.fallback = fallback;
        adapter.fallback_user = fallback_user;
      } else {
        adapter.fallback = sc_command_reply_legacy_unknown;
        adapter.fallback_user = &session->session;
      }
      status = hal_serial_commands_init(&session->commands, &adapter);
    }
    if (status != HAL_OK) {
      if (session->service.initialized) {
        (void)sc_command_service_deinit(&session->service);
      }
      hal_derr("%s SC adapter init failed: %s", log_name,
               hal_status_to_string(status));
    }
  }
  return status;
}

bool sc_config_session_poll(sc_config_session_t *session) {
  bool active = false;
  if (session != NULL) {
    hal_serial_session_poll(&session->session);
    sc_command_service_process_deferred(&session->service);
    active = hal_serial_session_is_active(&session->session);
  }
  return active;
}
