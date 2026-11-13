#include "console_stream.h"

enum { TEXT, ESCAPE, CSI, OSC, OSC_ESCAPE };

hal_status_t vp37_console_stream_init(vp37_console_stream_t *stream,
                                      char *storage, size_t capacity,
                                      vp37_line_callback_t callback,
                                      void *context) {
  if (stream == NULL)
    return HAL_EINVAL;
  *stream = (vp37_console_stream_t){0};
  return vp37_line_stream_init(&stream->lines, storage, capacity, callback,
                               context);
}

hal_status_t vp37_console_stream_feed(vp37_console_stream_t *stream,
                                      const unsigned char *data, size_t size) {
  if (stream == NULL || stream->lines.buffer == NULL ||
      (data == NULL && size != 0))
    return HAL_EINVAL;
  if (stream->lines.status != HAL_OK)
    return stream->lines.status;
  for (size_t i = 0; i < size; ++i) {
    const unsigned char byte = data[i];
    if (byte == 0) {
      stream->lines.status = HAL_EPROTO;
      return stream->lines.status;
    }
    if (byte == '\r' || byte == '\n') {
      stream->escape = TEXT;
      if (byte == '\n' && stream->after_cr) {
        stream->after_cr = false;
        continue;
      }
      stream->after_cr = byte == '\r';
      const hal_status_t status =
          vp37_line_stream_feed(&stream->lines, "\n", 1);
      if (status != HAL_OK)
        return status;
      continue;
    }
    stream->after_cr = false;
    if (stream->escape == ESCAPE) {
      stream->escape = byte == '[' ? CSI : (byte == ']' ? OSC : TEXT);
      continue;
    }
    if (stream->escape == CSI) {
      if (byte >= 0x40 && byte <= 0x7e)
        stream->escape = TEXT;
      continue;
    }
    if (stream->escape == OSC || stream->escape == OSC_ESCAPE) {
      if (byte == 7 || (stream->escape == OSC_ESCAPE && byte == '\\'))
        stream->escape = TEXT;
      else
        stream->escape = byte == 27 ? OSC_ESCAPE : OSC;
      continue;
    }
    if (byte == 27) {
      stream->escape = ESCAPE;
      continue;
    }
    if (byte < 32 && byte != '\t')
      continue;
    const char text = byte == '\t' ? ' ' : (char)byte;
    const hal_status_t status = vp37_line_stream_feed(&stream->lines, &text, 1);
    if (status != HAL_OK)
      return status;
  }
  return HAL_OK;
}
