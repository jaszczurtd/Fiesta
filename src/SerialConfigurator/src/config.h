#ifndef SC_CONFIG_H
#define SC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ── Flash-path persistence ───────────────────────────────────────── */

#ifdef _WIN32
//nothing yet
#else
#define SC_FLASH_PATHS_XDG "%s/fiesta-configurator/flash-paths.json"
#define SC_FLASH_PATHS_HOME "%s/.config/fiesta-configurator/flash-paths.json"
#define SC_FLASH_PATHS_TMP "/tmp/fiesta-configurator-flash-paths.json"
#endif
#define SC_FLASH_PATHS_PATH_MAX 1024u
#define SC_FLASH_PATHS_JSON_MAX_BYTES (64u * 1024u)
#define SC_MANIFEST_MAX_JSON_SIZE (64u * 1024u)
#define SC_MANIFEST_MAX_ARTIFACT_SIZE (8u * 1024u * 1024u)

/* ── Transport defaults ───────────────────────────────────────────── */

#ifdef _WIN32
//nothing yet
#else
#define SC_TRANSPORT_GLOB_PATTERN "/dev/serial/by-id/usb-Jaszczur_Fiesta_*"
#endif
#define SC_TRANSPORT_PRIMARY_TIMEOUT_MS 400
#define SC_TRANSPORT_RETRY_TIMEOUT_MS 1500
#define SC_TRANSPORT_OPEN_SETTLE_USEC 100000
#define SC_TRANSPORT_RETRY_PAUSE_USEC 150000
#define SC_TRANSPORT_HELLO_ATTEMPTS 3
#define SC_TRANSPORT_SC_ATTEMPTS 2
#define SC_TRANSPORT_MAX_CACHED_PORTS 8u

/* ── Flash flow defaults ──────────────────────────────────────────── */

#define SC_FLASH_DEFAULT_BOOTSEL_TIMEOUT_MS 60000u
#define SC_FLASH_DEFAULT_REENUM_TIMEOUT_MS 30000u
#define SC_FLASH_DEFAULT_REENUM_GRACE_MS 2500u
#define SC_FLASH_BOOTSEL_PARENT_A_FMT "/media/%s"
#define SC_FLASH_BOOTSEL_PARENT_B_FMT "/run/media/%s"
#define SC_FLASH_REENUM_PARENT_DIR "/dev/serial/by-id"
#define SC_FLASH_BOOTSEL_PARENTS_MAX 4u
#define SC_FLASH_BOOTSEL_POLL_INTERVAL_MS 100u
#define SC_FLASH_BOOTSEL_HEARTBEAT_MS 5000u
#define SC_FLASH_AUTOMOUNT_GRACE_MS 500u
#define SC_FLASH_COPY_CHUNK_BYTES (64u * 1024u)
#define SC_FLASH_COPY_DEST_FILENAME "firmware.uf2"

/* ── UI / CLI defaults ────────────────────────────────────────────── */

#define SC_APP_WINDOW_DEFAULT_WIDTH 1040
#define SC_APP_WINDOW_DEFAULT_HEIGHT 680
#define SC_RUNTIME_DETECTION_LOG_MAX 16384u
#define SC_RUNTIME_COMMAND_LOG_MAX 4096u
#define SC_UI_AUTO_REFRESH_PARAM_PROBE 0
#define SC_UI_STATUS_TEXT_MAX 160u
#define SC_UI_FLASH_PROGRESS_HEIGHT_PX 12
#define SC_UI_PROGRESSBAR_DEFAULT_HEIGHT_PX 18

/* ── Flash-tab progress tuning ────────────────────────────────────── */

#define SC_UI_FLASH_CREEP_TICK_MS 100u
#define SC_UI_FLASH_COPY_MAX_STEP 0.05
#define SC_UI_FLASH_PHASE_MAX_STEP 0.02
#define SC_UI_FLASH_CREEP_PHASE_EPSILON 0.005

#define SC_UI_FLASH_PHASE_FORMAT_CHECK_START 0.00
#define SC_UI_FLASH_PHASE_MANIFEST_VERIFY_START 0.05
#define SC_UI_FLASH_PHASE_AUTHENTICATE_START 0.10
#define SC_UI_FLASH_PHASE_REBOOT_TO_BOOTLOADER_START 0.20
#define SC_UI_FLASH_PHASE_WAIT_BOOTSEL_START 0.30
#define SC_UI_FLASH_PHASE_COPY_START 0.55
#define SC_UI_FLASH_PHASE_WAIT_REENUM_START 0.80
#define SC_UI_FLASH_PHASE_POST_FLASH_HELLO_START 0.95

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
