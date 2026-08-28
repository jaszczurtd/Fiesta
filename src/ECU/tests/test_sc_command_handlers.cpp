#include "unity.h"

#include "../../common/scDefinitions/sc_command_handlers.h"
#include "../../common/scDefinitions/sc_param_types.h"
#include "../../common/scDefinitions/sc_protocol.h"
#include "hal/impl/.mock/hal_mock.h"

#include <cstring>

namespace {

struct test_values_t {
  int16_t value;
  int16_t read_only;
};

const sc_param_descriptor_t k_params[] = {
    SC_PARAM_SCALAR_I16("value", test_values_t, value, -10, 10, 1, 1, "test"),
    SC_PARAM_SCALAR_I16_RO_NOT_PERSISTED("read_only", test_values_t, read_only,
                                         -10, 10, 0, 1, "test"),
};

unsigned s_set_applied_count = 0u;
unsigned s_foreign_call_count = 0u;
hal_status_t s_commit_status = HAL_OK;

hal_status_t dummyHandler(const hal_command_request_t *,
                          hal_command_response_t *, void *) {
  return HAL_OK;
}

hal_status_t foreignHandler(const hal_command_request_t *,
                            hal_command_response_t *response, void *user) {
  auto *call_count = static_cast<unsigned *>(user);
  ++(*call_count);
  return hal_command_response_write_str(response, "FOREIGN");
}

void setApplied(void *) { ++s_set_applied_count; }

hal_status_t commit(void *, const char **out_reason, size_t *out_count) {
  if (out_reason == nullptr || out_count == nullptr) {
    return HAL_EINVAL;
  }
  *out_reason = s_commit_status == HAL_OK ? nullptr : "test_failure";
  *out_count = s_commit_status == HAL_OK ? 1u : 0u;
  return s_commit_status;
}

void revert(void *) {}

void readGps(void *, sc_command_gps_snapshot_t *out_snapshot) {
  if (out_snapshot != nullptr) {
    std::memset(out_snapshot, 0, sizeof(*out_snapshot));
  }
}

sc_command_service_config_t serviceConfig(test_values_t *active,
                                          test_values_t *staging) {
  sc_command_service_config_t config = {};
  config.module_token = "test";
  config.firmware_version = "1";
  config.build_id = "build";
  config.params = k_params;
  config.param_count = COUNTOF(k_params);
  config.active_values = active;
  config.staging_values = staging;
  config.set_applied = setApplied;
  config.commit = commit;
  config.revert = revert;
  config.read_gps = readGps;
  config.allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION);
  return config;
}

} // namespace

void setUp(void) {
  s_set_applied_count = 0u;
  s_foreign_call_count = 0u;
  s_commit_status = HAL_OK;
}

void tearDown(void) {}

