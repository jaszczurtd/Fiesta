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
    /** Phase 6.3 — Linux watcher hit its deadline without seeing the
     *  RPI-RP2 / RP2350 mass-storage drive appear under any of the
     *  candidate parent directories. */
    SC_FLASH_ERR_BOOTSEL_TIMEOUT,
    /** Phase 6.4 — could not open / write the destination
     *  `<drive_path>/firmware.uf2`. Reported alongside the diagnostic
     *  string so the operator can see the underlying errno. */
    SC_FLASH_ERR_FILE_WRITE,
    /** Phase 6.4 — re-enumeration waiter exhausted its budget without
     *  seeing a `/dev/serial/by-id/` entry matching the supplied UID
     *  suffix. */
    SC_FLASH_ERR_REENUM_TIMEOUT,
    /** Reserved for backends that have no implementation on the
     *  current platform (Windows BOOTSEL watcher today). */
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

/**
 * @brief Watch for the RP2040 BOOTSEL mass-storage drive to appear and
 *        return its mount path as soon as it does.
 *
 * Linux: polls `/media/$USER/` and `/run/media/$USER/` at ~100 ms
 * intervals until @p timeout_ms elapses, looking for a directory entry
 * whose name starts with `RPI-RP2` (covers `RPI-RP2` for RP2040 and
 * `RPI-RP2350` for RP2350) or equals `RP2350`. On the first match the
 * full mount path is written to @p out_path and the function returns
 * @c SC_FLASH_OK. On deadline expiry it returns
 * @c SC_FLASH_ERR_BOOTSEL_TIMEOUT. The session that triggered the
 * BOOTSEL request (Phase 5 reboot ACK + ROM jump) typically settles
 * within 1–3 seconds; callers should pass at least 5000 ms of margin.
 *
 * Windows: stub returning @c SC_FLASH_ERR_NOT_IMPLEMENTED. Function
 * shape is OS-neutral so the future Windows backend slots in cleanly.
 *
 * Always writes a NUL-terminated diagnostic into @p error_buf when
 * non-NULL: the matched mount path on success, the timeout duration on
 * BOOTSEL_TIMEOUT, the missing $USER on a degenerate Linux env.
 *
 * @p out_path / @p error_buf may be NULL if the caller does not need
 * the corresponding output; @p out_path_size / @p error_size are then
 * ignored.
 */
sc_flash_status_t sc_flash_watch_for_bootsel(uint32_t timeout_ms,
                                             char *out_path,
                                             size_t out_path_size,
                                             char *error_buf,
                                             size_t error_size);

/**
 * @brief Test-only entry point: same contract as
 *        @ref sc_flash_watch_for_bootsel but with caller-supplied
 *        parent directories instead of the OS-standard `/media/$USER/`
 *        and `/run/media/$USER/`.
 *
 * The bench-time public function is a thin wrapper over this; tests
 * use it to point the watcher at an `mkdtemp` fixture so the suite
 * stays hermetic. Available on every platform that implements the
 * polling backend (Linux today; the Windows shape lives in the
 * production wrapper, never reaches this entry point).
 */
sc_flash_status_t sc_flash__watch_for_bootsel_in(
    const char *const *parent_dirs, size_t parent_count,
    uint32_t timeout_ms,
    char *out_path, size_t out_path_size,
    char *error_buf, size_t error_size);

/**
 * @brief Progress callback invoked after every chunk written by
 *        @ref sc_flash_copy_uf2.
 *
 * @p bytes_written is the cumulative count after the latest write;
 * @p total_bytes is the source file size, fixed for the duration of
 * the copy. The callback runs on the same thread that called
 * @ref sc_flash_copy_uf2 — typically a worker thread spawned by the
 * orchestrator (Phase 6.5) — so any GUI updates have to be
 * marshalled by the caller (e.g. via @c g_idle_add).
 */
typedef void (*sc_flash_progress_cb)(uint64_t bytes_written,
                                     uint64_t total_bytes,
                                     void *user);

/**
 * @brief Copy a UF2 file onto the BOOTSEL mass-storage drive.
 *
 * Reads @p src_uf2_path in 64 KiB chunks and writes them to
 * `<drive_path>/firmware.uf2`, invoking @p progress_cb after each
 * chunk with the running and total byte counts. Closes both files
 * and `fsync()`s the destination before returning so the kernel
 * flushes to the actual mass-storage device — the RP2040 boot ROM
 * detects the completed write and reboots into the new firmware on
 * its own.
 *
 * Source size is capped at @ref SC_FLASH_UF2_MAX_BYTES;
 * structural validation of the UF2 itself is the caller's concern
 * (use @ref sc_flash_uf2_format_check before this).
 *
 * @p progress_cb may be NULL.
 */
sc_flash_status_t sc_flash_copy_uf2(const char *src_uf2_path,
                                    const char *drive_path,
                                    sc_flash_progress_cb progress_cb,
                                    void *progress_user,
                                    char *error_buf,
                                    size_t error_size);

/**
 * @brief Wait for the firmware to re-enumerate as a USB-serial
 *        device after a flash, identified by its UID hex suffix.
 *
 * Linux: polls `/dev/serial/by-id/` every 100 ms until
 * @p timeout_ms elapses, returning the full path to the first entry
 * whose name contains @p uid_hex. The match is on UID rather than
 * arbitrary device path because the underlying inode (`/dev/ttyACM*`)
 * may change across reboots, while the UID embedded in the by-id
 * symlink name is stable.
 *
 * Windows: stub returning @c SC_FLASH_ERR_NOT_IMPLEMENTED.
 */
sc_flash_status_t sc_flash_wait_reenumeration(const char *uid_hex,
                                              uint32_t timeout_ms,
                                              char *out_path,
                                              size_t out_path_size,
                                              char *error_buf,
                                              size_t error_size);

/**
 * @brief Test-only entry point for @ref sc_flash_wait_reenumeration
 *        — drives the polling loop against a caller-supplied parent
 *        directory rather than `/dev/serial/by-id/`.
 */
sc_flash_status_t sc_flash__wait_reenumeration_in(
    const char *parent_dir,
    const char *uid_hex,
    uint32_t timeout_ms,
    char *out_path, size_t out_path_size,
    char *error_buf, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* SC_FLASH_H */
