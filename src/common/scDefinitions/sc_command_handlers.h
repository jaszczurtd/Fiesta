#pragma once

/**
 * @file sc_command_handlers.h
 * @brief Fiesta command-router handlers for the SerialConfigurator surface.
 *
 * The serial adapter owns framing, session state and authentication metadata.
 * This module registers the exact Fiesta SC command names, parses their text
 * arguments and writes the existing wire replies into a router response.
 */

#include "sc_param_types.h"

#include <hal/commands/hal_command_router.h>
#include <hal/serial/hal_serial_commands.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool available;
  int32_t lat_e6;
  int32_t lon_e6;
  int16_t speed_kmh_x10;
  uint32_t epoch;
} sc_command_gps_snapshot_t;

/** @brief One functional test offered to the configurator. */
typedef struct {
  const char *name;   /**< Wire name, at most SC_TEST_ID_MAX - 1 chars. */
  bool in_sequence;   /**< Part of the sequence started by "all". */
  size_t param_count; /**< Runtime parameters of this test. */
} sc_command_test_info_t;

/** @brief One runtime parameter of a functional test. */
typedef struct {
  const char *id;   /**< Wire id, unique across all tests. */
  const char *test; /**< Wire name of the owning test. */
  const char *unit; /**< Unit token: count, s, ms or pct_per_s. */
  int32_t value;    /**< Value in force; the default after a restart. */
  int32_t min;
  int32_t max;
  int32_t default_value;
} sc_command_test_param_t;

/** @brief One progress value of the running test: a number, or a text token
 * when @c text is non-NULL. */
typedef struct sc_command_test_field_s {
  const char *key; /**< Wire key; static storage. */
  const char *text;
  int32_t value;
} sc_command_test_field_t;

/** @brief Most progress keys a single test reports. */
#define SC_COMMAND_TEST_FIELDS_MAX 6u

/** @brief Snapshot behind the SC_TEST_STATUS reply. */
typedef struct {
  const char *active;   /**< Running test, NULL when idle. */
  const char *source;   /**< "sc" or "console" while a test runs. */
  uint32_t runs;        /**< Tests started since boot, one-shots included. */
  uint8_t seq_index;    /**< 1-based position in a running sequence, else 0. */
  uint8_t seq_count;    /**< Tests in that sequence. */
  uint32_t elapsed_ms;  /**< Time since the running test started. */
  bool drive_valid;     /**< Demand and position below are meaningful. */
  int32_t demand_x10;   /**< Demanded position, percent of stroke x10. */
  int32_t position_x10; /**< Measured position, percent of stroke x10. */
  sc_command_test_field_t fields[SC_COMMAND_TEST_FIELDS_MAX];
  size_t field_count;
  const char *last;   /**< Most recent finished test, NULL before the first. */
  const char *result; /**< Its result token, e.g. done, ok, failed, stopped. */
} sc_command_test_status_t;

/**
 * @brief Functional tests a module exposes. Every callback is required and
 * runs on the core that polls the serial session; the module hands state
 * changes to the core that owns the actuator.
 */
typedef struct sc_command_test_ops_s {
  size_t (*count)(void *user);
  bool (*info)(void *user, size_t index, sc_command_test_info_t *out);
  bool (*param_at)(void *user, size_t test_index, size_t param_index,
                   sc_command_test_param_t *out);
  /** @return true when @p id names a parameter; fills @p out. */
  bool (*param)(void *user, const char *id, sc_command_test_param_t *out);
  /** @return HAL_OK, HAL_ENOENT for an unknown id, HAL_EINVAL out of range. */
  hal_status_t (*set_param)(void *user, const char *id, int32_t value);
  /** @param name Test name or SC_TEST_SEQUENCE.
   *  @return HAL_OK when queued, HAL_ENOENT for an unknown test, HAL_EPERM
   *  when the engine-speed interlock refuses, HAL_EBUSY while an earlier
   *  request waits. */
  hal_status_t (*run)(void *user, const char *name);
  /** @return HAL_OK when queued, HAL_EBUSY while an earlier request waits. */
  hal_status_t (*stop)(void *user);
  hal_status_t (*skip)(void *user);
  void (*status)(void *user, sc_command_test_status_t *out);
} sc_command_test_ops_t;

typedef void (*sc_command_refresh_fn)(void *user);
typedef void (*sc_command_set_applied_fn)(void *user);
typedef bool (*sc_command_writes_ready_fn)(void *user);
typedef hal_status_t (*sc_command_commit_fn)(void *user,
                                             const char **out_reason,
                                             size_t *out_count);
typedef void (*sc_command_revert_fn)(void *user);
typedef void (*sc_command_gps_fn)(void *user,
                                  sc_command_gps_snapshot_t *out_snapshot);