void test_full_service_rolls_back_and_can_be_initialized_again(void) {
  hal_command_router_t router = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&router));
  TEST_ASSERT_NOT_NULL(router);

  static const char *const k_dummy_names[] = {
      "DUMMY_0", "DUMMY_1", "DUMMY_2", "DUMMY_3",
      "DUMMY_4", "DUMMY_5", "DUMMY_6", "DUMMY_7",
  };
  for (size_t index = 0u; index < COUNTOF(k_dummy_names); ++index) {
    const hal_command_definition_t definition = {
        .name = k_dummy_names[index],
        .allowed_sources = HAL_COMMAND_SOURCE_MASK_ALL,
        .required_security = 0u,
        .handler = dummyHandler,
        .user = nullptr,
    };
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_command_router_register(router, &definition));
  }

  test_values_t active = {.value = 1};
  test_values_t staging = active;
  const sc_command_service_config_t config = serviceConfig(&active, &staging);
  sc_command_service_t service = {};

  TEST_ASSERT_EQUAL_INT(HAL_ENOMEM,
                        sc_command_service_init(&service, router, &config));
  size_t command_count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(router, &command_count));
  TEST_ASSERT_EQUAL_UINT(COUNTOF(k_dummy_names), command_count);
  TEST_ASSERT_FALSE(service.initialized);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_clear(router));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        sc_command_service_init(&service, router, &config));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(router, &command_count));
  TEST_ASSERT_EQUAL_UINT(9u, command_count);
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY,
                        sc_command_service_init(&service, router, &config));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(router, &command_count));
  TEST_ASSERT_EQUAL_UINT(9u, command_count);

  static const uint8_t k_arguments[] = "value 5";
  const hal_command_request_t request = {
      .source = HAL_COMMAND_SOURCE_SERIAL_SESSION,
      .encoding = HAL_COMMAND_ENCODING_TEXT,
      .command = SC_CMD_SET_PARAM,
      .arguments = k_arguments,
      .arguments_length = sizeof(k_arguments) - 1u,
      .request_id = 1u,
      .peer_id = 0u,
      .session_id = 1u,
      .security_flags = HAL_COMMAND_SECURITY_AUTHENTICATED,
      .source_context = nullptr,
  };
  hal_command_response_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_dispatch(router, &request, &response));
  TEST_ASSERT_EQUAL_STRING("SC_OK PARAM_SET id=value staged=5 active=1",
                           response.body);
  TEST_ASSERT_EQUAL_INT16(5, staging.value);
  TEST_ASSERT_EQUAL_UINT(1u, s_set_applied_count);

  static const uint8_t k_unknown_arguments[] = "missing 5";
  hal_command_request_t error_request = request;
  error_request.arguments = k_unknown_arguments;
  error_request.arguments_length = sizeof(k_unknown_arguments) - 1u;
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, hal_command_router_dispatch(
                                        router, &error_request, &response));
  TEST_ASSERT_EQUAL_STRING("SC_INVALID_PARAM_ID id=missing", response.body);

  static const uint8_t k_range_arguments[] = "value 50";
  error_request.arguments = k_range_arguments;
  error_request.arguments_length = sizeof(k_range_arguments) - 1u;
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_command_router_dispatch(
                                        router, &error_request, &response));
  TEST_ASSERT_EQUAL_STRING(
      "SC_BAD_REQUEST out_of_range id=value min=-10 max=10", response.body);

  static const uint8_t k_read_only_arguments[] = "read_only 5";
  error_request.arguments = k_read_only_arguments;
  error_request.arguments_length = sizeof(k_read_only_arguments) - 1u;
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(HAL_EPERM, hal_command_router_dispatch(
                                       router, &error_request, &response));
  TEST_ASSERT_EQUAL_STRING("SC_BAD_REQUEST read_only id=read_only",
                           response.body);

  static const uint8_t k_extra_arguments[] = "extra";
  error_request.command = SC_CMD_GET_META;
  error_request.arguments = k_extra_arguments;
  error_request.arguments_length = sizeof(k_extra_arguments) - 1u;
  error_request.security_flags = 0u;
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_command_router_dispatch(
                                        router, &error_request, &response));
  TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", response.body);

  error_request.arguments = nullptr;
  error_request.arguments_length = 0u;
  error_request.encoding = HAL_COMMAND_ENCODING_JSON;
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, hal_command_router_dispatch(
                                        router, &error_request, &response));
  TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", response.body);

  s_commit_status = HAL_EIO;
  error_request.command = SC_CMD_COMMIT_PARAMS;
  error_request.encoding = HAL_COMMAND_ENCODING_TEXT;
  error_request.arguments = nullptr;
  error_request.arguments_length = 0u;
  error_request.security_flags = HAL_COMMAND_SECURITY_AUTHENTICATED;
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(
      HAL_EIO, hal_command_router_dispatch(router, &error_request, &response));
  TEST_ASSERT_EQUAL_STRING("SC_COMMIT_FAILED reason=test_failure",
                           response.body);
  TEST_ASSERT_EQUAL_UINT(1u, s_set_applied_count);

  hal_mock_bootloader_reset_flag();
  error_request.command = SC_CMD_REBOOT_BOOTLOADER;
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_dispatch(router, &error_request, &response));
  TEST_ASSERT_EQUAL_STRING("SC_OK REBOOT", response.body);
  TEST_ASSERT_FALSE(hal_mock_bootloader_was_requested());
  sc_command_service_process_deferred(&service);
  TEST_ASSERT_TRUE(hal_mock_bootloader_was_requested());

  TEST_ASSERT_EQUAL_INT(HAL_OK, sc_command_service_deinit(&service));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(router, &command_count));
  TEST_ASSERT_EQUAL_UINT(0u, command_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

void test_service_rejects_incomplete_write_and_invalid_source_config(void) {
  hal_command_router_t router = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&router));

  test_values_t active = {.value = 1};
  test_values_t staging = active;
  const sc_command_service_config_t complete = serviceConfig(&active, &staging);

  sc_command_service_config_t invalid_configs[8] = {
      complete, complete, complete, complete,
      complete, complete, complete, complete,
  };
  invalid_configs[0].commit = nullptr;
  invalid_configs[1].revert = nullptr;
  invalid_configs[2].staging_values = nullptr;
  invalid_configs[3].staging_values = nullptr;
  invalid_configs[3].commit = nullptr;
  invalid_configs[3].revert = nullptr;
  invalid_configs[4].allowed_sources =
      HAL_COMMAND_SOURCE_MASK_ALL |
      (UINT32_C(1) << static_cast<uint32_t>(HAL_COMMAND_SOURCE_COUNT));
  invalid_configs[5].allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_BLE_STREAM);
  invalid_configs[6].allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_LORA_LINK);
  invalid_configs[7].allowed_sources =
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_SERIAL_SESSION) |
      HAL_COMMAND_SOURCE_MASK(HAL_COMMAND_SOURCE_BLE_STREAM);

  for (size_t index = 0u; index < COUNTOF(invalid_configs); ++index) {
    sc_command_service_t service = {};
    TEST_ASSERT_EQUAL_INT(
        HAL_EINVAL,
        sc_command_service_init(&service, router, &invalid_configs[index]));
    TEST_ASSERT_FALSE(service.initialized);
  }

  size_t command_count = 1u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(router, &command_count));
  TEST_ASSERT_EQUAL_UINT(0u, command_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

void test_service_preserves_foreign_command_entries(void) {
  hal_command_router_t router = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&router));

  const hal_command_definition_t late_foreign = {
      .name = SC_CMD_REBOOT_BOOTLOADER,
      .allowed_sources = HAL_COMMAND_SOURCE_MASK_ALL,
      .required_security = 0u,
      .handler = foreignHandler,
      .user = &s_foreign_call_count,
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_register(router, &late_foreign));

  test_values_t active = {.value = 1};
  test_values_t staging = active;
  const sc_command_service_config_t config = serviceConfig(&active, &staging);
  sc_command_service_t service = {};
  TEST_ASSERT_EQUAL_INT(HAL_EEXIST,
                        sc_command_service_init(&service, router, &config));
  TEST_ASSERT_FALSE(service.initialized);

  size_t command_count = 0u;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(router, &command_count));
  TEST_ASSERT_EQUAL_UINT(1u, command_count);

  const hal_command_request_t late_request = {
      .source = HAL_COMMAND_SOURCE_SERIAL_SESSION,
      .encoding = HAL_COMMAND_ENCODING_TEXT,
      .command = SC_CMD_REBOOT_BOOTLOADER,
      .arguments = nullptr,
      .arguments_length = 0u,
      .request_id = 1u,
      .peer_id = 0u,
      .session_id = 1u,
      .security_flags = 0u,
      .source_context = nullptr,
  };
  hal_command_response_t response = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_dispatch(router, &late_request, &response));
  TEST_ASSERT_EQUAL_STRING("FOREIGN", response.body);
  TEST_ASSERT_EQUAL_UINT(1u, s_foreign_call_count);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_clear(router));
  s_foreign_call_count = 0u;

  const hal_command_definition_t foreign = {
      .name = SC_CMD_GET_META,
      .allowed_sources = HAL_COMMAND_SOURCE_MASK_ALL,
      .required_security = 0u,
      .handler = foreignHandler,
      .user = &s_foreign_call_count,
  };
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_register(router, &foreign));

  TEST_ASSERT_EQUAL_INT(HAL_EEXIST,
                        sc_command_service_init(&service, router, &config));
  TEST_ASSERT_FALSE(service.initialized);

  const hal_command_request_t request = {
      .source = HAL_COMMAND_SOURCE_SERIAL_SESSION,
      .encoding = HAL_COMMAND_ENCODING_TEXT,
      .command = SC_CMD_GET_META,
      .arguments = nullptr,
      .arguments_length = 0u,
      .request_id = 1u,
      .peer_id = 0u,
      .session_id = 1u,
      .security_flags = 0u,
      .source_context = nullptr,
  };
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_dispatch(router, &request, &response));
  TEST_ASSERT_EQUAL_STRING("FOREIGN", response.body);
  TEST_ASSERT_EQUAL_UINT(1u, s_foreign_call_count);

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_clear(router));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        sc_command_service_init(&service, router, &config));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_register(router, &foreign));

  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, sc_command_service_deinit(&service));
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_count(router, &command_count));
  TEST_ASSERT_EQUAL_UINT(1u, command_count);
  std::memset(&response, 0, sizeof(response));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_dispatch(router, &request, &response));
  TEST_ASSERT_EQUAL_STRING("FOREIGN", response.body);
  TEST_ASSERT_EQUAL_UINT(2u, s_foreign_call_count);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_command_router_unregister(router, SC_CMD_GET_META));
  TEST_ASSERT_EQUAL_INT(HAL_OK, sc_command_service_deinit(&service));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(router));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_full_service_rolls_back_and_can_be_initialized_again);
  RUN_TEST(test_service_rejects_incomplete_write_and_invalid_source_config);
  RUN_TEST(test_service_preserves_foreign_command_entries);
  return UNITY_END();
}
