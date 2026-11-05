#ifndef VP37V_TELEMETRY_STREAM_H
#define VP37V_TELEMETRY_STREAM_H

#include "jb/status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Receives one line without LF. The borrowed bytes are valid during the call.
 */
typedef hal_status_t (*vp37_line_callback_t)(void *context, const char *line,
                                             size_t length);

/** Allocation-free line reader. Initialize before use; do not copy while
 * feeding. */
typedef struct {
  char *buffer;
  size_t capacity;
  size_t used;
  vp37_line_callback_t callback;
  void *context;
  hal_status_t status;
  bool finished;
} vp37_line_stream_t;

/**
 * Initialize a stream using caller-owned storage for one incomplete line.
 * Storage and callback must be non-NULL, capacity must be positive. Context
 * may be NULL. Capacity excludes LF; no terminating NUL is needed.
 * Returns HAL_EINVAL for invalid arguments. Storage must outlive the stream.
 */
hal_status_t vp37_line_stream_init(vp37_line_stream_t *stream, char *storage,
                                   size_t capacity,
                                   vp37_line_callback_t callback,
                                   void *context);
/**
 * Feed bytes that do not overlap the stream storage. NULL data is allowed
 * only for zero length. Complete lines are delivered immediately. An overlong
 * line returns HAL_EOVERFLOW. Callback errors stop the stream; earlier lines
 * remain delivered. Reinitialize after a terminal error or file replacement.
 */
hal_status_t vp37_line_stream_feed(vp37_line_stream_t *stream, const char *data,
                                   size_t length);
/** Deliver the final unterminated line, if any. Repeated calls do not redeliver
 * it. */
hal_status_t vp37_line_stream_finish(vp37_line_stream_t *stream);

/** MCU counter unwrapping state. Zero-initialize before the first observation.
 */
typedef struct {
  uint32_t last_us;
  double time;
  bool initialized;
} vp37_sample_clock_t;

/**
 * Map a 32-bit MCU timestamp into seconds, anchored to the first host time.
 * Ordered control observations pass advance=true. A backward control timestamp
 * starts a new epoch at host_time; forward wrapping advances the existing
 * epoch. Delayed trace/current samples pass advance=false and use the nearest
 * epoch. Consecutive ordered observations must be less than 2^31 us apart.
 * Returns HAL_EINVAL for NULL state/output or a non-finite host time.
 */
hal_status_t vp37_sample_clock_time(vp37_sample_clock_t *clock, uint32_t us,
                                    double host_time, bool advance,
                                    double *time);

#ifdef __cplusplus
}
#endif
#endif
