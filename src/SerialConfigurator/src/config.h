#ifndef SC_CONFIG_H
#define SC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Flash-path persistence ───────────────────────────────────────── */

#ifdef _WIN32
//nothing yet
#else
/* Primary location: $XDG_CONFIG_HOME/fiesta-configurator/flash-paths.json */
#define SC_FLASH_PATHS_XDG "%s/fiesta-configurator/flash-paths.json"
/* Fallback location when XDG_CONFIG_HOME is missing: ~/.config/... */
#define SC_FLASH_PATHS_HOME "%s/.config/fiesta-configurator/flash-paths.json"
/* Last-resort location for environments without a writable home config dir. */
#define SC_FLASH_PATHS_TMP "/tmp/fiesta-configurator-flash-paths.json"
#endif
/* Maximum absolute path length handled by flash-path persistence helpers. */
#define SC_FLASH_PATHS_PATH_MAX 1024u
/* Hard cap for flash-paths.json size (defense against malformed giant files). */
#define SC_FLASH_PATHS_JSON_MAX_BYTES (64u * 1024u)
/* Hard cap for manifest.json size accepted by the parser. */
#define SC_MANIFEST_MAX_JSON_SIZE (64u * 1024u)
/* Maximum firmware artifact size (UF2) loaded for hash verification. */
#define SC_MANIFEST_MAX_ARTIFACT_SIZE (8u * 1024u * 1024u)

/* ── Diagnostics ──────────────────────────────────────────────────── */

/* Deep diagnostic logging toggle.
 *
 * When defined, the verbose diagnostic helpers (`flash_log_v` in
 * sc_flash.c and `transport_log_v` in sc_transport.c) emit per-frame
 * trace to stderr (every send/recv chunk, every readdir entry, etc.).
 * When undefined they are no-ops.
 *
 * This is a compile-time gate by design: field reports of CDC drops
 * and BOOTSEL races require the trace to be available without
 * recompiling, but on developer workstations this output is too noisy
 * to leave on permanently. Comment / uncomment below for release vs
 * debug builds. */
//#define SC_DEBUG_DEEP

/* ── Transport defaults ───────────────────────────────────────────── */

#ifdef _WIN32
//nothing yet
#else
/* Linux device discovery pattern used by Detect (by-id symlink namespace). */
#define SC_TRANSPORT_GLOB_PATTERN "/dev/serial/by-id/usb-Jaszczur_Fiesta_*"
#endif
/* First-response deadline for HELLO/SC command attempts on a warm link. */
#define SC_TRANSPORT_PRIMARY_TIMEOUT_MS 400
/* Extended timeout for retry attempts after a transient failure/reset. */
#define SC_TRANSPORT_RETRY_TIMEOUT_MS 1500
/* Pause after opening CDC ACM before first exchange (driver/device settle). */
#define SC_TRANSPORT_OPEN_SETTLE_USEC 100000
/* Delay between retries to avoid tight-loop hammering on flapping links. */
#define SC_TRANSPORT_RETRY_PAUSE_USEC 150000
/* Number of HELLO attempts before reporting transport failure. */
#define SC_TRANSPORT_HELLO_ATTEMPTS 3
/* Number of generic SC command attempts before giving up. */
#define SC_TRANSPORT_SC_ATTEMPTS 2
/* Size of open-port FD cache (keeps sessions warm across repeated operations). */
#define SC_TRANSPORT_MAX_CACHED_PORTS 8u

/* ── Flash flow defaults ──────────────────────────────────────────── */

/* Overall timeout for detecting the BOOTSEL mass-storage drive after reboot. */
#define SC_FLASH_DEFAULT_BOOTSEL_TIMEOUT_MS 60000u
/* Timeout for waiting until the flashed module re-enumerates as serial again. */
#define SC_FLASH_DEFAULT_REENUM_TIMEOUT_MS 30000u
/* Extra grace after copy to absorb USB stack jitter before strict checks. */
#define SC_FLASH_DEFAULT_REENUM_GRACE_MS 2500u
/* Candidate mount parent on many Linux desktop setups. */
#define SC_FLASH_BOOTSEL_PARENT_A_FMT "/media/%s"
/* Candidate mount parent used by some distros/desktop sessions. */
#define SC_FLASH_BOOTSEL_PARENT_B_FMT "/run/media/%s"
/* Directory polled for post-flash serial re-enumeration. */
#define SC_FLASH_REENUM_PARENT_DIR "/dev/serial/by-id"
/* Maximum count of BOOTSEL parent roots scanned per poll cycle. */
#define SC_FLASH_BOOTSEL_PARENTS_MAX 4u
/* BOOTSEL poll cadence (tradeoff: responsiveness vs filesystem churn). */
#define SC_FLASH_BOOTSEL_POLL_INTERVAL_MS 100u
/* Interval for "still waiting" heartbeat updates in logs/UI. */
#define SC_FLASH_BOOTSEL_HEARTBEAT_MS 5000u
/* Small wait after automount helper call before directory scan resumes. */
#define SC_FLASH_AUTOMOUNT_GRACE_MS 500u
/* Buffered copy chunk size for UF2 transfer to BOOTSEL drive. */
#define SC_FLASH_COPY_CHUNK_BYTES (64u * 1024u)
/* Target filename expected by RP2040/RP2350 BOOTSEL storage. */
#define SC_FLASH_COPY_DEST_FILENAME "firmware.uf2"

