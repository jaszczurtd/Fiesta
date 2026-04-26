#ifndef SC_FRAME_H
#define SC_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SerialConfigurator wire framing.
 *
 * Frame format (both directions):
 *
 *   $SC,<seq>,<payload>*<crc8>\n
 *
 * - Literal start sentinel: "$SC,"
 * - <seq>:    ASCII unsigned decimal in [0..65535]; the response carries the
 *             same value so the host can correlate replies with requests.
 * - <payload>: free-form ASCII text, must not contain '*', '\r' or '\n'.
 * - <crc8>:   two uppercase hex digits, CRC-8/CCITT (poly 0x07, init 0x00,
 *             no reflect, no xor-out) computed over bytes between (but not
 *             including) the leading '$' and the '*' separator.
 *
 * This must stay byte-for-byte compatible with
 * `libraries/JaszczurHAL/src/hal/hal_serial_frame.h` on the firmware side.
 */

#define SC_FRAME_PREFIX "$SC,"
#define SC_FRAME_PREFIX_LEN 4u
#define SC_FRAME_PAYLOAD_MAX 256u
#define SC_FRAME_LINE_MAX (SC_FRAME_PAYLOAD_MAX + 32u)

/** @brief CRC-8/CCITT (poly 0x07, init 0x00) over @p data. */
uint8_t sc_frame_crc8(const uint8_t *data, size_t len);

/**
 * @brief Build a single framed line `$SC,<seq>,<payload>*<crc>` (no `\n`).
 *
 * @param seq       Sequence number (0..65535).
 * @param payload   NUL-terminated payload (must not contain '*', CR or LF).
 * @param out       Output buffer.
 * @param out_size  Output buffer size.
 * @param out_len   Optional, receives written bytes (excluding NUL).
 * @return true on success.
 */
bool sc_frame_encode(uint16_t seq,
                     const char *payload,
                     char *out,
                     size_t out_size,
                     size_t *out_len);

/**
 * @brief Parse a trimmed framed line into (seq, payload).
 *
 * The line must start with @ref SC_FRAME_PREFIX and carry a valid CRC.
 * Leading whitespace and trailing CR/LF must already be stripped by the
 * caller.
 */
bool sc_frame_decode(const char *line,
                     uint16_t *seq_out,
                     char *payload_out,
                     size_t payload_out_size);

#ifdef __cplusplus
}
#endif

#endif /* SC_FRAME_H */
