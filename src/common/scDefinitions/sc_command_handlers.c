#include "sc_command_handlers.h"

#include "sc_param_handlers.h"
#include "sc_protocol.h"

#include <hal/security/hal_crypto.h>
#include <hal/serial/hal_serial_session.h>
#include <hal/system/hal_system.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SC_COMMAND_BUILD_B64_SIZE 32u
#define SC_COMMAND_META_RESPONSE_SIZE 256u
#define SC_COMMAND_SMALL_RESPONSE_SIZE 96u
#define SC_COMMAND_REBOOT_DELAY_MS 50u

#define SC_COMMAND_REGISTERED_META UINT16_C(0x0001)
#define SC_COMMAND_REGISTERED_PARAM_LIST UINT16_C(0x0002)
#define SC_COMMAND_REGISTERED_VALUES UINT16_C(0x0004)
#define SC_COMMAND_REGISTERED_GET_PARAM UINT16_C(0x0008)
#define SC_COMMAND_REGISTERED_GPS UINT16_C(0x0010)
#define SC_COMMAND_REGISTERED_SET_PARAM UINT16_C(0x0020)
#define SC_COMMAND_REGISTERED_COMMIT UINT16_C(0x0040)
#define SC_COMMAND_REGISTERED_REVERT UINT16_C(0x0080)
#define SC_COMMAND_REGISTERED_REBOOT UINT16_C(0x0100)

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

static bool parse_i16(const char *text, int16_t *out_value) {
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

  const int32_t limit = negative ? -(int32_t)INT16_MIN : (int32_t)INT16_MAX;
  int32_t value = 0;
  while (text[offset] != '\0') {
    const char digit = text[offset];
    if (digit < '0' || digit > '9') {
      return false;
    }
    const int32_t digit_value = (int32_t)(digit - '0');
    if (value > ((limit - digit_value) / 10)) {
      return false;
    }
    value = (value * 10) + digit_value;
    ++offset;
  }

  *out_value = negative ? (int16_t)-value : (int16_t)value;
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
         strcmp(request->command, SC_CMD_REBOOT_BOOTLOADER) == 0;
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

static hal_status_t register_command(sc_command_service_t *service,
                                     const char *name,
                                     hal_command_security_flags_t security,
                                     hal_command_handler_t handler,
                                     uint16_t registered_bit) {
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
                                       uint16_t registered_bit) {
  if ((service->registered_commands & registered_bit) == 0u) {
    return HAL_OK;
  }
  const hal_status_t status = hal_command_router_unregister_if_matches(
      service->router, name, handler, service);
  if (status == HAL_OK || status == HAL_ENOENT) {
    service->registered_commands &= (uint16_t)~registered_bit;
    return HAL_OK;
  }
  return status;
}

static void unregister_and_record(sc_command_service_t *service,
                                  const char *name,
                                  hal_command_handler_t handler,
                                  uint16_t registered_bit,
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
  return first_error;
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
      !write_config_valid(config)) {
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
