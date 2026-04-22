#!/usr/bin/env bash
# =============================================================================
# BOOTSEL (UF2) upload helper
# Compiles the project and copies .uf2 to mounted BOOTSEL storage
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
ARDUINO_JSON="$PROJECT_DIR/.vscode/arduino.json"
BUILD_DIR="$PROJECT_DIR/.build"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*"; }

# Read settings
read_setting() {
    local file="$1" key="$2" default="${3:-}"
    [[ -f "$file" ]] || { echo "$default"; return; }
    python3 -c "
import json, sys
try:
    with open('$file') as f:
        s = json.load(f)
except Exception:
    sys.exit(0)
print(s.get('$key', '$default'))
"
}

CLI=$(read_setting "$SETTINGS_FILE" "arduino.cliPath" "arduino-cli")
FQBN=$(read_setting "$SETTINGS_FILE" "arduino.fqbn")
SKETCHBOOK=$(read_setting "$SETTINGS_FILE" "arduino.sketchbookPath")

# Fallback: derive FQBN from .vscode/arduino.json (board + configuration)
if [[ -z "$FQBN" && -f "$ARDUINO_JSON" ]]; then
    BOARD=$(read_setting "$ARDUINO_JSON" "board")
    CONFIG=$(read_setting "$ARDUINO_JSON" "configuration")
    if [[ -n "$BOARD" ]]; then
        if [[ -n "$CONFIG" ]]; then
            FQBN="${BOARD}:${CONFIG}"
        else
            FQBN="$BOARD"
        fi
        info "FQBN loaded from arduino.json"
    fi
fi

if [[ -z "$FQBN" ]]; then
    err "FQBN not set: add 'arduino.fqbn' to .vscode/settings.json or 'board' to .vscode/arduino.json"
    exit 1
fi

LIB_ARGS=""
if [[ -n "$SKETCHBOOK" && -d "$SKETCHBOOK/libraries" ]]; then
    LIB_ARGS="--libraries $SKETCHBOOK/libraries"
else
    for candidate in "$HOME/libraries" "$HOME/Arduino/libraries"; do
        if [[ -d "$candidate" ]]; then
            LIB_ARGS="--libraries $candidate"
            info "Libraries auto-detected: $candidate"
            break
        fi
    done
fi

# Compile
info "Compiling..."
info "  FQBN: $FQBN"
if ! $CLI compile --fqbn "$FQBN" --build-path "$BUILD_DIR" $LIB_ARGS \
    --build-property "compiler.cpp.extra_flags=-I '$PROJECT_DIR' -Werror" \
    --build-property "compiler.c.extra_flags=-I '$PROJECT_DIR' -Werror" \
    "$PROJECT_DIR"; then
    err "Compilation failed"
    exit 1
fi

# Find UF2 artifact
echo ""
info "Searching for UF2 file..."
UF2=$(find "$BUILD_DIR" -name '*.uf2' -type f | head -1)

if [[ -z "$UF2" ]]; then
    err "No .uf2 file found in $BUILD_DIR"
    exit 1
fi

ok "Found: $UF2"

# Find BOOTSEL drive
info "Searching for BOOTSEL drive..."
MOUNT=""
for name in RPI-RP2 RP2350 RPI-RP2350; do
    MOUNT=$(find /media/"$USER" -maxdepth 1 -name "$name" -type d 2>/dev/null | head -1)
    if [[ -n "$MOUNT" ]]; then
        break
    fi
done

if [[ -z "$MOUNT" ]]; then
    # Also check /run/media (some distros)
    for name in RPI-RP2 RP2350 RPI-RP2350; do
        MOUNT=$(find /run/media/"$USER" -maxdepth 1 -name "$name" -type d 2>/dev/null | head -1)
        if [[ -n "$MOUNT" ]]; then
            break
        fi
    done
fi

if [[ -z "$MOUNT" ]]; then
    err "BOOTSEL drive not found"
    echo ""
    echo "  Instructions:"
    echo "  1. Unplug the board from USB"
    echo "  2. Hold the BOOTSEL button"
    echo "  3. Plug USB in while holding BOOTSEL"
    echo "  4. Release BOOTSEL - an RPI-RP2 drive should appear"
    echo "  5. Run this script again"
    echo ""
    echo "  Mounted drives in /media/$USER/:"
    ls /media/"$USER"/ 2>/dev/null || echo "    (none)"
    exit 1
fi

# Copy UF2
info "Copying to $MOUNT..."
cp "$UF2" "$MOUNT/"
sync

echo ""
ok "UF2 upload finished"
ok "File: $(basename "$UF2") -> $MOUNT/"
