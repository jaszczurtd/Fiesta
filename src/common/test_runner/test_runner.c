/* Functional-test transport and lifecycle, independent of module outputs. */
#include "test_runner.h"
#include "../scDefinitions/sc_protocol.h"
#include "../seqlock.h"
#include <hal/core/hal_mutex_once.h>
#include <stdlib.h>
#include <string.h>

static const fiesta_test_config_t s_emptyConfig = {0};
static const fiesta_test_config_t *s_config = &s_emptyConfig;
static const fiesta_test_t *findById(uint32_t id);

static const fiesta_test_t *configuredSuite(size_t *count) {
  *count = 0U;
  return s_config->suite != NULL ? s_config->suite(count) : NULL;
}

// Console command queue
//=============================================================================

static hal_mutex_t s_commandMutex = NULL;
static char s_command[FIESTA_TEST_COMMAND_SIZE];
/* Written under the mutex, read without it by the controller core's poll: the
 * loop that releases every control step must not take a lock to learn that
 * nothing waits. */
static uint8_t s_commandLength;
typedef struct {
  const fiesta_test_t *entry; /* NULL for a sequence or idle message. */
  const char *result; /* NULL means started; otherwise stopped + result. */
} tests_lifecycle_event_t;
/* Controller core publishes; session core drains without holding up position
 * control. */
static tests_lifecycle_event_t s_lifecycleEvents[4];
static uint32_t s_lifecycleWritten;
static uint32_t s_lifecycleRead;
static uint32_t s_lifecycleDropped;

static void recordLifecycle(const fiesta_test_t *entry, const char *result) {
  if (hal_debug_is_muted()) {
    return;
  }
  const uint32_t written =
      HAL_ATOMIC_LOAD(&s_lifecycleWritten, HAL_ATOMIC_RELAXED);
  const uint32_t read = HAL_ATOMIC_LOAD(&s_lifecycleRead, HAL_ATOMIC_ACQUIRE);
  if ((written - read) >= COUNTOF(s_lifecycleEvents)) {
    (void)HAL_ATOMIC_FETCH_ADD(&s_lifecycleDropped, 1U, HAL_ATOMIC_RELAXED);
    return;
  }
  s_lifecycleEvents[written % COUNTOF(s_lifecycleEvents)] =
      (tests_lifecycle_event_t){.entry = entry, .result = result};
  HAL_ATOMIC_STORE(&s_lifecycleWritten, written + 1U, HAL_ATOMIC_RELEASE);
}

static void printLifecycle(void) {
  if (hal_debug_is_muted()) {
    return;
  }
  for (size_t i = 0U; i < COUNTOF(s_lifecycleEvents); ++i) {
    const uint32_t read = HAL_ATOMIC_LOAD(&s_lifecycleRead, HAL_ATOMIC_RELAXED);
    if (read == HAL_ATOMIC_LOAD(&s_lifecycleWritten, HAL_ATOMIC_ACQUIRE)) {
      break;
    }
    const tests_lifecycle_event_t event =
        s_lifecycleEvents[read % COUNTOF(s_lifecycleEvents)];
    HAL_ATOMIC_STORE(&s_lifecycleRead, read + 1U, HAL_ATOMIC_RELEASE);
    if (event.entry == NULL) {
      deb("TEST: %s", event.result);
    } else if (event.result == NULL) {
      deb("TEST: %s started", event.entry->name);
    } else {
      deb("TEST: %s stopped", event.entry->name);
      deb("TEST: %s result: %s", event.entry->name, event.result);
    }
  }
  const uint32_t dropped =
      HAL_ATOMIC_EXCHANGE(&s_lifecycleDropped, 0U, HAL_ATOMIC_ACQ_REL);
  if (dropped != 0U) {
    derr("TEST: lifecycle log overflow: %lu", (unsigned long)dropped);
  }
}

/* One-shot fixtures run on session core. A new publication cancels an unclaimed
 * request; the completion publishes its result back to the control core. */
static fiesta_seqlock_t s_fixtureLock;
static uint32_t s_fixtureId;
static uint32_t s_fixtureCompleted;

static void recordFixture(const fiesta_test_t *entry) {
  const uint32_t begun = fiestaSeqlockWriteBegin(&s_fixtureLock);
  HAL_ATOMIC_STORE(&s_fixtureId, entry != NULL ? entry->id : FIESTA_TEST_NONE,
                   HAL_ATOMIC_RELAXED);
  fiestaSeqlockWriteEnd(&s_fixtureLock, begun);
}

