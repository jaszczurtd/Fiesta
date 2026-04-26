#ifndef SC_MANIFEST_H
#define SC_MANIFEST_H

/*
 * Firmware manifest format and verification for the SerialConfigurator
 * pre-flash gate (Phase 4).
 *
 * The manifest is a small JSON object bundled alongside each UF2 artifact:
 *
 *   {
 *     "module_name":  "ECU",
 *     "fw_version":   "0.1.0",
 *     "build_id":     "2026-04-26 12:00:00",
 *     "sha256":       "<64 lowercase hex chars of the UF2 file>",
 *     "signature":    "<base64 or hex; OPTIONAL ed25519 over the rest>"
 *   }
 *
 * Required: module_name, fw_version, build_id, sha256.
 * Optional: signature.
 *
 * Verification policy is "hard reject":
 *   - any required field missing / wrong type / empty   -> reject,
 *   - sha256 length not exactly 64 hex chars            -> reject,
 *   - artifact's actual SHA-256 not equal to manifest   -> reject,
 *   - module_name not equal to the targeted module      -> reject.
 *
 * Signature verification is intentionally not wired in this slice. The
 * field is parsed and exposed so a future ed25519 backend can hook in
 * without changing the caller surface.
 *
 * The parser is a tight hand-rolled JSON reader that only accepts the
 * structure above (top-level object, string keys, string values). It
 * deliberately does not handle nested objects, arrays, numeric values,
 * booleans, or null. Every other JSON construct is a parse error.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum length of `module_name` (matches `MODULE_NAME` in firmware). */
#define SC_MANIFEST_MODULE_NAME_MAX 32u

/** @brief Maximum length of `fw_version`. */
#define SC_MANIFEST_FW_VERSION_MAX 32u

/** @brief Maximum length of `build_id`. */
#define SC_MANIFEST_BUILD_ID_MAX 64u

/** @brief Length of the lowercase hex SHA-256 string (64 chars + NUL). */
#define SC_MANIFEST_SHA256_HEX_LEN 64u
#define SC_MANIFEST_SHA256_HEX_BUF_SIZE (SC_MANIFEST_SHA256_HEX_LEN + 1u)

/** @brief Maximum signature length (raw text, base64 or hex). */
#define SC_MANIFEST_SIGNATURE_MAX 256u

/**
 * @brief Parse / verification status with a precise reason.
 *
 * Caller-side flashing logic must treat anything other than @c SC_MANIFEST_OK
 * as a hard refusal to flash.
 */
typedef enum {
    SC_MANIFEST_OK = 0,
    SC_MANIFEST_ERR_NULL_ARG,
    SC_MANIFEST_ERR_BAD_JSON,
    SC_MANIFEST_ERR_MISSING_FIELD,
    SC_MANIFEST_ERR_DUPLICATE_FIELD,
    SC_MANIFEST_ERR_FIELD_TOO_LONG,
    SC_MANIFEST_ERR_FIELD_EMPTY,
    SC_MANIFEST_ERR_BAD_SHA256_FORMAT,
    SC_MANIFEST_ERR_UNKNOWN_FIELD,
    SC_MANIFEST_ERR_FILE_OPEN,
    SC_MANIFEST_ERR_FILE_READ,
    SC_MANIFEST_ERR_FILE_TOO_LARGE,
    SC_MANIFEST_ERR_HASH_BACKEND,
    SC_MANIFEST_ERR_ARTIFACT_HASH_MISMATCH,
    SC_MANIFEST_ERR_MODULE_MISMATCH,
    SC_MANIFEST_ERR_SIGNATURE_NOT_SUPPORTED
} sc_manifest_status_t;

/** @brief Parsed manifest. All strings are NUL-terminated. */
typedef struct {
    char module_name[SC_MANIFEST_MODULE_NAME_MAX + 1u];
    char fw_version[SC_MANIFEST_FW_VERSION_MAX + 1u];
    char build_id[SC_MANIFEST_BUILD_ID_MAX + 1u];
    /** Lowercase hex (always 64 chars), NUL-terminated. */
    char sha256_hex[SC_MANIFEST_SHA256_HEX_BUF_SIZE];
    /** Decoded SHA-256 bytes (computed from @ref sha256_hex at parse time). */
    uint8_t sha256[SC_CRYPTO_SHA256_DIGEST_BYTES];
    /** Optional signature string (verbatim from JSON), or empty. */
    char signature[SC_MANIFEST_SIGNATURE_MAX + 1u];
    bool has_signature;
} sc_manifest_t;

/**
 * @brief Translate a status code into a short human-readable token.
 *
 * The token is meant for log lines / CLI errors and is stable across
 * versions, so it can be matched in tests.
 */
const char *sc_manifest_status_str(sc_manifest_status_t status);

/**
 * @brief Parse a manifest from an in-memory JSON buffer.
 *
 * @param json     Pointer to JSON bytes (NUL terminator NOT required).
 * @param json_len Length of @p json in bytes.
 * @param out      Receives parsed manifest on success.
 * @return @c SC_MANIFEST_OK on success; otherwise @p out is left undefined.
 */
sc_manifest_status_t sc_manifest_parse(const char *json,
                                       size_t json_len,
                                       sc_manifest_t *out);

/**
 * @brief Read and parse a manifest from a file.
 *
 * Convenience wrapper for desktop callers. Imposes a 64 KiB upper bound
 * on the JSON file size to keep parser inputs sane.
 */
sc_manifest_status_t sc_manifest_load_file(const char *path,
                                           sc_manifest_t *out);

/**
 * @brief Verify that the binary artifact at @p artifact_path hashes to
 *        exactly the SHA-256 declared in @p manifest.
 *
 * Reads the file fully into memory (capped at 8 MiB to fit the largest
 * conceivable RP2040 UF2 with comfortable margin).
 *
 * @return @c SC_MANIFEST_OK on a clean match, otherwise the appropriate
 *         error code (@c SC_MANIFEST_ERR_ARTIFACT_HASH_MISMATCH for a
 *         declared-vs-computed mismatch).
 */
sc_manifest_status_t sc_manifest_verify_artifact(
    const sc_manifest_t *manifest,
    const char *artifact_path);

/**
 * @brief Verify that @p manifest targets the expected module name.
 *
 * Comparison is byte-exact (case-sensitive); module names are short
 * canonical tokens defined in each firmware module's `config.h`.
 *
 * @return @c SC_MANIFEST_OK on match, @c SC_MANIFEST_ERR_MODULE_MISMATCH
 *         on disagreement.
 */
sc_manifest_status_t sc_manifest_check_module_match(
    const sc_manifest_t *manifest,
    const char *expected_module_name);

/**
 * @brief Verify the optional ed25519 signature. NOT IMPLEMENTED.
 *
 * Always returns @c SC_MANIFEST_ERR_SIGNATURE_NOT_SUPPORTED for now;
 * callers that require a signed manifest must treat the manifest as
 * unverified. Once an ed25519 backend lands, this entry point will
 * verify the signature against a host-side public-key set.
 */
sc_manifest_status_t sc_manifest_verify_signature(
    const sc_manifest_t *manifest);

#ifdef __cplusplus
}
#endif

#endif /* SC_MANIFEST_H */