/* ── UI / CLI defaults ────────────────────────────────────────────── */

/* Default GUI window width on first launch (before user resize persistence). */
#define SC_APP_WINDOW_DEFAULT_WIDTH 1040
/* Default GUI window height on first launch. */
#define SC_APP_WINDOW_DEFAULT_HEIGHT 680
/* Max bytes stored for detection log text in runtime state. */
#define SC_RUNTIME_DETECTION_LOG_MAX 16384u
/* Max bytes stored for per-command log text in runtime state. */
#define SC_RUNTIME_COMMAND_LOG_MAX 4096u
/* Optional startup probe of SC_GET_PARAM data (0 = disabled by default). */
#define SC_UI_AUTO_REFRESH_PARAM_PROBE 0
/* Generic short status label buffer length in the UI layer. */
#define SC_UI_STATUS_TEXT_MAX 160u
/* Flash tab progress widget visual height (compact bar style). */
#define SC_UI_FLASH_PROGRESS_HEIGHT_PX 12
/* Generic progressbar fallback/default visual height. */
#define SC_UI_PROGRESSBAR_DEFAULT_HEIGHT_PX 18

/* ── Flash-tab progress tuning ────────────────────────────────────── */

/* UI timer step for synthetic progress creep during long non-copy phases. */
#define SC_UI_FLASH_CREEP_TICK_MS 100u
/* Upper bound for single-step jump in COPY progress animation. */
#define SC_UI_FLASH_COPY_MAX_STEP 0.05
/* Upper bound for single-step jump in non-COPY phase progress animation. */
#define SC_UI_FLASH_PHASE_MAX_STEP 0.02
/* Stop threshold for creep when phase target is effectively reached. */
#define SC_UI_FLASH_CREEP_PHASE_EPSILON 0.005

/* Progress timeline anchors (0.0-1.0) for each flash orchestrator phase. */
#define SC_UI_FLASH_PHASE_FORMAT_CHECK_START 0.00
#define SC_UI_FLASH_PHASE_MANIFEST_VERIFY_START 0.05
#define SC_UI_FLASH_PHASE_AUTHENTICATE_START 0.10
#define SC_UI_FLASH_PHASE_REBOOT_TO_BOOTLOADER_START 0.20
#define SC_UI_FLASH_PHASE_WAIT_BOOTSEL_START 0.30
#define SC_UI_FLASH_PHASE_COPY_START 0.55
#define SC_UI_FLASH_PHASE_WAIT_REENUM_START 0.80
#define SC_UI_FLASH_PHASE_POST_FLASH_HELLO_START 0.95

/* Expected phase durations used by the UI smoother/predictive progress model. */
#define SC_UI_FLASH_PHASE_FORMAT_CHECK_EXPECTED_MS 200.0
#define SC_UI_FLASH_PHASE_MANIFEST_VERIFY_EXPECTED_MS 300.0
#define SC_UI_FLASH_PHASE_AUTHENTICATE_EXPECTED_MS 500.0
#define SC_UI_FLASH_PHASE_REBOOT_TO_BOOTLOADER_EXPECTED_MS 400.0
#define SC_UI_FLASH_PHASE_WAIT_BOOTSEL_EXPECTED_MS 4000.0
#define SC_UI_FLASH_PHASE_COPY_EXPECTED_MS 3000.0
#define SC_UI_FLASH_PHASE_WAIT_REENUM_EXPECTED_MS 3000.0
#define SC_UI_FLASH_PHASE_POST_FLASH_HELLO_EXPECTED_MS 500.0

#ifdef __cplusplus
}
#endif

#endif /* SC_CONFIG_H */
