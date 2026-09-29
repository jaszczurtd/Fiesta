#ifndef SC_TEST_TRANSPORT_H
#define SC_TEST_TRANSPORT_H

/*
 * Pieces shared by the host-test transports that stand in for an ECU:
 * the candidate list, path resolution and the HELLO / AUTH_BEGIN replies.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_auth.h"
#include "sc_transport.h"

/** UID reported in the HELLO replies built by sc_test_hello_reply(). */
#define SC_TEST_UID_HEX "E661A4D1234567AB"

/** Empty candidate list: the tests pass an explicit device path. */
bool sc_test_list_no_candidates(void *ctx, ScTransportCandidateList *list,
                                char *err, size_t err_size);

/** Resolves a candidate path to itself. */
bool sc_test_resolve_same_path(void *ctx, const char *candidate, char *out,
                               size_t out_size, char *err, size_t err_size);

/** HELLO reply of the ECU with @p session and @p fw_version, reporting
 *  SC_TEST_UID_HEX. */
void sc_test_hello_reply(char *out, size_t out_size, uint32_t session,
                         const char *fw_version);

/** `SC_OK AUTH_CHALLENGE <hex>` for @p challenge, in lower-case hex. */
void sc_test_challenge_reply(
    char *out, size_t out_size,
    const uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES]);

#endif /* SC_TEST_TRANSPORT_H */
