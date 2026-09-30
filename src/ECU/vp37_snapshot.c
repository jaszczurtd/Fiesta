// VP37 snapshot: the control core publishes the pump at the end of a step, any
// other core copies it without a lock. A sequence counter is odd while the
// snapshot is written; a copy that saw it odd or changed is taken again.
//
// The published words are the only memory both cores touch, and they are
// written and read with relaxed atomic accesses only, so a copy that overlaps
// a publication is a discarded result, never a data race. The control core
// builds each publication in its own memory first; on the RP2040 an aligned
// relaxed word access is a plain load or store.

#include "vp37_internal.h"

/** Copies a reader attempts before it reports HAL_EAGAIN. */
#define VP37_SNAPSHOT_READ_ATTEMPTS 4U

/* A copy starts at the sequence the first returns and stands when the second
 * accepts it; host tests interleave publications between the two. */
TESTABLE_STATIC uint32_t VP37_snapshotBegin(const VP37Pump *self);
TESTABLE_STATIC bool VP37_snapshotEnd(const VP37Pump *self, uint32_t sequence);

/** @brief One published word, seen as a float, as its bits or, for the
 * telemetry reader, as its bytes in memory order. */
typedef union {
  float value;
  uint32_t bits;
#if VP37_TELEMETRY_ENABLED
  uint8_t bytes[sizeof(uint32_t)];
#endif
} VP37Word;

/* A status word holds the whole bit pattern of one float. */
_Static_assert(sizeof(float) == sizeof(uint32_t),
               "a published status word must hold one float");

/** @brief The bit pattern of a float, for a status word. */
static uint32_t VP37_floatBits(float value) {
  const VP37Word word = {.value = value};
  return word.bits;
}

/** @brief The float a status word holds. */
static float VP37_bitsFloat(uint32_t bits) {
  const VP37Word word = {.bits = bits};
  return word.value;
}

#if VP37_TELEMETRY_ENABLED
/* The telemetry part of the snapshot. It lives here, not in the pump, so the
 * pump has one layout in every build; like the trace buffer, it belongs to the
 * one pump of the image that publishes telemetry. The staging copy is the
 * control core's own: built as the struct and published through the other
 * member of the union, so the control step copies whole words. Only the
 * published words are shared. */
static union {
  VP37Telemetry value;
  uint32_t
      words[(sizeof(VP37Telemetry) + sizeof(uint32_t) - 1U) / sizeof(uint32_t)];
} s_telemetryStaging;
static uint32_t s_telemetryWords[COUNTOF(s_telemetryStaging.words)];

/** @brief Copy the published telemetry words into the bytes of a
 * VP37Telemetry. Each word gives its bytes back in memory order, as the
 * writer's union took them, with no copy of the whole struct on the stack. */
static void VP37_loadTelemetry(uint8_t *to) {
  for (size_t i = 0U; i < COUNTOF(s_telemetryWords); i++) {
    const VP37Word word = {
        .bits = HAL_ATOMIC_LOAD(&s_telemetryWords[i], HAL_ATOMIC_RELAXED)};
    for (size_t b = 0U; b < sizeof(uint32_t); b++) {
      const size_t offset = (i * sizeof(uint32_t)) + b;
      if (offset < sizeof(VP37Telemetry)) {
        to[offset] = word.bytes[b];
      }
    }
  }
}

const VP37Telemetry *VP37_publishedTelemetry(void) {
  return &s_telemetryStaging.value;
}

static void VP37_buildTelemetry(const VP37Pump *self,
                                VP37Telemetry *telemetry) {
  telemetry->pidTimeUpdate = self->pidTimeUpdate;
  telemetry->controlLastUs = self->controlLastUs;
  telemetry->controlDtUs = self->controlDtUs;
  telemetry->controlSequence = self->controlSequence;
  telemetry->pidDtUs = self->pidDtUs;
  telemetry->controlExecUs = self->controlExecUs;
  telemetry->cyclicDelayMs = self->callbacks.cyclicDelayMs();
  telemetry->activeTestName = self->callbacks.activeTestName();
  telemetry->adjustometer = self->adjustometer.last;
  telemetry->feedback = self->feedback;
  telemetry->demand = self->demand;
  telemetry->feedforward = self->feedforward;
  telemetry->pid = self->pid;
  telemetry->supply = self->supply;
  telemetry->thermal = self->thermal;
  telemetry->scan = self->scan;
  telemetry->currentControl = self->currentControl;
  telemetry->output = self->output;
}
#endif

