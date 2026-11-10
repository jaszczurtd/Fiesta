#ifndef FIESTA_TEST_RUNNER_H
#define FIESTA_TEST_RUNNER_H

#include "../scDefinitions/sc_command_handlers.h"
#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @file test_runner.h
 * @brief One functional-test runner per firmware, shared by two core roles.
 *
 * Initialize before either core starts using the runner. The controller role
 * calls start/skip/stop/tick and owns the test state and output callbacks.
 * The session role calls console/events/SC operations/session_ended. Either
 * physical core may serve either role; neither role may have two callers.
 * Parameter reads/writes are atomic. Configuration and strings live for the
 * firmware lifetime. Initialization requires both roles to be quiescent.
 *
 * Console tests run unsupervised until the console arms the watch with
 * "watch <ms>": from then on every console line counts as host contact, and a
 * console test whose host stays silent for the limit stops with host_lost,
 * releasing the module output. "watch" alone renews and reports the limit,
 * "watch 0" disarms it; initialization leaves it off. SC tests keep their own
 * keepalive.
 */

/** @brief Reserved identifier for no active test. */
#define FIESTA_TEST_NONE 0U
/** @brief Reserved identifier for the console sequence. */
#define FIESTA_TEST_ALL UINT32_MAX
/** @brief Console buffer size, including the terminating NUL. */
#define FIESTA_TEST_COMMAND_SIZE 64U
/** @brief Smallest console watch limit "watch <ms>" accepts [ms]. */
#define FIESTA_TEST_WATCH_MIN_MS 1000U
/** @brief Largest console watch limit "watch <ms>" accepts [ms]. */
#define FIESTA_TEST_WATCH_MAX_MS 600000U
/** @brief Console highlight start, also used by module-specific help. */
#define TEST_HIGHLIGHT_ON                                                      \
  "\033"                                                                       \
  "[33m"
/** @brief Console highlight reset. */
#define TEST_HIGHLIGHT_OFF                                                     \
  "\033"                                                                       \
  "[0m"

/** @brief One test; IDs must be unique and exclude NONE and ALL. */
typedef struct {
  uint32_t id;         /**< Module identifier. */
  const char *name;    /**< Console and SC name. */
  const char *summary; /**< Console description. */
  bool drivesDemand;   /**< Owns the module output while active. */
  bool sequenced;      /**< Included in run all. */
  bool scSupported;    /**< Offered to SerialConfigurator. */
  void (*start)(
      void); /**< Optional initializer; with no step, runs on session role. */
  float (*step)(bool *outFinished); /**< Controller step; NULL is a one-shot. */
  const char *(*result)(void);
  /**< Completion token; NULL means done. Runs on the session role after a
   * one-shot start, otherwise on the controller role after the final step. */
  size_t (*progress)(sc_command_test_field_t *fields, size_t capacity);
  /**< Optional controller-role status fields; must respect capacity. */
} fiesta_test_t;

/** @brief Runtime parameter metadata; storage is supplied separately. */
typedef struct {
  const char *id;   /**< Wire name. */
  uint32_t test;    /**< Owning test ID; FIESTA_TEST_ALL lists it with every
                         test and names the owner SC_TEST_SEQUENCE. */
  const char *unit; /**< Wire unit token. */
  int32_t min;      /**< Minimum accepted value. */
  int32_t max;      /**< Maximum accepted value. */
  int32_t def;      /**< Initial value. */
} fiesta_test_param_t;

/** @brief Optional module output measurement for the existing SC protocol. */
typedef struct {
  bool valid;           /**< Whether both values are available. */
  int32_t demand_x10;   /**< Demand in tenths of a percent. */
  int32_t position_x10; /**< Position in tenths of a percent. */
} fiesta_test_drive_t;

/** @brief Module registry and policy. Callbacks other than suite are optional.
 */
