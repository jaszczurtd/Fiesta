#ifndef SC_TEXT_H
#define SC_TEXT_H

/**
 * @file sc_text.h
 * @brief Bounded string and token helpers shared by the host core modules.
 *
 * Replies on the wire are single lines of space-separated tokens. Every
 * parser in src/core walks them with the same helpers, so a change to the
 * token rules lands in one place.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Copy @p src into @p dst, truncating; NULL @p src gives "". */
void sc_text_copy(char *dst, size_t dst_size, const char *src);

/** @brief Copy the span [@p start, @p end) into @p dst, truncating. */
void sc_text_copy_span(char *dst, size_t dst_size, const char *start,
                       const char *end);

/** @brief First non-space character at or after @p cursor. */
const char *sc_text_skip_spaces(const char *cursor);

/** @brief End of the token starting at @p cursor (next space or NUL). */
const char *sc_text_token_end(const char *cursor);

/**
 * @brief Copy the next space-delimited token into @p token and advance
 *        @p cursor past it.
 * @return false when no token is left.
 */
bool sc_text_next_token(const char **cursor, char *token, size_t token_size);

/** @brief Whether @p text starts with the whole token @p word, i.e. @p word
 *         followed by a space or the end of the text. */
bool sc_text_starts_with_token(const char *text, const char *word);

/** @brief Strict base-10 parse: the whole text must be one number. */
bool sc_text_parse_i64(const char *text, int64_t *value);

/** @brief Write @p message into an optional error buffer. */
void sc_text_set_error(char *error, size_t error_size, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* SC_TEXT_H */
