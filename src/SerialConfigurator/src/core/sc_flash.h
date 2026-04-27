#ifndef SC_FLASH_H
#define SC_FLASH_H

/*
 * Flashing-flow primitives for the SerialConfigurator (Phase 6).
 *
 * 6.2 lands the format checker only: parse a UF2 file and verify
 * structural invariants (magic, family id, block alignment) before
 * the GUI lets the operator queue it for flashing. No transport,
 * no boot-ROM interaction, no host-side filesystem watching — those
 * arrive in 6.3 / 6.4 / 6.5.
 *
 * Keep this module GTK-free so the CLI can reuse the same checker
 * later (when Phase 6.5 wires `sc_core_flash` end-to-end).
 *
 * UF2 reference (Microsoft / RP2040 firmware-image flavour):
 *   - 512-byte blocks, no padding,
 *   - bytes  0..3  : magic1 = 0x0A324655 ("UF2\n"),
 *   - bytes  4..7  : magic2 = 0x9E5D5157,
 *   - bytes 28..31 : family id (RP2040 = 0xE48BFF56),
 *   - bytes 508..511 : end magic = 0x0AB16F30.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief UF2 block size in bytes. */
#define SC_FLASH_UF2_BLOCK_SIZE 512u

#define SC_FLASH_UF2_FIRST_MAGIC  0x0A324655u
#define SC_FLASH_UF2_SECOND_MAGIC 0x9E5D5157u
#define SC_FLASH_UF2_END_MAGIC    0x0AB16F30u
#define SC_FLASH_UF2_FAMILY_RP2040 0xE48BFF56u

/** @brief Cap on accepted UF2 file size (8 MiB — comfortably above any
 *  realistic RP2040 image). */
#define SC_FLASH_UF2_MAX_BYTES (8u * 1024u * 1024u)

/**
 * @brief Stable status enum used by every flash-flow helper.
 *
 * 6.3 / 6.4 / 6.5 will extend this with watcher / copy / orchestrator
 * codes; the existing values stay byte-stable so log greps / tests
 * keep working across phases.
 */
typedef enum sc_flash_status_t {
    SC_FLASH_OK = 0,
    SC_FLASH_ERR_NULL_ARG,
    SC_FLASH_ERR_FILE_OPEN,
    SC_FLASH_ERR_FILE_READ,
    SC_FLASH_ERR_EMPTY,
    SC_FLASH_ERR_TOO_LARGE,
    SC_FLASH_ERR_NOT_BLOCK_ALIGNED,
    SC_FLASH_ERR_BAD_FIRST_MAGIC,
    SC_FLASH_ERR_BAD_SECOND_MAGIC,
    SC_FLASH_ERR_BAD_END_MAGIC,
    SC_FLASH_ERR_WRONG_FAMILY,
    SC_FLASH_ERR_BLOCK_INDEX_OUT_OF_RANGE,
    /* Reserved for 6.3+ — exposed now so downstream files can pre-bind
     * status-token consumers without future re-renumbering. */
    SC_FLASH_ERR_NOT_IMPLEMENTED
} sc_flash_status_t;

/**
 * @brief Stable English token for log lines / CLI output. Tests
 *        match against these tokens, so do not rephrase.
 */
const char *sc_flash_status_str(sc_flash_status_t st);

/**
 * @brief Validate that @p path points to a structurally valid UF2
 *        file targeted at the RP2040 family.
 *
 * Reads the whole file into memory (capped at
 * @ref SC_FLASH_UF2_MAX_BYTES) and walks every 512-byte block:
 * magic1 / magic2 / end magic at fixed offsets, family id matches
 * RP2040, block index < total blocks, total blocks consistent across
 * blocks. Each block's payload bytes are NOT checked — that's the
 * artifact's content domain, not its structural contract.
 *
 * On success returns @c SC_FLASH_OK and writes a short OK token to
 * @p error_buf. On failure returns the appropriate status code and
 * writes a human-readable diagnostic into @p error_buf (typically
 * the offset / block index where the check failed).
 *
 * @p error_buf may be NULL if the caller does not need the
 * diagnostic; @p error_size is then ignored.
 */
sc_flash_status_t sc_flash_uf2_format_check(const char *path,
                                            char *error_buf,
                                            size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* SC_FLASH_H */
