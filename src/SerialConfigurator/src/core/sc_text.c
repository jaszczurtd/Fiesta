/**
 * @file sc_text.c
 * @brief Bounded string and token helpers shared by the host core modules.
 */

#include "sc_text.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

void sc_text_copy(char *dst, size_t dst_size, const char *src) {
  if (dst == NULL || dst_size == 0u) {
    return;
  }
  if (src == NULL) {
    dst[0] = '\0';
    return;
  }
  /* Bounded copy without snprintf("%s", ...) which trips
   * -Werror=format-truncation when GCC cannot prove src fits. */
  const size_t src_len = strlen(src);
  const size_t copy_len =
      (src_len < (dst_size - 1u)) ? src_len : (dst_size - 1u);
  memcpy(dst, src, copy_len);
  dst[copy_len] = '\0';
}

void sc_text_copy_span(char *dst, size_t dst_size, const char *start,
                       const char *end) {
  if (dst == NULL || dst_size == 0u) {
    return;
  }
  if (start == NULL || end == NULL || end <= start) {
    dst[0] = '\0';
    return;
  }
  size_t len = (size_t)(end - start);
  if (len >= dst_size) {
    len = dst_size - 1u;
  }
  (void)memcpy(dst, start, len);
  dst[len] = '\0';
}

const char *sc_text_skip_spaces(const char *cursor) {
  if (cursor == NULL) {
    return NULL;
  }
  while (cursor[0] == ' ') {
    cursor++;
  }
  return cursor;
}

const char *sc_text_token_end(const char *cursor) {
  if (cursor == NULL) {
    return NULL;
  }
  while (cursor[0] != '\0' && cursor[0] != ' ') {
    cursor++;
  }
  return cursor;
}

bool sc_text_next_token(const char **cursor, char *token, size_t token_size) {
  if (cursor == NULL || token == NULL || token_size == 0u || *cursor == NULL) {
    return false;
  }
  const char *start = sc_text_skip_spaces(*cursor);
  if (start[0] == '\0') {
    token[0] = '\0';
    *cursor = start;
    return false;
  }
  const char *end = sc_text_token_end(start);
  sc_text_copy_span(token, token_size, start, end);
  *cursor = end;
  return token[0] != '\0';
}

bool sc_text_starts_with_token(const char *text, const char *word) {
  if (text == NULL || word == NULL) {
    return false;
  }
  const size_t length = strlen(word);
  return strncmp(text, word, length) == 0 &&
         (text[length] == ' ' || text[length] == '\0');
}

bool sc_text_parse_i64(const char *text, int64_t *value) {
  if (text == NULL || value == NULL || text[0] == '\0') {
    return false;
  }
  errno = 0;
  char *end = NULL;
  const long long parsed = strtoll(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') {
    return false;
  }
  *value = (int64_t)parsed;
  return true;
}

void sc_text_set_error(char *error, size_t error_size, const char *message) {
  sc_text_copy(error, error_size, message);
}
