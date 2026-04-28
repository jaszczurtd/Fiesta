#include "sc_frame.h"
#include "sc_fiesta_module_tokens.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "[FAIL] %s\n", msg); \
    return 1; \
} } while (0)

/* Reference vector: CRC-8/CCITT (poly 0x07, init 0x00) of "123456789" is 0xF4. */
static int test_crc8_check_string(void)
{
    const uint8_t kCheck[] = {'1','2','3','4','5','6','7','8','9'};
    const uint8_t crc = sc_frame_crc8(kCheck, sizeof(kCheck));
    TEST_ASSERT(crc == 0xF4u, "CRC-8/CCITT check vector mismatch");

    /* Empty input must return init value (0x00). */
    TEST_ASSERT(sc_frame_crc8((const uint8_t *)"", 0u) == 0x00u, "empty CRC");

    /* NULL is treated as init value too. */
    TEST_ASSERT(sc_frame_crc8(NULL, 5u) == 0x00u, "NULL CRC");
    return 0;
}

static int test_encode_basic(void)
{
    char out[64];
    size_t out_len = 0u;
    TEST_ASSERT(sc_frame_encode(1u, "HELLO", out, sizeof(out), &out_len),
                "encode HELLO");
    /* CRC8 over "SC,1,HELLO" = 0x0F (verified against an independent
     * Python reference implementation of CRC-8/CCITT). */
    TEST_ASSERT(strcmp(out, "$SC,1,HELLO*0F") == 0, "encoded HELLO byte-stream");
    TEST_ASSERT(out_len == strlen(out), "out_len matches strlen");
    return 0;
}

static int test_encode_rejects_invalid_payload(void)
{
    char out[64];
    /* Star is the frame separator and may not appear in payload. */
    TEST_ASSERT(!sc_frame_encode(1u, "BAD*PAYLOAD", out, sizeof(out), NULL),
                "encode must reject '*' in payload");
    TEST_ASSERT(!sc_frame_encode(1u, "BAD\nPAYLOAD", out, sizeof(out), NULL),
                "encode must reject '\\n' in payload");
    TEST_ASSERT(!sc_frame_encode(1u, "BAD\rPAYLOAD", out, sizeof(out), NULL),
                "encode must reject '\\r' in payload");
    /* NULL out / payload. */
    TEST_ASSERT(!sc_frame_encode(1u, NULL, out, sizeof(out), NULL),
                "encode must reject NULL payload");
    TEST_ASSERT(!sc_frame_encode(1u, "OK", NULL, sizeof(out), NULL),
                "encode must reject NULL out");
    /* Out buffer too small. */
    char small[8];
    TEST_ASSERT(!sc_frame_encode(1u, "PAYLOAD", small, sizeof(small), NULL),
                "encode must reject undersized out buffer");
    return 0;
}

static int test_roundtrip(void)
{
    char encoded[128];
    TEST_ASSERT(sc_frame_encode(1234u, "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1",
                                encoded, sizeof(encoded), NULL),
                "encode roundtrip");

    uint16_t seq = 0u;
    char inner[128];
    TEST_ASSERT(sc_frame_decode(encoded, &seq, inner, sizeof(inner)),
                "decode roundtrip");
    TEST_ASSERT(seq == 1234u, "seq roundtrip");
    TEST_ASSERT(strcmp(inner, "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1") == 0,
                "payload roundtrip");
    return 0;
}

static int test_decode_rejects_bad_crc(void)
{
    /* Build a valid frame, then flip one CRC nibble. */
    char buf[64];
    TEST_ASSERT(sc_frame_encode(7u, "PING", buf, sizeof(buf), NULL),
                "encode for CRC-mutation test");

    /* Last two chars are the CRC; flip the low nibble. */
    const size_t len = strlen(buf);
    const char last = buf[len - 1u];
    buf[len - 1u] = (char)((last == '0') ? '1' : '0');

    uint16_t seq = 0u;
    char inner[16];
    TEST_ASSERT(!sc_frame_decode(buf, &seq, inner, sizeof(inner)),
                "decode must reject corrupted CRC");
    TEST_ASSERT(inner[0] == '\0', "payload must be cleared on decode failure");
    return 0;
}

