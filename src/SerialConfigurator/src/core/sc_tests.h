#ifndef SC_TESTS_H
#define SC_TESTS_H

/**
 * @file sc_tests.h
 * @brief Host side of the ECU functional tests (SC_TEST_* commands).
 *
 * The module reports its tests, their runtime parameters and the progress
 * of the one that runs. Reads need no authentication. Setting parameters,
 * starting, skipping and stopping do: run @ref sc_core_authenticate first,
 * as for @ref sc_core_set_param.
 *
 * A test started here belongs to this session. The ECU stops it when the
 * session ends or when test traffic stops for a few seconds, so a caller
 * that starts one keeps polling @ref sc_tests_get_status every
 * SC_TESTS_POLL_INTERVAL_MS until it ends.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most tests the host keeps from one catalog. */
#define SC_TESTS_MAX 12u
/** Most runtime parameters of one test. */
#define SC_TESTS_PARAMS_MAX 8u
/** Most progress values in one status reply. */
#define SC_TESTS_FIELDS_MAX 8u
/** Buffer for short wire tokens: units, keys, results, sources. */
#define SC_TESTS_TOKEN_MAX 24u

typedef struct ScTestParam {
  char id[SC_TEST_ID_MAX];
  char test[SC_TEST_ID_MAX];
  char unit[SC_TESTS_TOKEN_MAX];
  int32_t value; /**< In force now; the default again after an ECU reset. */
  int32_t min;
  int32_t max;
  int32_t default_value;
} ScTestParam;

typedef struct ScTestEntry {
  char name[SC_TEST_ID_MAX];
  bool in_sequence; /**< Part of the sequence started by SC_TEST_SEQUENCE. */
  size_t param_count;
  ScTestParam params[SC_TESTS_PARAMS_MAX];
} ScTestEntry;

typedef struct ScTestCatalog {
  /** False when the firmware has no SC_TEST_* commands at all. */
  bool supported;
  size_t count;
  ScTestEntry tests[SC_TESTS_MAX];
} ScTestCatalog;

/** One progress value: a number, or a text token when @c is_text. */
typedef struct ScTestField {
  char key[SC_TESTS_TOKEN_MAX];
  bool is_text;
  char text[SC_TESTS_TOKEN_MAX];
  int32_t value;
} ScTestField;

typedef struct ScTestStatus {
  bool running;
  uint32_t runs; /**< Tests started since the ECU booted. */
  char test[SC_TEST_ID_MAX];
  char source[SC_TESTS_TOKEN_MAX]; /**< SC_TEST_SOURCE_SC or _CONSOLE. */
  uint32_t elapsed_ms;
  unsigned seq_index; /**< 1-based position in a running sequence, else 0. */
  unsigned seq_count;
  bool drive_valid;     /**< Demand and position below were reported. */
  int32_t demand_x10;   /**< Demanded position, percent of stroke x10. */
  int32_t position_x10; /**< Measured position, percent of stroke x10. */
  size_t field_count;
  ScTestField fields[SC_TESTS_FIELDS_MAX];
  bool has_last;
  char last[SC_TEST_ID_MAX];
  char result[SC_TESTS_TOKEN_MAX];
} ScTestStatus;

typedef enum ScTestActionStatus {
  SC_TEST_ACTION_OK = 0,
  SC_TEST_ACTION_ERR_NULL_ARG,
  SC_TEST_ACTION_ERR_TRANSPORT,
  SC_TEST_ACTION_ERR_NOT_AUTHORIZED,
  SC_TEST_ACTION_ERR_UNKNOWN_TEST,
  SC_TEST_ACTION_ERR_INVALID_PARAM,
  SC_TEST_ACTION_ERR_OUT_OF_RANGE,
  SC_TEST_ACTION_ERR_BUSY,
  SC_TEST_ACTION_ERR_ENGINE_RUNNING,
  SC_TEST_ACTION_ERR_UNEXPECTED_REPLY
} ScTestActionStatus;

/** @brief Stable token for @p status, for logs and CLI output. */
const char *sc_test_action_status_name(ScTestActionStatus status);

/** @brief Decode "SC_OK TEST_LIST count=<n> names=<a,b>" into test names. */
bool sc_tests_parse_list(const ScCommandResult *result, ScTestCatalog *out,
                         char *error, size_t error_size);

/** @brief Decode "SC_OK TEST_INFO ..." into @p entry: sequence flag and the
 *         ids of its parameters (their details come from TEST_PARAM). */
bool sc_tests_parse_info(const ScCommandResult *result, ScTestEntry *entry,
                         char *error, size_t error_size);

/** @brief Decode "SC_OK TEST_PARAM ..." into @p param. */
bool sc_tests_parse_param(const ScCommandResult *result, ScTestParam *param,
                          char *error, size_t error_size);

/** @brief Decode "SC_OK TEST_STATUS ..."; unknown keys become progress
 *         fields, so new ones need no host change to be shown. */
bool sc_tests_parse_status(const ScCommandResult *result, ScTestStatus *out,
                           char *error, size_t error_size);

/**
 * @brief Read the whole catalog: the test list, each test's details and
 *        each parameter's range and value.
 *
 * A firmware without the commands (SC_UNKNOWN_CMD) gives a successful load
 * with @c supported false; a production ECU gives @c supported true and no
 * tests. Returns false on a transport or reply error.
 */
bool sc_tests_load_catalog(ScCore *core, size_t module_index,
                           ScTestCatalog *out, char *error, size_t error_size);

/** @brief Read the status of the running or last test. */
bool sc_tests_get_status(ScCore *core, size_t module_index, ScTestStatus *out,
                         char *error, size_t error_size);

/** @brief Value of progress @p key, or false when the status has none. */
bool sc_tests_status_field(const ScTestStatus *status, const char *key,
                           int32_t *out_value);

/** @brief Set one test parameter; it holds until the ECU resets. */
ScTestActionStatus sc_tests_set_param(const ScTransport *transport,
                                      const char *device_path, const char *id,
                                      int32_t value, char *error,
                                      size_t error_size);

/** @brief Start one test, or the sequence with SC_TEST_SEQUENCE. */
ScTestActionStatus sc_tests_run(const ScTransport *transport,
                                const char *device_path, const char *name,
                                char *error, size_t error_size);

/** @brief Stop the running test and any sequence. */
ScTestActionStatus sc_tests_stop(const ScTransport *transport,
                                 const char *device_path, char *error,
                                 size_t error_size);

/** @brief Stop the running test; a sequence moves on to its next test. */
ScTestActionStatus sc_tests_skip(const ScTransport *transport,
                                 const char *device_path, char *error,
                                 size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* SC_TESTS_H */
