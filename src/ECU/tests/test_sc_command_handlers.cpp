#include "unity.h"

#include "../../common/scDefinitions/sc_command_handlers.h"
#include "../../common/scDefinitions/sc_param_types.h"
#include "../../common/scDefinitions/sc_protocol.h"
#include "hal/impl/.mock/hal_mock.h"

#include <cstdio>
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

// ── Functional tests seen through fake operations ─────────────────────────

struct fake_tests_t {
  int32_t alpha_rate;
  hal_status_t run_status;
  hal_status_t stop_status;
  hal_status_t skip_status;
  char last_run[SC_TEST_ID_MAX];
  unsigned set_calls;
  unsigned stop_calls;
  unsigned skip_calls;
  sc_command_test_status_t status;
};

fake_tests_t s_fake = {};

const sc_command_test_info_t k_fake_tests[] = {
    {"alpha", true, 1u},
    {"beta", false, 0u},
};

size_t fakeCount(void *) { return COUNTOF(k_fake_tests); }

bool fakeInfo(void *, size_t index, sc_command_test_info_t *out) {
  if (index >= COUNTOF(k_fake_tests)) {
    return false;
  }
  *out = k_fake_tests[index];
  return true;
}

void fillAlphaRate(sc_command_test_param_t *out) {
  *out = {"alpha_rate", "alpha", SC_TEST_UNIT_COUNT, s_fake.alpha_rate, 1,
          10,           4};
}

bool fakeParamAt(void *, size_t test_index, size_t param_index,
                 sc_command_test_param_t *out) {
  if (test_index != 0u || param_index != 0u) {
    return false;
  }
  fillAlphaRate(out);
  return true;
}

bool fakeParam(void *, const char *id, sc_command_test_param_t *out) {
  if (std::strcmp(id, "alpha_rate") != 0) {
    return false;
  }
  fillAlphaRate(out);
  return true;
}

hal_status_t fakeSetParam(void *, const char *id, int32_t value) {
  ++s_fake.set_calls;
  if (std::strcmp(id, "alpha_rate") != 0) {
    return HAL_ENOENT;
  }
  if (value < 1 || value > 10) {
    return HAL_EINVAL;
  }
  s_fake.alpha_rate = value;
  return HAL_OK;
}

hal_status_t fakeRun(void *, const char *name) {
  (void)snprintf(s_fake.last_run, sizeof(s_fake.last_run), "%s", name);
  return s_fake.run_status;
}

hal_status_t fakeStop(void *) {
  ++s_fake.stop_calls;
  return s_fake.stop_status;
}

hal_status_t fakeSkip(void *) {
  ++s_fake.skip_calls;
  return s_fake.skip_status;
}

void fakeStatus(void *, sc_command_test_status_t *out) { *out = s_fake.status; }

const sc_command_test_ops_t k_fake_ops = {
    fakeCount, fakeInfo, fakeParamAt, fakeParam,  fakeSetParam,
    fakeRun,   fakeStop, fakeSkip,    fakeStatus,
};

/** @brief Router with the full service and the fake tests installed. */
struct test_service_t {
  test_values_t active;
  test_values_t staging;
  sc_command_service_t service;
  hal_command_router_t router;
};

void startTestService(test_service_t *fixture) {
  *fixture = {};
  fixture->active.value = 1;
  fixture->staging = fixture->active;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&fixture->router));
  sc_command_service_config_t config =
      serviceConfig(&fixture->active, &fixture->staging);
  config.tests = &k_fake_ops;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK,
      sc_command_service_init(&fixture->service, fixture->router, &config));
}

void stopTestService(test_service_t *fixture) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, sc_command_service_deinit(&fixture->service));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(fixture->router));
}

