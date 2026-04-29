#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/fiesta-arduino-common.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*"; }

usage() {
    err "Usage: $0 <project-dir> <build|debug|upload>"
    exit 2
}

[[ $# -ge 2 ]] || usage

PROJECT_DIR="$1"
MODE="$2"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
MODULE="$(fiesta_module_name "$PROJECT_DIR")"

case "$MODE" in
    build|debug|upload)
        ;;
    *)
        usage
        ;;
esac

CLI="$(fiesta_find_arduino_cli "$SETTINGS_FILE" || true)"
if [[ -z "$CLI" ]]; then
    err "arduino-cli not found"
    exit 1
fi

FQBN="$(fiesta_resolve_fqbn "$PROJECT_DIR" || true)"
if [[ -z "$FQBN" ]]; then
    err "FQBN not set: add 'arduino.fqbn' to .vscode/settings.json or 'board' to .vscode/arduino.json"
    exit 1
fi

PORT=""
VERBOSE="$(fiesta_read_json_setting "$SETTINGS_FILE" "arduino.verbose" "false")"
VERBOSE_FLAG="0"
WERROR_FLAG="$(fiesta_module_use_werror "$MODULE")"

if [[ "$MODE" != "debug" ]] && fiesta_truthy "$VERBOSE"; then
    VERBOSE_FLAG="1"
fi

if [[ "$MODE" == "upload" ]]; then
    PORT_SETTING="$(fiesta_read_json_setting "$SETTINGS_FILE" "arduino.uploadPort" "")"
    PORT_PICKED=""
    if ! PORT_PICKED="$(fiesta_resolve_upload_port "$PROJECT_DIR" "$PORT_SETTING")"; then
        err "Could not resolve upload port for module '$MODULE'"
        exit 1
    fi
    PORT="${PORT_PICKED%%|*}"
    PORT_PICKED_REST="${PORT_PICKED#*|}"
    PORT_REASON="${PORT_PICKED_REST%%|*}"
    PORT_DEVICE="${PORT_PICKED_REST#*|}"
    fiesta_start_persistent_monitor "$PROJECT_DIR" "$PORT"
fi

info "Compiling..."
info "  FQBN: $FQBN"
if [[ "$MODE" == "upload" && -n "$PORT" ]]; then
    info "  Port: $PORT"
    info "  Detected device: $PORT_DEVICE"
    info "  Port source: $PORT_REASON"
fi

COMPILE_MODE="$MODE"
COMPILE_PORT="$PORT"
if [[ "$MODE" == "upload" ]]; then
    # Upload mode compiles first, then performs a manifest-gated upload of the
    # exact UF2 artifact selected by the preflight.
    COMPILE_MODE="build"
    COMPILE_PORT=""
fi

if ! fiesta_run_compile "$PROJECT_DIR" "$COMPILE_MODE" "$PROJECT_DIR" "$WERROR_FLAG" 1 "$VERBOSE_FLAG" "$COMPILE_PORT"; then
    err "Compilation failed"
    exit 1
fi

BUILD_DIR="$PROJECT_DIR/.build"
UF2="$(fiesta_find_uf2_artifact "$BUILD_DIR" || true)"
if [[ -z "$UF2" ]]; then
    if [[ "$MODE" == "debug" ]]; then
        warn "No UF2 artifact found in $BUILD_DIR (debug build); manifest skipped"
    else
        err "No .uf2 file found in $BUILD_DIR"
        exit 1
    fi
else
    MANIFEST="$(fiesta_prepare_manifest_for_uf2 "$PROJECT_DIR" "$UF2" || true)"
    if [[ -z "$MANIFEST" ]]; then
        err "Manifest generation/verification failed for $UF2"
        exit 1
    fi
    ok "Manifest ready: $MANIFEST"
fi

if [[ "$MODE" == "upload" ]]; then
    if [[ -z "$UF2" ]]; then
        err "Upload refused: missing UF2 artifact"
        exit 1
    fi

    info "Uploading exact UF2 selected by manifest gate..."
    if ! fiesta_run_upload_from_file "$PROJECT_DIR" "$UF2" "$PORT" "$VERBOSE_FLAG"; then
        err "Upload failed"
        exit 1
    fi

    ok "Upload finished"
else
    ok "Build finished"
fi