static void runPendingFixture(void) {
  for (uint32_t attempt = 0U; attempt < FIESTA_SEQLOCK_READ_ATTEMPTS;
       ++attempt) {
    const uint32_t sequence = fiestaSeqlockReadBegin(&s_fixtureLock);
    const uint32_t id = HAL_ATOMIC_LOAD(&s_fixtureId, HAL_ATOMIC_RELAXED);
    if (!fiestaSeqlockReadConsistent(&s_fixtureLock, sequence)) {
      continue;
    }
    const fiesta_test_t *entry = findById(id);
    if ((entry != NULL) && (sequence != HAL_ATOMIC_LOAD(&s_fixtureCompleted,
                                                        HAL_ATOMIC_RELAXED))) {
      // Fixture publication follows its start event on the control core.
      printLifecycle();
      entry->start();
      HAL_ATOMIC_STORE(&s_fixtureCompleted, sequence, HAL_ATOMIC_RELEASE);
    }
    return;
  }
}

//=============================================================================
// Runtime parameters
//=============================================================================

const fiesta_test_param_t *fiesta_test_param_desc(size_t param) {
  return (param < s_config->param_count) ? &s_config->params[param] : NULL;
}

int32_t fiesta_test_param(size_t param) {
  return (param < s_config->param_count)
             ? HAL_ATOMIC_LOAD(&s_config->values[param], HAL_ATOMIC_ACQUIRE)
             : 0;
}

/**
 * @brief Set one runtime parameter; safe from either core.
 * @param param Parameter below s_config->param_count.
 * @param value New value inside the parameter's range.
 * @return HAL_OK, HAL_ENOENT for an index out of range or HAL_EINVAL for a
 * value outside the range, which leaves the old value in force.
 * @note A running test sees the new value the next time it reads it.
 */
