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

if [[ $# -lt 1 ]]; then
    err "Usage: $0 <project-dir>"
    exit 2
fi

PROJECT_DIR="$1"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
BUILD_DIR="$PROJECT_DIR/.build"
MODULE="$(fiesta_module_name "$PROJECT_DIR")"

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

info "Compiling..."
info "  FQBN: $FQBN"
if ! fiesta_run_compile "$PROJECT_DIR" build "$PROJECT_DIR" "$(fiesta_module_use_werror "$MODULE")" 0 0 ""; then
    err "Compilation failed"
    exit 1
fi

echo ""
info "Searching for UF2 file..."
UF2="$(fiesta_find_uf2_artifact "$BUILD_DIR" || true)"

if [[ -z "$UF2" ]]; then
    err "No .uf2 file found in $BUILD_DIR"
    exit 1
fi

ok "Found: $UF2"

MANIFEST="$(fiesta_prepare_manifest_for_uf2 "$PROJECT_DIR" "$UF2" || true)"
if [[ -z "$MANIFEST" ]]; then
    err "Manifest generation/verification failed for $UF2"
    exit 1
fi
ok "Manifest ready: $MANIFEST"

info "Searching for BOOTSEL drive..."
MOUNT="$(fiesta_ensure_bootsel_mount "$USER" || true)"

if [[ -z "$MOUNT" ]]; then
    err "BOOTSEL drive not found or could not be mounted"
    echo ""
    echo "  Instructions:"
    echo "  1. Unplug the board from USB"
    echo "  2. Hold the BOOTSEL button"
    echo "  3. Plug USB in while holding BOOTSEL"
    echo "  4. Release BOOTSEL - an RPI-RP2 drive should appear"
    echo "  5. Run this script again"
    echo ""
    echo "  Mounted drives in /media/$USER/:"
    ls "/media/$USER"/ 2>/dev/null || echo "    (none)"
    echo ""
    echo "  BOOTSEL labels in /dev/disk/by-label/:"
    ls /dev/disk/by-label 2>/dev/null | grep -E '^(RPI-RP2|RP2350|RPI-RP2350)$' || echo "    (none)"
    exit 1
fi

info "Copying to $MOUNT..."
cp "$UF2" "$MOUNT/"
sync

echo ""
ok "UF2 upload finished"
ok "File: $(basename "$UF2") -> $MOUNT/"
ok "Preflight: manifest module + sha256 verified"
