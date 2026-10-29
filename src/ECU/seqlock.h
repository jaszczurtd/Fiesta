#ifndef T_SEQLOCK
#define T_SEQLOCK

#include <hal/core/hal_compiler.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file seqlock.h
 * @brief Single-writer sequence lock for a few words shared between the cores.
 *
 * The writer brackets its stores with ecuSeqlockWriteBegin() and
 * ecuSeqlockWriteEnd(); the sequence is odd in between. A reader loads the
 * sequence, copies the words, and accepts the copy only when
 * ecuSeqlockReadConsistent() confirms that no write began or completed in the
 * meantime. Neither side ever waits: a reader that keeps overlapping writes
 * gives up after a few attempts, which the caller decides.
 */
typedef struct {
  uint32_t sequence; /**< Even between writes, odd during one. */
} ecu_seqlock_t;

/** @brief Read attempts after which a reader gives up on a busy writer. */
#define ECU_SEQLOCK_READ_ATTEMPTS 4u

/**
 * @brief Begin a write on the single writer core.
 * @param lock Lock of the shared words.
 * @return Sequence before the write, to pass to ecuSeqlockWriteEnd().
 */
static inline uint32_t ecuSeqlockWriteBegin(ecu_seqlock_t *lock) {
  const uint32_t sequence =
      HAL_ATOMIC_LOAD(&lock->sequence, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&lock->sequence, sequence + 1u, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_RELEASE);
  return sequence;
}

/**
 * @brief Complete a write: publishes the words stored since the begin.
 * @param lock Lock of the shared words.
 * @param begun Value returned by ecuSeqlockWriteBegin().
 */
static inline void ecuSeqlockWriteEnd(ecu_seqlock_t *lock, uint32_t begun) {
  HAL_ATOMIC_STORE(&lock->sequence, begun + 2u, HAL_ATOMIC_RELEASE);
}

/**
 * @brief Begin a read attempt: load the sequence before copying the words.
 * @param lock Lock of the shared words.
 * @return Sequence to pass to ecuSeqlockReadConsistent().
 */
static inline uint32_t ecuSeqlockReadBegin(ecu_seqlock_t *lock) {
  return HAL_ATOMIC_LOAD(&lock->sequence, HAL_ATOMIC_ACQUIRE);
}

/**
 * @brief Tell whether the words copied since the begin form one write.
 * @param lock Lock of the shared words.
 * @param begun Value returned by ecuSeqlockReadBegin().
 * @return true when no write was in progress at the begin and none began
 *         since; the copy is then consistent.
 */
static inline bool ecuSeqlockReadConsistent(ecu_seqlock_t *lock,
                                            uint32_t begun) {
  HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_ACQUIRE);
  return ((begun & 1u) == 0u) &&
         (HAL_ATOMIC_LOAD(&lock->sequence, HAL_ATOMIC_RELAXED) == begun);
}

#ifdef __cplusplus
}
#endif

#endif