/** @brief Dispatch one text command; the reply body lands in @p out_body. */
hal_status_t sendText(const test_service_t &fixture, const char *command,
                      const char *arguments, bool authenticated, char *out_body,
                      size_t out_size) {
  const hal_command_request_t request = {
      .source = HAL_COMMAND_SOURCE_SERIAL_SESSION,
      .encoding = HAL_COMMAND_ENCODING_TEXT,
      .command = command,
      .arguments = reinterpret_cast<const uint8_t *>(arguments),
      .arguments_length = arguments != nullptr ? std::strlen(arguments) : 0u,
      .request_id = 1u,
      .peer_id = 0u,
      .session_id = 1u,
      .security_flags = authenticated ? HAL_COMMAND_SECURITY_AUTHENTICATED : 0u,
      .source_context = nullptr,
  };
  hal_command_response_t response = {};
  const hal_status_t status =
      hal_command_router_dispatch(fixture.router, &request, &response);
  (void)snprintf(out_body, out_size, "%.*s", (int)response.body_len,
                 response.body);
  return status;
}

} // namespace

void setUp(void) {
  s_fake = {};
  s_fake.alpha_rate = 4;
  s_fake.run_status = HAL_OK;
  s_fake.stop_status = HAL_OK;
  s_fake.skip_status = HAL_OK;
  s_set_applied_count = 0u;
  s_foreign_call_count = 0u;
  s_commit_status = HAL_OK;
}

void tearDown(void) {}

void test_full_service_rolls_back_and_can_be_initialized_again(void) {
  hal_command_router_t router = nullptr;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_create(&router));
  TEST_ASSERT_NOT_NULL(router);

  // Leave eight free slots: one fewer than the nine commands the service
  // registers without tests, whatever the router's capacity.
  static const size_t k_free_slots = 8u;
  static char k_dummy_names[HAL_COMMAND_ROUTER_MAX_COMMANDS][16];
  const size_t dummy_count = HAL_COMMAND_ROUTER_MAX_COMMANDS - k_free_slots;
  for (size_t index = 0u; index < dummy_count; ++index) {
    (void)snprintf(k_dummy_names[index], sizeof(k_dummy_names[index]),
                   "DUMMY_%u", (unsigned)index);
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
  TEST_ASSERT_EQUAL_UINT(dummy_count, command_count);
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

void test_test_commands_are_registered_only_with_complete_operations(void) {
  test_service_t fixture;
  startTestService(&fixture);
  size_t command_count = 0u;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_count(fixture.router, &command_count));
  TEST_ASSERT_EQUAL_UINT(17u, command_count);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(HAL_COMMAND_ROUTER_MAX_COMMANDS,
                                 command_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, sc_command_service_deinit(&fixture.service));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_count(fixture.router, &command_count));
  TEST_ASSERT_EQUAL_UINT(0u, command_count);

  // A module that leaves out any operation gets no test command at all.
  test_values_t active = {.value = 1};
  test_values_t staging = active;
  sc_command_service_config_t config = serviceConfig(&active, &staging);
  sc_command_test_ops_t incomplete = k_fake_ops;
  incomplete.skip = nullptr;
  config.tests = &incomplete;
  sc_command_service_t service = {};
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, sc_command_service_init(&service, fixture.router, &config));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_command_router_count(fixture.router, &command_count));
  TEST_ASSERT_EQUAL_UINT(0u, command_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_command_router_destroy(fixture.router));
}