hal_status_t fiesta_test_param_set(size_t param, int32_t value) {
  const fiesta_test_param_t *desc = fiesta_test_param_desc(param);
  if (desc == NULL) {
    return HAL_ENOENT;
  }
  if ((value < desc->min) || (value > desc->max)) {
    return HAL_EINVAL;
  }
  HAL_ATOMIC_STORE(&s_config->values[param], value, HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

/**
 * @brief Find a runtime parameter by its wire id.
 * @param id NUL-terminated id; NULL finds nothing.
 * @param outParam Non-NULL; receives the parameter when found.
 * @return True when @p id names a parameter.
 */
static bool paramFind(const char *id, size_t *outParam) {
  for (size_t i = 0U; (id != NULL) && (i < s_config->param_count); i++) {
    if (strcmp(id, s_config->params[i].id) == 0) {
      *outParam = (size_t)i;
      return true;
    }
  }
  return false;
}

/** @brief Put every runtime parameter back to its default. */
void fiesta_test_params_reset(void) {
  for (size_t i = 0U; i < s_config->param_count; i++) {
    HAL_ATOMIC_STORE(&s_config->values[i], s_config->params[i].def,
                     HAL_ATOMIC_RELEASE);
  }
}

//=============================================================================
// Lifecycle
//=============================================================================

/**
 * @brief Reset every generator and parameter to its built-in default.
 * @return True when the command queue is ready.
 */
static bool reset(void) {
  HAL_ATOMIC_STORE(&s_commandLength, 0U, HAL_ATOMIC_RELEASE);
  s_command[0] = '\0';
  HAL_ATOMIC_STORE(&s_lifecycleWritten, 0U, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_lifecycleRead, 0U, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_lifecycleDropped, 0U, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_fixtureLock.sequence, 0U, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_fixtureId, FIESTA_TEST_NONE, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&s_fixtureCompleted, 0U, HAL_ATOMIC_RELAXED);

  fiesta_test_params_reset();
  if (s_config->reset != NULL) {
    s_config->reset();
  }

  return jh_hal_mutex_try_create_once(&s_commandMutex) != NULL;
}

/**
 * @brief Queue one console line for the controller core.
 * @param line NUL-terminated line; must be shorter than
 * FIESTA_TEST_COMMAND_SIZE.
 * @return HAL_OK, HAL_EINVAL when the line is empty or too long, or HAL_EBUSY
 * while an earlier line waits to be applied.
 */
static hal_status_t queueCommand(const char *line) {
  if ((line == NULL) || (line[0] == '\0')) {
    return HAL_EINVAL;
  }
  const size_t length = strlen(line);
  if (length >= COUNTOF(s_command)) {
    return HAL_EINVAL;
  }
  if (jh_hal_mutex_try_create_once(&s_commandMutex) == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_commandMutex);
  const bool busy = s_commandLength != 0U;
  if (!busy) {
    (void)memcpy(s_command, line, length + 1U);
    HAL_ATOMIC_STORE(&s_commandLength, (uint8_t)length, HAL_ATOMIC_RELEASE);
  }
  hal_mutex_unlock(s_commandMutex);
  return busy ? HAL_EBUSY : HAL_OK;
}

/**
 * @brief Take the queued console line, if any.
 * @param out Non-NULL destination of at least FIESTA_TEST_COMMAND_SIZE bytes.
 * @return True when a line was copied out and the queue is free again.
 */
static bool takeCommand(char *out) {
  if ((out == NULL) ||
      (HAL_ATOMIC_LOAD(&s_commandLength, HAL_ATOMIC_ACQUIRE) == 0U) ||
      (jh_hal_mutex_try_create_once(&s_commandMutex) == NULL)) {
    return false;
  }
  hal_mutex_lock(s_commandMutex);
  const uint8_t length = s_commandLength;
  if (length != 0U) {
    (void)memcpy(out, s_command, (size_t)length + 1U);
    HAL_ATOMIC_STORE(&s_commandLength, 0U, HAL_ATOMIC_RELEASE);
  }
  hal_mutex_unlock(s_commandMutex);
  return length != 0U;
}

/**
 * @brief Uppercase one ASCII letter.
 * @param value Character to fold.
 * @return Uppercase letter, or the character unchanged.
 */
static char asciiUpper(char value) {
  return ((value >= 'a') && (value <= 'z')) ? (char)(value - ('a' - 'A'))
                                            : value;
}

/**
 * @brief Compare two NUL-terminated words without case.
 * @param text Left side, may be NULL.
 * @param word Right side, may be NULL.
 * @return True when both are non-NULL and equal.
 */
static bool equals(const char *text, const char *word) {
  if ((text == NULL) || (word == NULL)) {
    return false;
  }
  size_t i = 0U;
  while ((text[i] != '\0') && (word[i] != '\0')) {
    if (asciiUpper(text[i]) != asciiUpper(word[i])) {
      return false;
    }
    i++;
  }
  return (text[i] == '\0') && (word[i] == '\0');
}

//=============================================================================
// Runner: starts, sequences and supervises the suite of tests.c
//=============================================================================

/** @brief Who started the running test. */
typedef enum { TESTS_SOURCE_CONSOLE = 0, TESTS_SOURCE_SC } tests_source_t;

static bool s_initialized = false;
enum { TESTS_METADATA_LIST = 1U, TESTS_METADATA_PARAMS = 2U };
/* Queued and drained on the console core, outside the muted RX callback. */
static uint8_t s_consoleMetadata = 0U;
static void printList(void);
static void printParams(void);
static uint32_t s_active = FIESTA_TEST_NONE;
static tests_source_t s_activeSource = TESTS_SOURCE_CONSOLE;
static uint32_t s_activeStartedMs = 0U;
static bool s_sequenceRunning = false;
static bool s_sequenceScOnly = false; /* Sequence of configurator tests. */
/* init limits the registry to UINT8_MAX entries, including the end cursor. */
static uint8_t s_sequenceIndex = 0U;
static uint32_t s_lastTest = FIESTA_TEST_NONE;
static const char *s_lastResult = NULL;
static uint32_t s_runs = 0U; /* Tests started since boot. */

static const fiesta_test_t *findById(uint32_t id) {
  size_t total = 0U;
  const fiesta_test_t *suite = configuredSuite(&total);
  for (size_t i = 0U; i < total; i++) {
    if (suite[i].id == id) {
      return &suite[i];
    }
  }
  return NULL;
}

static const fiesta_test_t *findByName(const char *name) {
  size_t total = 0U;
  const fiesta_test_t *suite = configuredSuite(&total);
  for (size_t i = 0U; i < total; i++) {
    if (equals(name, suite[i].name)) {
      return &suite[i];
    }
  }
  return NULL;
}

/** @brief Whether an entry belongs to the sequence that is running. */
static bool inSequence(const fiesta_test_t *entry, bool scOnly) {
  return entry->sequenced && (!scOnly || entry->scSupported);
}

//=============================================================================
// Configurator requests and status, shared between the cores
//=============================================================================

/** @brief What the configurator asked the controller core to do. */
typedef enum {
  SC_REQUEST_NONE = 0,
  SC_REQUEST_RUN,
  SC_REQUEST_SEQUENCE,
  SC_REQUEST_STOP,
  SC_REQUEST_SKIP
} sc_request_kind_t;

/** @brief Status the controller core publishes for SC_TEST_STATUS. */
typedef struct {
  uint32_t active;
  uint32_t runs;
  uint32_t startedMs;
  int32_t demandX10;
  int32_t positionX10;
  sc_command_test_field_t fields[SC_COMMAND_TEST_FIELDS_MAX];
  size_t fieldCount;
  uint32_t last;
  const char *result;
  /* Group the small fields to avoid padding in the RP2040 RAM snapshot. */
  tests_source_t source;
  uint8_t seqIndex;
  uint8_t seqCount;
  bool driveValid;
} tests_published_t;

static hal_mutex_t s_scMutex = NULL;
/* Written under the mutex, polled without it like the console queue. */
static uint8_t s_scRequest = SC_REQUEST_NONE;
static uint32_t s_scRequestTest = FIESTA_TEST_NONE;
static tests_published_t s_published;
static uint32_t s_publishedMs = 0U;
/* Written by the session core, read by the controller core. */
static uint32_t s_scContactMs = 0U;
static bool s_scSessionEnded = false;

static bool scLock(void) {
  if (jh_hal_mutex_try_create_once(&s_scMutex) == NULL) {
    return false;
  }
  hal_mutex_lock(s_scMutex);
  return true;
}

static void scTouch(void) {
  HAL_ATOMIC_STORE(&s_scContactMs, hal_millis(), HAL_ATOMIC_RELEASE);
}

static hal_status_t scQueue(sc_request_kind_t kind, uint32_t test) {
  if (!scLock()) {
    return HAL_ENOMEM;
  }
  const bool busy = s_scRequest != SC_REQUEST_NONE;
  if (!busy) {
    s_scRequestTest = test;
    HAL_ATOMIC_STORE(&s_scRequest, (uint8_t)kind, HAL_ATOMIC_RELEASE);
  }
  hal_mutex_unlock(s_scMutex);
  return busy ? HAL_EBUSY : HAL_OK;
}

static sc_request_kind_t scTake(uint32_t *outTest) {
  if ((HAL_ATOMIC_LOAD(&s_scRequest, HAL_ATOMIC_ACQUIRE) == SC_REQUEST_NONE) ||
      !scLock()) {
    return SC_REQUEST_NONE;
  }
  const sc_request_kind_t kind = (sc_request_kind_t)s_scRequest;
  *outTest = s_scRequestTest;
  HAL_ATOMIC_STORE(&s_scRequest, SC_REQUEST_NONE, HAL_ATOMIC_RELEASE);
  hal_mutex_unlock(s_scMutex);
  return kind;
}

/** @brief Whether the configurator polled within the keepalive window. */
static bool scPolled(uint32_t *nowMs) {
  /* Read contact first: the other core may publish a newer millisecond. */
  const uint32_t contactMs =
      HAL_ATOMIC_LOAD(&s_scContactMs, HAL_ATOMIC_ACQUIRE);
  *nowMs = hal_millis();
  return !hal_elapsed_u32(*nowMs, contactMs, s_config->keepalive_ms);
}

/**
 * @brief Publish the status; @p force skips the refresh interval.
 * @note Starts and stops always publish. The periodic refresh of drive and
 * progress runs only while the configurator polls: nobody else reads it, and
 * it shares the loop that releases every control step.
 */
static void publishStatus(bool force) {
  uint32_t nowMs;
  const bool polled = scPolled(&nowMs);
  const uint32_t intervalMs = 20U; /* Refresh while nothing changes. */
  if (!force &&
      (!hal_elapsed_u32(nowMs, s_publishedMs, intervalMs) || !polled)) {
    return;
  }
  s_publishedMs = nowMs;
  tests_published_t next = {0};
  next.active = s_active;
  next.source = s_activeSource;
  next.runs = s_runs;
  next.startedMs = s_activeStartedMs;
  if (s_sequenceRunning && (s_active != FIESTA_TEST_NONE)) {
    size_t total = 0U;
    const fiesta_test_t *suite = configuredSuite(&total);
    for (size_t i = 0U; i < total; i++) {
      if (inSequence(&suite[i], s_sequenceScOnly)) {
        next.seqCount++;
        if (i < s_sequenceIndex) {
          next.seqIndex++;
        }
      }
    }
  }
  if (s_config->read_drive != NULL) {
    fiesta_test_drive_t drive = {0};
    s_config->read_drive(&drive);
    next.driveValid = drive.valid;
    next.demandX10 = drive.demand_x10;
    next.positionX10 = drive.position_x10;
  }
  const fiesta_test_t *entry = findById(s_active);
  if ((entry != NULL) && (entry->progress != NULL)) {
    next.fieldCount = entry->progress(next.fields, COUNTOF(next.fields));
  }
  next.last = s_lastTest;
  next.result = s_lastResult;
  if (scLock()) {
    s_published = next;
    hal_mutex_unlock(s_scMutex);
  }
}

//=============================================================================
// Activation
//=============================================================================

static void activate(const fiesta_test_t *entry, tests_source_t source) {
  s_active = entry->id;
  s_activeSource = source;
  s_activeStartedMs = hal_millis();
  s_runs++;
  // Announce before arming, so whatever the fixture prints reads as its own.
  recordLifecycle(entry, NULL);
  if (s_config->prepare != NULL) {
    s_config->prepare(entry);
  }
  if ((entry->step == NULL) && (entry->start != NULL)) {
    recordFixture(entry);
  } else if (entry->start != NULL) {
    entry->start();
  }
  publishStatus(true);
}

/** @brief Release module outputs and record the result on every exit. */
static void deactivate(const char *result) {
  const fiesta_test_t *entry = findById(s_active);
  if (entry == NULL) {
    s_active = FIESTA_TEST_NONE;
    return;
  }
  if (s_config->cleanup != NULL) {
    s_config->cleanup(entry, result, false);
  }
  s_active = FIESTA_TEST_NONE;
  recordFixture(NULL);
  s_lastTest = entry->id;
  s_lastResult = result;
  // Bench tools wait for the exact "stopped" line; the result follows it.
  recordLifecycle(entry, result);
  publishStatus(true);
}

/** @brief Start the next test of the sequence; ends it when none is left. */
static void advanceSequence(tests_source_t source) {
  size_t total = 0U;
  const fiesta_test_t *suite = configuredSuite(&total);
  while (s_sequenceIndex < total) {
    const fiesta_test_t *entry = &suite[s_sequenceIndex];
    s_sequenceIndex++;
    if (inSequence(entry, s_sequenceScOnly)) {
      activate(entry, source);
      return;
    }
  }
  s_sequenceRunning = false;
  recordLifecycle(NULL, "sequence finished");
  publishStatus(true);
}

static void beginSequence(bool scOnly, tests_source_t source) {
  deactivate(SC_TEST_RESULT_STOPPED);
  s_sequenceIndex = 0U;
  s_sequenceRunning = true;
  s_sequenceScOnly = scOnly;
  recordLifecycle(NULL, "sequence started");
  advanceSequence(source);
}

/** @brief Start one test from @p source, ending any running sequence. */
static void beginTest(const fiesta_test_t *entry, tests_source_t source) {
  if (s_active != entry->id) {
    deactivate(SC_TEST_RESULT_STOPPED);
  } else if (s_config->cleanup != NULL) {
    s_config->cleanup(entry, SC_TEST_RESULT_STOPPED, true);
  }
  s_sequenceRunning = false;
  activate(entry, source);
}

/** @brief Stop the running test and any sequence, recording why. */
static void stopAll(const char *result) {
  const bool wasActive = s_active != FIESTA_TEST_NONE;
  deactivate(result);
  s_sequenceRunning = false;
  if (!wasActive) {
    recordLifecycle(NULL, "none running");
  }
}

/** @brief Stop the running test; a running sequence moves to the next one. */
static void skipOne(void) {
  const bool wasActive = s_active != FIESTA_TEST_NONE;
  const tests_source_t source = s_activeSource;
  deactivate(SC_TEST_RESULT_STOPPED);
  if (s_sequenceRunning) {
    advanceSequence(source);
  } else if (!wasActive) {
    recordLifecycle(NULL, "none running");
  } else {
    // The stopped test already reported itself.
  }
}

//=============================================================================
// Public interface
//=============================================================================

hal_status_t fiesta_test_init(const fiesta_test_config_t *config) {
  if ((config == NULL) || (config->suite == NULL) ||
      (config->keepalive_ms == 0U) ||
      ((config->param_count != 0U) &&
       ((config->params == NULL) || (config->values == NULL)))) {
    return HAL_EINVAL;
  }
  size_t count = 0U;
  const fiesta_test_t *suite = config->suite(&count);
  if ((suite == NULL) || (count == 0U) || (count > UINT8_MAX)) {
    return HAL_EINVAL;
  }
  s_config = config;
  s_consoleMetadata = 0U;
  s_active = FIESTA_TEST_NONE;
  s_activeSource = TESTS_SOURCE_CONSOLE;
  s_sequenceRunning = false;
  s_sequenceScOnly = false;
  s_sequenceIndex = 0U;
  s_lastTest = FIESTA_TEST_NONE;
  s_lastResult = NULL;
  HAL_ATOMIC_STORE(&s_scRequest, SC_REQUEST_NONE, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&s_scSessionEnded, false, HAL_ATOMIC_RELEASE);
  /* No configurator is in contact until it calls in. */
  HAL_ATOMIC_STORE(&s_scContactMs, hal_millis() - s_config->keepalive_ms,
                   HAL_ATOMIC_RELEASE);
  s_initialized = reset() && (jh_hal_mutex_try_create_once(&s_scMutex) != NULL);
  if (s_initialized) {
    publishStatus(true);
  }
  return s_initialized ? HAL_OK : HAL_ENOMEM;
}

hal_status_t fiesta_test_start(uint32_t test) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  if (test == FIESTA_TEST_NONE) {
    return fiesta_test_stop();
  }
  if (test == FIESTA_TEST_ALL) {
    beginSequence(false, TESTS_SOURCE_CONSOLE);
    return HAL_OK;
  }
  const fiesta_test_t *entry = findById(test);
  if (entry == NULL) {
    return HAL_EINVAL;
  }
  beginTest(entry, TESTS_SOURCE_CONSOLE);
  return HAL_OK;
}

hal_status_t fiesta_test_skip(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  skipOne();
  return HAL_OK;
}

hal_status_t fiesta_test_stop(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  stopAll(SC_TEST_RESULT_STOPPED);
  return HAL_OK;
}

const char *fiesta_test_active_name(void) {
  const fiesta_test_t *entry = findById(s_active);
  return entry != NULL ? entry->name : NULL;
}

void fiesta_test_events(void) {
  printLifecycle();
  if (!hal_debug_is_muted()) {
    const uint32_t metadata = s_consoleMetadata;
    s_consoleMetadata = 0U;
    if ((metadata & TESTS_METADATA_LIST) != 0U) {
      printList();
    }
    if ((metadata & TESTS_METADATA_PARAMS) != 0U) {
      printParams();
    }
  }
  runPendingFixture();
  if (s_config->events != NULL) {
    s_config->events();
  }
}

uint32_t fiesta_test_active_id(void) { return s_active; }

void fiesta_test_session_ended(void) {
  HAL_ATOMIC_STORE(&s_scSessionEnded, true, HAL_ATOMIC_RELEASE);
}

//=============================================================================
// Console
//=============================================================================

static void printHelp(void) {
  deb(TEST_HIGHLIGHT_ON "Tests: run <name> | run all | stop | skip | list | "
                        "params | set <param> <value> | ?" TEST_HIGHLIGHT_OFF);
  if (s_config->help != NULL) {
    s_config->help();
  }
}

static void printList(void) {
  uint32_t active = FIESTA_TEST_NONE;
  if (scLock()) {
    active = s_published.active;
    hal_mutex_unlock(s_scMutex);
  }
  size_t total = 0U;
  const fiesta_test_t *suite = configuredSuite(&total);
  for (size_t i = 0U; i < total; i++) {
    deb("TEST: %-7s %-42s %s%s%s", suite[i].name, suite[i].summary,
        suite[i].sequenced ? "in sequence" : "on request",
        suite[i].scSupported ? ", configurator" : "",
        active == suite[i].id ? ", running" : "");
  }
}

static void printParams(void) {
  for (size_t i = 0U; i < (size_t)s_config->param_count; i++) {
    const fiesta_test_param_t *desc = fiesta_test_param_desc((size_t)i);
    deb("TEST: %-16s %ld %s [%ld..%ld, default %ld]", desc->id,
        (long)fiesta_test_param((size_t)i), desc->unit, (long)desc->min,
        (long)desc->max, (long)desc->def);
  }
}

/** @brief Apply "set <param> <value>" to one runtime parameter. */
static void dispatchSet(const char *args) {
  char id[SC_TEST_ID_MAX] = {0};
  size_t length = 0U;
  while ((args[length] != '\0') && (args[length] != ' ') &&
         (length + 1U < sizeof(id))) {
    id[length] = args[length];
    length++;
  }
  size_t param = s_config->param_count;
  char *end = NULL;
  const long value = strtol(&args[length], &end, 10);
  if (!paramFind(id, &param) || (end == &args[length]) || (*end != '\0') ||
      (fiesta_test_param_set(param, (int32_t)value) != HAL_OK)) {
    derr("TEST: set %s rejected; send params for ids and ranges", id);
    return;
  }
  deb("TEST: set %s = %ld ok", id, (long)fiesta_test_param(param));
  publishStatus(true);
}

/** @brief Split "run <name>" and start the named test. */
static void dispatchRun(const char *name) {
  if (equals(name, "all")) {
    (void)fiesta_test_start(FIESTA_TEST_ALL);
    return;
  }
  const fiesta_test_t *entry = findByName(name);
  if (entry == NULL) {
    derr("Unknown test: '%s'. Send list for the registry.", name);
    return;
  }
  (void)fiesta_test_start(entry->id);
}

/** @brief Length of @p word when @p cmd starts with it, case folded; else 0. */
static size_t commandPrefix(const char *cmd, const char *word) {
  size_t i = 0U;
  while (word[i] != '\0') {
    if (asciiUpper(cmd[i]) != asciiUpper(word[i])) {
      return 0U;
    }
    i++;
  }
  return i;
}

static const char *commandText(const char *line) {
  size_t skip = 0U;
  while ((line != NULL) && (line[skip] == ' ')) {
    skip++;
  }
  return line != NULL ? &line[skip] : NULL;
}

static void dispatch(const char *line) {
  const char *cmd = commandText(line);
  if ((cmd == NULL) || (cmd[0] == '\0')) {
    return;
  }

  if (((cmd[0] == '?') && (cmd[1] == '\0')) || equals(cmd, "help")) {
    printHelp();
    return;
  }
  if (equals(cmd, "list")) {
    printList();
    return;
  }
  if (equals(cmd, "params")) {
    printParams();
    return;
  }
  if (equals(cmd, "stop")) {
    (void)fiesta_test_stop();
    return;
  }
  if (equals(cmd, "skip")) {
    (void)fiesta_test_skip();
    return;
  }
  size_t prefix = commandPrefix(cmd, "run ");
  if (prefix != 0U) {
    dispatchRun(&cmd[prefix]);
    return;
  }
  prefix = commandPrefix(cmd, "set ");
  if (prefix != 0U) {
    dispatchSet(&cmd[prefix]);
    return;
  }

  uint32_t requested = FIESTA_TEST_NONE;
  if ((s_config->command == NULL) || !s_config->command(cmd, &requested)) {
    derr("Unknown command: '%s'. Send ? for help.", cmd);
    return;
  }
  if (requested != FIESTA_TEST_NONE) {
    (void)fiesta_test_start(requested);
  }
}

void fiesta_test_console(const char *line) {
  if (!s_initialized) {
    return;
  }
  if ((line != NULL) && (strlen(line) < FIESTA_TEST_COMMAND_SIZE)) {
    const char *cmd = commandText(line);
    if (equals(cmd, "list")) {
      s_consoleMetadata |= TESTS_METADATA_LIST;
      return;
    }
    if (equals(cmd, "params")) {
      s_consoleMetadata |= TESTS_METADATA_PARAMS;
      return;
    }
  }
  const hal_status_t status = queueCommand(line);
  if (status == HAL_EBUSY) {
    derr("Test command pending; retry after acknowledgement");
  } else if (status == HAL_EINVAL) {
    derr("Test command rejected: empty or too long");
  } else {
    // Queued for the controller core.
  }
}

//=============================================================================
// Controller core
//=============================================================================

/** @brief Apply the request the configurator queued, if any. */
static void applyScRequest(void) {
  uint32_t test = FIESTA_TEST_NONE;
  switch (scTake(&test)) {
  case SC_REQUEST_RUN: {
    const fiesta_test_t *entry = findById(test);
    if (entry != NULL) {
      beginTest(entry, TESTS_SOURCE_SC);
    }
    break;
  }
  case SC_REQUEST_SEQUENCE:
    beginSequence(true, TESTS_SOURCE_SC);
    break;
  case SC_REQUEST_STOP:
    stopAll(SC_TEST_RESULT_STOPPED);
    break;
  case SC_REQUEST_SKIP:
    skipOne();
    break;
  default:
    break;
  }
}

/**
 * @brief Stop a configurator test nobody supervises any more.
 * @note The session ended, the host went quiet for s_config->keepalive_ms,
 * or, with the interlock on, the engine started. Console tests are the
 * bench operator's and are left alone, and so is a session end seen while no
 * configurator test runs: scRun() clears it before queuing the next one.
 */
static void superviseScTest(void) {
  if ((s_active == FIESTA_TEST_NONE) || (s_activeSource != TESTS_SOURCE_SC)) {
    return;
  }
  const bool ended =
      HAL_ATOMIC_LOAD(&s_scSessionEnded, HAL_ATOMIC_ACQUIRE) &&
      HAL_ATOMIC_EXCHANGE(&s_scSessionEnded, false, HAL_ATOMIC_ACQ_REL);
  uint32_t nowMs;
  const char *reason = NULL;
  if (ended) {
    reason = SC_TEST_RESULT_SESSION_END;
  } else if (!scPolled(&nowMs)) {
    reason = SC_TEST_RESULT_HOST_LOST;
  } else if (s_config->sc_blocked != NULL) {
    reason = s_config->sc_blocked();
  }
  if (reason != NULL) {
    derr("TEST: configurator test stopped: %s", reason);
    stopAll(reason);
  }
}

bool fiesta_test_tick(void) {
  if (!s_initialized) {
    return false;
  }

  applyScRequest();
  char line[FIESTA_TEST_COMMAND_SIZE];
  if (takeCommand(line)) {
    dispatch(line);
  }
  superviseScTest();

  const fiesta_test_t *entry = findById(s_active);
  if (entry == NULL) {
    publishStatus(false);
    return false;
  }

  if ((entry->step == NULL) && (entry->start != NULL) &&
      (HAL_ATOMIC_LOAD(&s_fixtureCompleted, HAL_ATOMIC_ACQUIRE) !=
       HAL_ATOMIC_LOAD(&s_fixtureLock.sequence, HAL_ATOMIC_RELAXED))) {
    publishStatus(false);
    return entry->drivesDemand;
  }

  bool finished = true;
  float demand = 0.0f;
  if (entry->step != NULL) {
    demand = entry->step(&finished);
  }
  const bool owned = entry->drivesDemand;
  if (owned && (s_config->apply_output != NULL)) {
    s_config->apply_output(demand);
  }
  if (finished) {
    const tests_source_t source = s_activeSource;
    deactivate((entry->result != NULL) ? entry->result() : SC_TEST_RESULT_DONE);
    if (s_sequenceRunning) {
      advanceSequence(source);
    }
  }
  publishStatus(false);
  return owned;
}

//=============================================================================
// Configurator operations, on the core that polls the serial session
//=============================================================================

/** @brief Registry entry of the n-th configurator test. */
static const fiesta_test_t *scTestAt(size_t index) {
  size_t total = 0U;
  const fiesta_test_t *suite = configuredSuite(&total);
  size_t seen = 0U;
  for (size_t i = 0U; i < total; i++) {
    if (suite[i].scSupported) {
      if (seen == index) {
        return &suite[i];
      }
      seen++;
    }
  }
  return NULL;
}

static size_t scParamCount(uint32_t test) {
  size_t count = 0U;
  for (size_t i = 0U; i < (size_t)s_config->param_count; i++) {
    if (fiesta_test_param_desc((size_t)i)->test == test) {
      count++;
    }
  }
  return count;
}

static void scFillParam(size_t param, sc_command_test_param_t *out) {
  const fiesta_test_param_t *desc = fiesta_test_param_desc(param);
  const fiesta_test_t *owner = findById(desc->test);
  *out =
      (sc_command_test_param_t){desc->id,   (owner != NULL) ? owner->name : "",
                                desc->unit, fiesta_test_param(param),
                                desc->min,  desc->max,
                                desc->def};
}

static size_t scCount(void *user) {
  (void)user;
  size_t count = 0U;
  while (scTestAt(count) != NULL) {
    count++;
  }
  return count;
}

static bool scInfo(void *user, size_t index, sc_command_test_info_t *out) {
  (void)user;
  const fiesta_test_t *entry = scTestAt(index);
  if (entry == NULL) {
    return false;
  }
  *out = (sc_command_test_info_t){entry->name, inSequence(entry, true),
                                  scParamCount(entry->id)};
  return true;
}

static bool scParamAt(void *user, size_t testIndex, size_t paramIndex,
                      sc_command_test_param_t *out) {
  (void)user;
  const fiesta_test_t *entry = scTestAt(testIndex);
  size_t seen = 0U;
  for (size_t i = 0U; (entry != NULL) && (i < (size_t)s_config->param_count);
       i++) {
    if (fiesta_test_param_desc((size_t)i)->test == entry->id) {
      if (seen == paramIndex) {
        scFillParam((size_t)i, out);
        return true;
      }
      seen++;
    }
  }
  return false;
}

static bool scParam(void *user, const char *id, sc_command_test_param_t *out) {
  (void)user;
  size_t param = s_config->param_count;
  if (!paramFind(id, &param)) {
    return false;
  }
  scFillParam(param, out);
  return true;
}

static hal_status_t scSetParam(void *user, const char *id, int32_t value) {
  (void)user;
  scTouch();
  size_t param = s_config->param_count;
  return paramFind(id, &param) ? fiesta_test_param_set(param, value)
                               : HAL_ENOENT;
}

static hal_status_t scRun(void *user, const char *name) {
  (void)user;
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  scTouch();
  const bool sequence = strcmp(name, SC_TEST_SEQUENCE) == 0;
  const fiesta_test_t *entry = sequence ? NULL : findByName(name);
  if (!sequence && ((entry == NULL) || !entry->scSupported)) {
    return HAL_ENOENT;
  }
  if ((s_config->sc_blocked != NULL) && (s_config->sc_blocked() != NULL)) {
    return HAL_EPERM;
  }
  HAL_ATOMIC_STORE(&s_scSessionEnded, false, HAL_ATOMIC_RELEASE);
  return sequence ? scQueue(SC_REQUEST_SEQUENCE, FIESTA_TEST_NONE)
                  : scQueue(SC_REQUEST_RUN, entry->id);
}

static hal_status_t scStop(void *user) {
  (void)user;
  scTouch();
  return scQueue(SC_REQUEST_STOP, FIESTA_TEST_NONE);
}

static hal_status_t scSkip(void *user) {
  (void)user;
  scTouch();
  return scQueue(SC_REQUEST_SKIP, FIESTA_TEST_NONE);
}

static void scStatus(void *user, sc_command_test_status_t *out) {
  (void)user;
  scTouch();
  tests_published_t snapshot = {0};
  if (scLock()) {
    snapshot = s_published;
    hal_mutex_unlock(s_scMutex);
  }
  const fiesta_test_t *active = findById(snapshot.active);
  const fiesta_test_t *last = findById(snapshot.last);
  *out = (sc_command_test_status_t){0};
  out->active = (active != NULL) ? active->name : NULL;
  out->source = (snapshot.source == TESTS_SOURCE_SC) ? SC_TEST_SOURCE_SC
                                                     : SC_TEST_SOURCE_CONSOLE;
  out->runs = snapshot.runs;
  out->seq_index = snapshot.seqIndex;
  out->seq_count = snapshot.seqCount;
  out->elapsed_ms = (active != NULL) ? (hal_millis() - snapshot.startedMs) : 0U;
  out->drive_valid = snapshot.driveValid;
  out->demand_x10 = snapshot.demandX10;
  out->position_x10 = snapshot.positionX10;
  out->field_count = snapshot.fieldCount;
  (void)memcpy(out->fields, snapshot.fields, sizeof(out->fields));
  out->last = (last != NULL) ? last->name : NULL;
  out->result = snapshot.result;
}

const struct sc_command_test_ops_s *fiesta_test_sc_ops(void) {
  static const sc_command_test_ops_t s_scOps = {
      scCount, scInfo, scParamAt, scParam,  scSetParam,
      scRun,   scStop, scSkip,    scStatus,
  };
  return &s_scOps;
}