void VP37_publish(VP37Pump *self) {
  // Built from the fields alone: a struct copy would carry its padding.
  const uint32_t statusWords[VP37_STATUS_WORDS] = {
      [VP37_STATUS_WORD_INITIALIZED] = self->vp37Initialized ? 1U : 0U,
      [VP37_STATUS_WORD_READ_COUNT] = self->feedback.readCount,
      [VP37_STATUS_WORD_FUEL_TEMP] = VP37_floatBits(self->feedback.fuelTempC),
      [VP37_STATUS_WORD_SUPPLY] = VP37_floatBits(self->feedback.supplyVolts)};
#if VP37_TELEMETRY_ENABLED
  VP37_buildTelemetry(self, &s_telemetryStaging.value);
#endif

  const uint32_t sequence =
      HAL_ATOMIC_LOAD(&self->publishedSequence, HAL_ATOMIC_ACQUIRE);
  HAL_ATOMIC_STORE(&self->publishedSequence, sequence + 1U, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_RELEASE);
  for (size_t i = 0U; i < VP37_STATUS_WORDS; i++) {
    HAL_ATOMIC_STORE(&self->published[i], statusWords[i], HAL_ATOMIC_RELAXED);
  }
#if VP37_TELEMETRY_ENABLED
  for (size_t i = 0U; i < COUNTOF(s_telemetryWords); i++) {
    HAL_ATOMIC_STORE(&s_telemetryWords[i], s_telemetryStaging.words[i],
                     HAL_ATOMIC_RELAXED);
  }
#endif
  // Zero stays reserved for a pump that never published.
  const uint32_t next = ((sequence + 2U) == 0U) ? 2U : (sequence + 2U);
  HAL_ATOMIC_STORE(&self->publishedSequence, next, HAL_ATOMIC_RELEASE);
}

void VP37_publishStop(VP37Pump *self) {
  if (HAL_ATOMIC_LOAD(&self->published[VP37_STATUS_WORD_INITIALIZED],
                      HAL_ATOMIC_RELAXED) != 0U) {
    VP37_publish(self);
  }
}

TESTABLE_STATIC uint32_t VP37_snapshotBegin(const VP37Pump *self) {
  return HAL_ATOMIC_LOAD(&self->publishedSequence, HAL_ATOMIC_ACQUIRE);
}

TESTABLE_STATIC bool VP37_snapshotEnd(const VP37Pump *self, uint32_t sequence) {
  HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_ACQUIRE);
  return ((sequence & 1U) == 0U) &&
         (HAL_ATOMIC_LOAD(&self->publishedSequence, HAL_ATOMIC_RELAXED) ==
          sequence);
}

/**
 * @brief Copy one whole publication.
 * @param statusWords Receives the VP37_STATUS_WORD_* words.
 * @param telemetry Receives the bytes of the telemetry, or NULL for none;
 * always NULL without VP37_TELEMETRY_ENABLED.
 */
static hal_status_t VP37_copyPublished(const VP37Pump *self,
                                       uint32_t *statusWords,
                                       uint8_t *telemetry) {
#if !VP37_TELEMETRY_ENABLED
  (void)telemetry;
#endif
  hal_status_t status = HAL_EAGAIN;
  for (uint8_t attempt = 0U;
       (attempt < VP37_SNAPSHOT_READ_ATTEMPTS) && (status == HAL_EAGAIN);
       attempt++) {
    const uint32_t sequence = VP37_snapshotBegin(self);
    if (sequence == 0U) {
      status = HAL_ENOENT;
    } else if ((sequence & 1U) == 0U) {
      for (size_t i = 0U; i < VP37_STATUS_WORDS; i++) {
        statusWords[i] =
            HAL_ATOMIC_LOAD(&self->published[i], HAL_ATOMIC_RELAXED);
      }
#if VP37_TELEMETRY_ENABLED
      if (telemetry != NULL) {
        VP37_loadTelemetry(telemetry);
      }
#endif
      if (VP37_snapshotEnd(self, sequence)) {
        status = HAL_OK;
      }
    } else {
      /* A publication is being written: copy again. */
    }
  }
  return status;
}

/** @brief The status the published words describe. */
static void VP37_decodeStatus(const uint32_t *words, VP37Status *out) {
  out->initialized = words[VP37_STATUS_WORD_INITIALIZED] != 0U;
  out->readCount = words[VP37_STATUS_WORD_READ_COUNT];
  out->fuelTempC = VP37_bitsFloat(words[VP37_STATUS_WORD_FUEL_TEMP]);
  out->supplyVolts = VP37_bitsFloat(words[VP37_STATUS_WORD_SUPPLY]);
}

hal_status_t VP37_readStatus(const VP37Pump *self, VP37Status *out) {
  hal_status_t status = HAL_EINVAL;
  if ((self != NULL) && (out != NULL)) {
    uint32_t words[VP37_STATUS_WORDS];
    status = VP37_copyPublished(self, words, NULL);
    if (status == HAL_OK) {
      VP37_decodeStatus(words, out);
    }
  }
  return status;
}

#if VP37_TELEMETRY_ENABLED
bool VP37_waitForPublication(const VP37Pump *self, uint32_t timeoutUs) {
  bool published = false;
  if (self != NULL) {
    const uint32_t first = VP37_snapshotBegin(self);
    const uint32_t startedUs = hal_micros();
    while (!published && !hal_elapsed_u32(hal_micros(), startedUs, timeoutUs)) {
      const uint32_t sequence = VP37_snapshotBegin(self);
      published = (sequence != first) && ((sequence & 1U) == 0U);
      if (!published) {
        hal_delay_us(VP37_PUBLICATION_POLL_US);
      }
    }
  }
  return published;
}

hal_status_t VP37_readSnapshot(const VP37Pump *self, VP37Snapshot *out) {
  hal_status_t status = HAL_EINVAL;
  if ((self != NULL) && (out != NULL)) {
    uint32_t words[VP37_STATUS_WORDS];
    status = VP37_copyPublished(self, words, (uint8_t *)&out->telemetry);
    if (status == HAL_OK) {
      VP37_decodeStatus(words, &out->status);
    }
  }
  return status;
}
#endif
