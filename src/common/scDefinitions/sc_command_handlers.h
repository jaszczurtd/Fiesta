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
  void *user;
  /** Must be exactly the Serial Session source mask. */
  hal_command_source_mask_t allowed_sources;
} sc_command_service_config_t;

typedef struct {
  sc_command_service_config_t config;
  hal_command_router_t router;
  uint16_t registered_commands;
  bool reboot_pending;
  bool initialized;
} sc_command_service_t;

/**
 * @brief Register the SerialConfigurator commands supported by one module.
 *
 * Read commands are always installed. SET/COMMIT/REVERT are installed when
 * staging storage and both write callbacks are present. GPS is installed when
 * @c read_gps is present. Bootloader reboot is always installed and requires
 * an authenticated request. The current service accepts only
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

#ifdef __cplusplus
}
#endif