typedef struct {
  const char *module_token;
  const char *firmware_version;
  const char *build_id;
  const sc_param_descriptor_t *params;
  size_t param_count;
  const void *active_values;
  void *staging_values;
  sc_command_refresh_fn refresh;
  sc_command_set_applied_fn set_applied;
  /** Optional gate for SET/COMMIT/REVERT while module state is recovering. */
  sc_command_writes_ready_fn writes_ready;
  sc_command_commit_fn commit;
  sc_command_revert_fn revert;
  sc_command_gps_fn read_gps;
  /** Optional functional tests; NULL leaves the SC_TEST_* commands out. */
  const sc_command_test_ops_t *tests;
  void *user;
  /** Must be exactly the Serial Session source mask. */
  hal_command_source_mask_t allowed_sources;
} sc_command_service_config_t;

typedef struct {
  sc_command_service_config_t config;
  hal_command_router_t router;
  uint32_t registered_commands;
  bool reboot_pending;
  bool initialized;
} sc_command_service_t;

/**
 * @brief SerialConfigurator session of one firmware module.
 *
 * Holds the serial session, the SC command service and the adapter that
 * routes framed SC commands from the session to the service. Zero-initialize
 * before the first sc_config_session_start().
 */
typedef struct {
  hal_serial_session_t session;
  sc_command_service_t service;
  hal_serial_commands_t commands;
} sc_config_session_t;

/**
 * @brief Register the SerialConfigurator commands supported by one module.
 *
 * Read commands are always installed. SET/COMMIT/REVERT are installed when
 * staging storage and both write callbacks are present. GPS is installed when
 * @c read_gps is present. The SC_TEST_* commands are installed when @c tests
 * is present: list, info, param and status are reads, set, run, stop and skip
 * require an authenticated request. Bootloader reboot is always installed and
 * requires an authenticated request. The current service accepts only
 * @c HAL_COMMAND_SOURCE_SERIAL_SESSION because its mutable state and deferred
 * reboot sequencing are tied to the synchronous serial path. Zero-initialize
 * @p service before its first use.
 */
hal_status_t sc_command_service_init(sc_command_service_t *service,
                                     hal_command_router_t router,
                                     const sc_command_service_config_t *config);

/** @brief Remove every command registered by this service. */
hal_status_t sc_command_service_deinit(sc_command_service_t *service);

/** @brief Return the router selected during service initialization. */
hal_command_router_t
sc_command_service_router(const sc_command_service_t *service);

/** @brief Format router lookup and policy failures with Fiesta SC tokens. */
hal_status_t
sc_command_format_serial_response(const hal_command_request_t *request,
                                  const hal_command_response_t *response,
                                  char *output, size_t output_capacity,
                                  size_t *out_length, void *user);

/** @brief Admit only the historical pre-HELLO reboot request. */
bool sc_command_allow_inactive_reboot(const hal_command_request_t *request,
                                      void *user);

/** @brief Preserve the historical `ERR UNKNOWN` reply for non-SC payloads. */
void sc_command_reply_legacy_unknown(const char *line, void *session_user);

/**
 * @brief Execute a reboot requested by the router after its reply was sent.
 *
 * Call immediately after the Serial Session adapter finishes polling. The
 * pending flag is consumed before entering the bootloader.
 */
void sc_command_service_process_deferred(sc_command_service_t *service);

/**
 * @brief Detach the adapter and the service left by a previous start.
 *
 * Call before sc_config_session_start() when the session is started again.
 * A failure is logged as "<log_name> SC adapter/service detach failed" and
 * leaves the session untouched.
 *
 * @param session Session to stop; parts that were never started are skipped.
 * @param log_name Module name used in the log, e.g. "ECU".
 * @return HAL_OK, HAL_EINVAL for NULL arguments, or the detach error.
 */
hal_status_t sc_config_session_stop(sc_config_session_t *session,
                                    const char *log_name);

/**
 * @brief Start the serial session, the SC command service and the adapter.
 *
 * The HELLO identity (module token, firmware version, build id) comes from
 * @p config, so a module states it once. The adapter selects "SC_" commands,
 * formats router failures with Fiesta tokens and admits only the pre-HELLO
 * reboot request. On failure the service is detached again and the error is
 * logged as "<log_name> SC adapter init failed".
 *
 * @param session Stopped or zero-initialized session.
 * @param config Service configuration; its strings must outlive the session.
 * @param fallback Handler for payloads that are not SC commands; NULL keeps
 *        the historical `ERR UNKNOWN` reply.
 * @param fallback_user Argument for @p fallback; ignored when it is NULL.
 * @param log_name Module name used in the log, e.g. "ECU".
 * @return HAL_OK, HAL_EINVAL for NULL arguments, or the init error.
 */
hal_status_t sc_config_session_start(sc_config_session_t *session,
                                     const sc_command_service_config_t *config,
                                     hal_serial_session_unknown_cb_t fallback,
                                     void *fallback_user, const char *log_name);

/**
 * @brief Poll the serial session and run a reboot deferred by a command.
 * @return Whether a host session is active after the poll; false for NULL.
 */
bool sc_config_session_poll(sc_config_session_t *session);

#ifdef __cplusplus
}
#endif
