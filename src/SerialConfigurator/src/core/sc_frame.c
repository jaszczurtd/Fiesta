#include "sc_frame.h"

#include <stdio.h>
#include <string.h>

uint8_t sc_frame_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00u;
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (uint8_t)((crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u)
                                          : (uint8_t)(crc << 1));
        }
    }
    return crc;
}

bool sc_frame_encode(uint16_t seq,
                     const char *payload,
                     char *out,
                     size_t out_size,
                     size_t *out_len)
{
    if (payload == NULL || out == NULL || out_size < 12u) {
        return false;
    }

    for (const char *p = payload; *p != '\0'; ++p) {
        const char c = *p;
        if (c == '*' || c == '\r' || c == '\n') {
            return false;
        }
    }

    int written = snprintf(out, out_size, "%s%u,%s",
                           SC_FRAME_PREFIX, (unsigned)seq, payload);
    if (written < 0 || (size_t)written >= out_size) {
        return false;
    }

    const uint8_t crc = sc_frame_crc8((const uint8_t *)out + 1,
                                      (size_t)written - 1u);

    const int extra = snprintf(out + written, out_size - (size_t)written,
                               "*%02X", (unsigned)crc);
    if (extra < 0 || (size_t)extra >= out_size - (size_t)written) {
        return false;
    }

    if (out_len != NULL) {
        *out_len = (size_t)(written + extra);
    }
    return true;
}

bool sc_frame_decode(const char *line,
                     uint16_t *seq_out,
                     char *payload_out,
                     size_t payload_out_size)
{
    if (line == NULL || seq_out == NULL || payload_out == NULL ||
        payload_out_size == 0u) {
        return false;
    }

    payload_out[0] = '\0';

    if (strncmp(line, SC_FRAME_PREFIX, SC_FRAME_PREFIX_LEN) != 0) {
        return false;
    }

    const char *seq_begin = line + SC_FRAME_PREFIX_LEN;
    const char *seq_end = seq_begin;
    while (*seq_end >= '0' && *seq_end <= '9') {
        ++seq_end;
    }
    if (seq_end == seq_begin || *seq_end != ',') {
        return false;
    }

    uint32_t seq_acc = 0u;
    for (const char *p = seq_begin; p < seq_end; ++p) {
        seq_acc = seq_acc * 10u + (uint32_t)(*p - '0');
        if (seq_acc > 0xFFFFu) {
            return false;
        }
    }

    const char *payload_begin = seq_end + 1;
    const char *star = strchr(payload_begin, '*');
    if (star == NULL) {
        return false;
    }

    if (star[1] == '\0' || star[2] == '\0' || star[3] != '\0') {
        return false;
    }

    uint8_t expected_crc = 0u;
    for (int i = 1; i <= 2; ++i) {
        const char c = star[i];
        uint8_t nibble;
        if (c >= '0' && c <= '9') {
            nibble = (uint8_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            nibble = (uint8_t)(10 + (c - 'A'));
        } else if (c >= 'a' && c <= 'f') {
            nibble = (uint8_t)(10 + (c - 'a'));
        } else {
            return false;
        }
        expected_crc = (uint8_t)((expected_crc << 4) | nibble);
    }

    const size_t crc_len = (size_t)(star - (line + 1));
    const uint8_t actual_crc = sc_frame_crc8((const uint8_t *)(line + 1),
                                             crc_len);
    if (actual_crc != expected_crc) {
        return false;
    }

    const size_t payload_len = (size_t)(star - payload_begin);
    if (payload_len + 1u > payload_out_size) {
        return false;
    }
    memcpy(payload_out, payload_begin, payload_len);
    payload_out[payload_len] = '\0';

    *seq_out = (uint16_t)seq_acc;
    return true;
}
