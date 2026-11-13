#include "telemetry_stream.h"

#include <math.h>
#include <string.h>

hal_status_t vp37_line_stream_init(vp37_line_stream_t *stream, char *storage,
                                   size_t capacity,
                                   vp37_line_callback_t callback,
                                   void *context) {
  if (stream == NULL || storage == NULL || capacity == 0 || callback == NULL) {
    return HAL_EINVAL;
  }
  *stream = (vp37_line_stream_t){storage, capacity, 0,    callback,
                                 context, HAL_OK,   false};
  return HAL_OK;
}

hal_status_t vp37_line_stream_feed(vp37_line_stream_t *stream, const char *data,
                                   size_t length) {
  if (stream == NULL || stream->buffer == NULL || stream->callback == NULL ||
      (data == NULL && length != 0))
    return HAL_EINVAL;
  if (stream->status != HAL_OK)
    return stream->status;
  if (stream->finished)
    return HAL_ESTATE;
  while (length != 0) {
    const char *end = memchr(data, '\n', length);
    const size_t take = end != NULL ? (size_t)(end - data) : length;
    if (take > stream->capacity - stream->used) {
      stream->status = HAL_EOVERFLOW;
      return stream->status;
    }
    if (take != 0)
      memcpy(stream->buffer + stream->used, data, take);
    stream->used += take;
    if (end == NULL)
      return HAL_OK;
    stream->status =
        stream->callback(stream->context, stream->buffer, stream->used);
    stream->used = 0;
    if (stream->status != HAL_OK)
      return stream->status;
    data += take + 1;
    length -= take + 1;
  }
  return HAL_OK;
}

hal_status_t vp37_line_stream_finish(vp37_line_stream_t *stream) {
  if (stream == NULL || stream->buffer == NULL || stream->callback == NULL)
    return HAL_EINVAL;
  if (stream->status != HAL_OK)
    return stream->status;
  if (stream->finished)
    return HAL_OK;
  stream->finished = true;
  if (stream->used != 0) {
    stream->status =
        stream->callback(stream->context, stream->buffer, stream->used);
    stream->used = 0;
  }
  return stream->status;
}

hal_status_t vp37_sample_clock_time(vp37_sample_clock_t *clock, uint32_t us,
                                    double host_time, bool advance,
                                    double *time) {
  if (clock == NULL || time == NULL || !isfinite(host_time))
    return HAL_EINVAL;
  clock->restarted = false;
  if (!clock->initialized) {
    clock->last_us = us;
    clock->time = host_time;
    clock->initialized = true;
  }
  const uint32_t distance = us - clock->last_us;
  const int64_t delta = distance <= INT32_MAX
                            ? (int64_t)distance
                            : (int64_t)distance - INT64_C(4294967296);
  const double mapped = clock->time + (double)delta / 1000000.0;
  if (advance) {
    clock->last_us = us;
    clock->restarted = delta < 0 || fabs(mapped - host_time) > 5.0;
    clock->time = clock->restarted ? host_time : mapped;
  }
  *time = advance ? clock->time : mapped;
  return HAL_OK;
}
