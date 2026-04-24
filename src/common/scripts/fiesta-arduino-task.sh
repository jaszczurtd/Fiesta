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
    PORT="$(fiesta_read_json_setting "$SETTINGS_FILE" "arduino.uploadPort" "")"
    fiesta_start_persistent_monitor "$PROJECT_DIR"
fi

info "Compiling..."
info "  FQBN: $FQBN"
if [[ "$MODE" == "upload" && -n "$PORT" ]]; then
    info "  Port: $PORT"
fi

if ! fiesta_run_compile "$PROJECT_DIR" "$MODE" "$PROJECT_DIR" "$WERROR_FLAG" 1 "$VERBOSE_FLAG" "$PORT"; then
    err "Compilation failed"
    exit 1
fi

if [[ "$MODE" == "upload" ]]; then
    ok "Upload finished"
else
    ok "Build finished"
fi