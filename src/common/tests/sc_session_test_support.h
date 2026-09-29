#pragma once

/**
 * @file sc_session_test_support.h
 * @brief Host-test helpers that drive a module's SerialConfigurator session
 *        over the mocked serial port.
 *
 * Compiled into each module's test binary next to that module's config.h,
 * which provides configSessionTick(), configSessionActive() and
 * configSessionId(). Failures are reported through Unity assertions.
 */

#include <stddef.h>
#include <stdint.h>

/** Reference CRC-8/CCITT (poly 0x07, init 0x00), kept apart from the
 *  production helper so the tests lock in the wire format. */
uint8_t refCrc8(const char *data, size_t len);

/** Next request sequence number: monotonic across the test process, never
 *  0, so consecutive requests exercise the reply correlation. */
uint16_t nextTestSeq(void);

/** Writes the frame `$SC,<seq>,<payload>*<crc>\n` into @p out. */
void buildFrame(uint16_t seq, const char *payload, char *out, size_t out_size);

/** Injects @p line unchanged, runs one session tick and returns the last
 *  line the firmware wrote. */
const char *sendRawSerialLine(const char *line);

/** Frames @p inner_with_eol (trailing CR/LF dropped) with nextTestSeq(),
 *  sends it and returns the unwrapped payload of the reply, so substring
 *  assertions keep working. Asserts that the reply is a valid frame with the
 *  same seq; a missing or empty reply is returned unchanged. */
const char *sendSerialLine(const char *inner_with_eol);

/** Sends HELLO and asserts that the session is active with a non-zero id. */
void performHello(void);