static int test_decode_rejects_non_framed_lines(void)
{
    uint16_t seq = 0u;
    char inner[32];

    /* Strict prefix matching: the frame sentinel must appear at the very
     * start of the trimmed line. A line that *contains* "$SC," in the
     * middle of a debug log is NOT a frame and must be rejected. */
    TEST_ASSERT(!sc_frame_decode("[INFO] saw $SC,1,foo*00",
                                 &seq, inner, sizeof(inner)),
                "decode must reject '$SC,' as substring");
    TEST_ASSERT(!sc_frame_decode("OK HELLO module=" SC_MODULE_TOKEN_ECU,
                                 &seq, inner, sizeof(inner)),
                "decode must reject legacy plain-text line");
    TEST_ASSERT(!sc_frame_decode("$SC,abc,FOO*00",
                                 &seq, inner, sizeof(inner)),
                "decode must reject non-numeric seq");
    TEST_ASSERT(!sc_frame_decode("$SC,1,FOO",
                                 &seq, inner, sizeof(inner)),
                "decode must reject missing '*'");
    TEST_ASSERT(!sc_frame_decode("$SC,1,FOO*",
                                 &seq, inner, sizeof(inner)),
                "decode must reject empty CRC");
    TEST_ASSERT(!sc_frame_decode("$SC,1,FOO*ZZ",
                                 &seq, inner, sizeof(inner)),
                "decode must reject non-hex CRC");
    TEST_ASSERT(!sc_frame_decode("$SC,99999,FOO*00",
                                 &seq, inner, sizeof(inner)),
                "decode must reject seq > 65535");
    return 0;
}

static int test_decode_accepts_lowercase_hex(void)
{
    char buf[64];
    TEST_ASSERT(sc_frame_encode(42u, "PONG", buf, sizeof(buf), NULL),
                "encode for hex-case test");

    /* Lowercase the CRC nibbles to confirm the decoder is case-insensitive. */
    const size_t len = strlen(buf);
    for (size_t i = len - 2u; i < len; ++i) {
        if (buf[i] >= 'A' && buf[i] <= 'F') {
            buf[i] = (char)(buf[i] - 'A' + 'a');
        }
    }

    uint16_t seq = 0u;
    char inner[16];
    TEST_ASSERT(sc_frame_decode(buf, &seq, inner, sizeof(inner)),
                "decode must accept lowercase hex CRC");
    TEST_ASSERT(seq == 42u && strcmp(inner, "PONG") == 0,
                "lowercase-CRC decode payload");
    return 0;
}

static int test_decode_rejects_short_payload_buffer(void)
{
    char buf[64];
    TEST_ASSERT(sc_frame_encode(1u, "VERY_LONG_PAYLOAD",
                                buf, sizeof(buf), NULL),
                "encode for short-buffer test");

    uint16_t seq = 0u;
    char tiny[4];
    TEST_ASSERT(!sc_frame_decode(buf, &seq, tiny, sizeof(tiny)),
                "decode must reject too-small payload buffer");
    return 0;
}

int main(void)
{
    if (test_crc8_check_string() != 0) return 1;
    if (test_encode_basic() != 0) return 1;
    if (test_encode_rejects_invalid_payload() != 0) return 1;
    if (test_roundtrip() != 0) return 1;
    if (test_decode_rejects_bad_crc() != 0) return 1;
    if (test_decode_rejects_non_framed_lines() != 0) return 1;
    if (test_decode_accepts_lowercase_hex() != 0) return 1;
    if (test_decode_rejects_short_payload_buffer() != 0) return 1;

    printf("[OK] sc_frame tests passed\n");
    return 0;
}