typedef struct {
  const fiesta_test_t *(*suite)(size_t *count);
  /**< Immutable registry and length; callable from either role. The accessor
   * allows the module configuration itself to remain in read-only storage. */
  const fiesta_test_param_t *params; /**< Immutable parameter metadata. */
  int32_t *values;                   /**< One aligned word per parameter. */
  size_t param_count;                /**< Parameter/storage length. */
  uint32_t keepalive_ms;             /**< SC inactivity limit; nonzero. */
  void (*reset)(void); /**< Startup reset, before either role runs. */
  void (*prepare)(const fiesta_test_t *entry); /**< Controller, before start. */
  void (*cleanup)(const fiesta_test_t *entry, const char *result, bool restart);
  /**< Controller, every exit including same-test restart. A continuous-output
   * module may retain its output on restart; all other exits must release it.
   */
  void (*apply_output)(float value); /**< Controller, after a driving step. */
  void (*read_drive)(
      fiesta_test_drive_t *out); /**< Controller, status snapshot. */
  const char *(*sc_blocked)(
      void); /**< Either role; NULL permits SC operation. */
  bool (*command)(const char *line, uint32_t *requested);
  /**< Controller, module command; optionally requests a test ID. */
  void (*help)(void);   /**< Controller, module console help. */
  void (*events)(void); /**< Session, module events after lifecycle/fixtures. */
} fiesta_test_config_t;

/** @brief Initialize the singleton with lifetime configuration.
 * @param config Registry, storage and callbacks.
 * @return HAL_OK, HAL_EINVAL for invalid configuration, HAL_ENOMEM on mutex
 * failure.
 */
hal_status_t fiesta_test_init(const fiesta_test_config_t *config);
/** @brief Start a test or ALL; NONE stops. Controller role.
 * @param id Registry ID or reserved action.
 * @return HAL_OK, HAL_EUNINIT or HAL_EINVAL for an unknown ID.
 */
hal_status_t fiesta_test_start(uint32_t id);
/** @brief Skip the active test, continuing a sequence. Controller role.
 * @return HAL_OK or HAL_EUNINIT.
 */
hal_status_t fiesta_test_skip(void);
/** @brief Stop the active test and sequence. Controller role.
 * @return HAL_OK or HAL_EUNINIT.
 */
hal_status_t fiesta_test_stop(void);
/** @brief Apply queued requests and execute one controller step.
 * @return Whether this tick belonged to a test owning the module output.
 */
bool fiesta_test_tick(void);
/** @brief Queue a console line. Session role.
 * @param line NUL-terminated text; invalid/busy requests report console errors.
 */
void fiesta_test_console(const char *line);
/** @brief Drain events and run pending one-shot fixtures. Session role. */
void fiesta_test_events(void);
/** @brief Active name, or NULL; controller role only.
 * @return Registry string, valid for the firmware lifetime.
 */
const char *fiesta_test_active_name(void);
/** @brief Active ID, or NONE; controller role only.
 * @return Current registry ID.
 */
uint32_t fiesta_test_active_id(void);
/**
 * @brief Report that the configurator session ended; session role.
 * @note A configurator test whose run request was queued before this call
 * stops with session_end at the next tick. A request queued afterwards, or
 * refused as busy, does not undo it.
 */
void fiesta_test_session_ended(void);
/** @brief SC operations for the session role; their user argument is ignored.
 * @return Static operations table.
 */
const sc_command_test_ops_t *fiesta_test_sc_ops(void);
/** @brief Parameter metadata, or NULL for an invalid index.
 * @param index Parameter index.
 * @return Registry entry, valid for the firmware lifetime.
 */
const fiesta_test_param_t *fiesta_test_param_desc(size_t index);
/** @brief Atomic parameter read from either role.
 * @param index Parameter index.
 * @return Value, or zero for an invalid index.
 */
int32_t fiesta_test_param(size_t index);
/** @brief Atomic parameter write from either role.
 * @param index Parameter index.
 * @param value New value within the declared range.
 * @return HAL_OK, HAL_ENOENT for an invalid index, HAL_EINVAL for an invalid
 * value.
 */
hal_status_t fiesta_test_param_set(size_t index, int32_t value);
/** @brief Restore parameter defaults, atomically per value, from either role.
 */
void fiesta_test_params_reset(void);

#ifdef __cplusplus
}
#endif
#endif
