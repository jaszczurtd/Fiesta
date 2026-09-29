#include "sc_test_transport.h"

#include "sc_fiesta_module_tokens.h"

#include <stdio.h>

bool sc_test_list_no_candidates(void *ctx, ScTransportCandidateList *list,
                                char *err, size_t err_size) {
  (void)ctx;
  (void)err;
  (void)err_size;
  list->count = 0u;
  list->truncated = false;
  return true;
}

bool sc_test_resolve_same_path(void *ctx, const char *candidate, char *out,
                               size_t out_size, char *err, size_t err_size) {
  (void)ctx;
  (void)err;
  (void)err_size;
  snprintf(out, out_size, "%s", candidate);
  return true;
}

void sc_test_hello_reply(char *out, size_t out_size, uint32_t session,
                         const char *fw_version) {
  snprintf(out, out_size,
           "OK HELLO module=" SC_MODULE_TOKEN_ECU
           " proto=1 session=%lu fw=%s build=dev uid=" SC_TEST_UID_HEX,
           (unsigned long)session, fw_version);
}

void sc_test_challenge_reply(
    char *out, size_t out_size,
    const uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES]) {
  static const char k_hex_table[] = "0123456789abcdef";
  char hex[HAL_SC_AUTH_CHALLENGE_BYTES * 2u + 1u];
  for (size_t i = 0u; i < HAL_SC_AUTH_CHALLENGE_BYTES; ++i) {
    hex[i * 2u] = k_hex_table[(challenge[i] >> 4) & 0x0Fu];
    hex[i * 2u + 1u] = k_hex_table[challenge[i] & 0x0Fu];
  }
  hex[HAL_SC_AUTH_CHALLENGE_BYTES * 2u] = '\0';
  snprintf(out, out_size, "SC_OK AUTH_CHALLENGE %s", hex);
}
