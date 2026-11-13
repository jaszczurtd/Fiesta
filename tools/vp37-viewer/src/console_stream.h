#ifndef VP37V_CONSOLE_STREAM_H
#define VP37V_CONSOLE_STREAM_H

#include "telemetry_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bounded monitor output reader: strips ANSI escapes and normalizes CR/LF.
 * Storage belongs to the caller. Only complete lines reach the callback;
 * discard this state when the child exits, including an unfinished line. */
typedef struct {
  vp37_line_stream_t lines;
  unsigned escape;
  bool after_cr;
} vp37_console_stream_t;

hal_status_t vp37_console_stream_init(vp37_console_stream_t *stream,
                                      char *storage, size_t capacity,
                                      vp37_line_callback_t callback,
                                      void *context);
hal_status_t vp37_console_stream_feed(vp37_console_stream_t *stream,
                                      const unsigned char *data, size_t size);

#ifdef __cplusplus
}
#endif
#endif