void test_test_catalog_reads_need_no_authentication(void) {
  test_service_t fixture;
  startTestService(&fixture);
  char body[HAL_COMMAND_RESPONSE_BUFFER_SIZE + 1u] = {};

  TEST_ASSERT_EQUAL_INT(HAL_OK, sendText(fixture, SC_CMD_TEST_LIST, nullptr,
                                         false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_LIST count=2 names=alpha,beta", body);

  TEST_ASSERT_EQUAL_INT(HAL_OK, sendText(fixture, SC_CMD_TEST_INFO, "alpha",
                                         false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_INFO name=alpha seq=1 params=alpha_rate",
                           body);
  TEST_ASSERT_EQUAL_INT(HAL_OK, sendText(fixture, SC_CMD_TEST_INFO, "beta",
                                         false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_INFO name=beta seq=0 params=", body);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, sendText(fixture, SC_CMD_TEST_INFO, "gamma",
                                             false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_BAD_REQUEST unknown_test name=gamma", body);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        sendText(fixture, SC_CMD_TEST_INFO, "alpha beta", false,
                                 body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_BAD_REQUEST expected=SC_TEST_INFO <test>", body);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        sendText(fixture, SC_CMD_TEST_PARAM, "alpha_rate",
                                 false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_PARAM id=alpha_rate test=alpha value=4 "
                           "min=1 max=10 default=4 unit=count",
                           body);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        sendText(fixture, SC_CMD_TEST_PARAM, "missing", false,
                                 body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_INVALID_PARAM_ID id=missing", body);

  TEST_ASSERT_EQUAL_INT(HAL_OK, sendText(fixture, SC_CMD_TEST_STATUS, nullptr,
                                         false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_STATUS state=idle runs=0", body);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, sendText(fixture, SC_CMD_TEST_STATUS, "now",
                                             false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", body);
  stopTestService(&fixture);
}

void test_test_status_reports_sequence_drive_progress_and_last_result(void) {
  test_service_t fixture;
  startTestService(&fixture);
  char body[HAL_COMMAND_RESPONSE_BUFFER_SIZE + 1u] = {};

  s_fake.status.active = "alpha";
  s_fake.status.source = SC_TEST_SOURCE_SC;
  s_fake.status.runs = 7u;
  s_fake.status.seq_index = 2u;
  s_fake.status.seq_count = 5u;
  s_fake.status.elapsed_ms = 1234u;
  s_fake.status.drive_valid = true;
  s_fake.status.demand_x10 = 905;
  s_fake.status.position_x10 = -3;
  s_fake.status.fields[0] = {SC_TEST_FIELD_PHASE, SC_TEST_PHASE_HOLD, 0};
  s_fake.status.fields[1] = {SC_TEST_FIELD_RATE, nullptr, 250};
  s_fake.status.field_count = 2u;
  s_fake.status.last = "beta";
  s_fake.status.result = SC_TEST_RESULT_HOST_LOST;
  TEST_ASSERT_EQUAL_INT(HAL_OK, sendText(fixture, SC_CMD_TEST_STATUS, nullptr,
                                         false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING(
      "SC_OK TEST_STATUS state=running runs=7 test=alpha src=sc "
      "elapsed_ms=1234 "
      "seq=2/5 demand_x10=905 position_x10=-3 phase=hold rate=250 "
      "last=beta result=host_lost",
      body);

  // Idle with a history; no drive reading while uncalibrated.
  s_fake.status = {};
  s_fake.status.last = "alpha";
  s_fake.status.runs = 8u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, sendText(fixture, SC_CMD_TEST_STATUS, nullptr,
                                         false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_STATUS state=idle runs=8 last=alpha "
                           "result=done",
                           body);
  stopTestService(&fixture);
}

void test_test_actions_require_authentication_and_map_refusals(void) {
  test_service_t fixture;
  startTestService(&fixture);
  char body[HAL_COMMAND_RESPONSE_BUFFER_SIZE + 1u] = {};

  // Without authentication nothing reaches the module.
  TEST_ASSERT_NOT_EQUAL(HAL_OK,
                        sendText(fixture, SC_CMD_TEST_SET, "alpha_rate 7",
                                 false, body, sizeof(body)));
  TEST_ASSERT_NOT_EQUAL(HAL_OK, sendText(fixture, SC_CMD_TEST_RUN, "alpha",
                                         false, body, sizeof(body)));
  TEST_ASSERT_NOT_EQUAL(HAL_OK, sendText(fixture, SC_CMD_TEST_STOP, nullptr,
                                         false, body, sizeof(body)));
  TEST_ASSERT_NOT_EQUAL(HAL_OK, sendText(fixture, SC_CMD_TEST_SKIP, nullptr,
                                         false, body, sizeof(body)));
  TEST_ASSERT_EQUAL_UINT(0u, s_fake.set_calls);
  TEST_ASSERT_EQUAL_STRING("", s_fake.last_run);
  TEST_ASSERT_EQUAL_UINT(0u, s_fake.stop_calls);
  TEST_ASSERT_EQUAL_UINT(0u, s_fake.skip_calls);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        sendText(fixture, SC_CMD_TEST_SET, "alpha_rate 7", true,
                                 body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_SET id=alpha_rate value=7", body);
  TEST_ASSERT_EQUAL_INT32(7, s_fake.alpha_rate);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        sendText(fixture, SC_CMD_TEST_SET, "alpha_rate 11",
                                 true, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING(
      "SC_BAD_REQUEST out_of_range id=alpha_rate min=1 max=10", body);
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT,
                        sendText(fixture, SC_CMD_TEST_SET, "missing 1", true,
                                 body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_INVALID_PARAM_ID id=missing", body);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        sendText(fixture, SC_CMD_TEST_SET, "alpha_rate x", true,
                                 body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_BAD_REQUEST value_not_int32", body);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        sendText(fixture, SC_CMD_TEST_SET, "alpha_rate", true,
                                 body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_BAD_REQUEST expected=SC_TEST_SET <id> <value>",
                           body);
  TEST_ASSERT_EQUAL_INT32(7, s_fake.alpha_rate);

  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        sendText(fixture, SC_CMD_TEST_RUN, SC_TEST_SEQUENCE,
                                 true, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_RUN name=all", body);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_SEQUENCE, s_fake.last_run);
  s_fake.run_status = HAL_EPERM;
  TEST_ASSERT_EQUAL_INT(HAL_EPERM, sendText(fixture, SC_CMD_TEST_RUN, "alpha",
                                            true, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_NOT_READY ENGINE_RUNNING", body);
  s_fake.run_status = HAL_EBUSY;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, sendText(fixture, SC_CMD_TEST_RUN, "alpha",
                                            true, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_NOT_READY BUSY", body);
  s_fake.run_status = HAL_ENOENT;
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, sendText(fixture, SC_CMD_TEST_RUN, "dtc",
                                             true, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_BAD_REQUEST unknown_test name=dtc", body);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, sendText(fixture, SC_CMD_TEST_RUN, nullptr,
                                             true, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_BAD_REQUEST expected=SC_TEST_RUN <test|all>",
                           body);

  TEST_ASSERT_EQUAL_INT(HAL_OK, sendText(fixture, SC_CMD_TEST_STOP, nullptr,
                                         true, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_OK TEST_STOP", body);
  s_fake.skip_status = HAL_EBUSY;
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, sendText(fixture, SC_CMD_TEST_SKIP, nullptr,
                                            true, body, sizeof(body)));
  TEST_ASSERT_EQUAL_STRING("SC_NOT_READY BUSY", body);
  TEST_ASSERT_EQUAL_UINT(1u, s_fake.stop_calls);
  TEST_ASSERT_EQUAL_UINT(1u, s_fake.skip_calls);
  stopTestService(&fixture);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_full_service_rolls_back_and_can_be_initialized_again);
  RUN_TEST(test_service_rejects_incomplete_write_and_invalid_source_config);
  RUN_TEST(test_service_preserves_foreign_command_entries);
  RUN_TEST(test_test_commands_are_registered_only_with_complete_operations);
  RUN_TEST(test_test_catalog_reads_need_no_authentication);
  RUN_TEST(test_test_status_reports_sequence_drive_progress_and_last_result);
  RUN_TEST(test_test_actions_require_authentication_and_map_refusals);
  return UNITY_END();
}
