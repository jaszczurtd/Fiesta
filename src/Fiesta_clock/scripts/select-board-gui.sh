#!/usr/bin/env bash
# =============================================================================
# VS Code GUI board selector handler
# Input format (preferred): "<description> :: <fqbn>|<chip>"
# Legacy fallback:          "<fqbn> — <description>"
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
ARDUINO_JSON_FILE="$PROJECT_DIR/.vscode/arduino.json"

SELECTION="${1:-}"

if [[ -z "$SELECTION" ]]; then
    echo "[ERROR] No board selection provided."
    exit 1
fi

trim() {
    xargs <<<"$1"
}

infer_board_metadata() {
    local fqbn="$1"
    case "$fqbn" in
        rp2040:rp2040:rpipico) echo "Raspberry Pi Pico|RP2040" ;;
        rp2040:rp2040:rpipicow) echo "Raspberry Pi Pico W|RP2040" ;;
        rp2040:rp2040:rpipico2) echo "Raspberry Pi Pico 2|RP2350" ;;
        rp2040:rp2040:rpipico2w) echo "Raspberry Pi Pico 2 W|RP2350" ;;
        rp2040:rp2040:waveshare_rp2040_zero) echo "Waveshare RP2040-Zero|RP2040" ;;
        rp2040:rp2040:waveshare_rp2040_plus) echo "Waveshare RP2040-Plus|RP2040" ;;
        *) echo "$fqbn|Unknown" ;;
    esac
}

parse_selection() {
    local selection="$1"
    local desc=""
    local payload=""
    local fqbn=""
    local chip=""

    if [[ "$selection" == *"::"* && "$selection" == *"|"* ]]; then
        desc="$(trim "${selection%%::*}")"
        payload="$(trim "${selection##*::}")"
        fqbn="$(trim "${payload%%|*}")"
        chip="$(trim "${payload##*|}")"
    elif [[ "$selection" == *"—"* ]]; then
        fqbn="$(trim "$(sed 's/ *—.*//' <<<"$selection")")"
        desc="$(trim "$(sed 's/.*— *//' <<<"$selection")")"
    else
        fqbn="$(trim "$selection")"
    fi

    if [[ -z "$fqbn" ]]; then
        echo "[ERROR] Could not parse FQBN from selection: $selection"
        exit 1
    fi

    if [[ -z "$desc" || "$desc" == "$fqbn" ]]; then
        IFS='|' read -r desc chip < <(infer_board_metadata "$fqbn")
    elif [[ -z "$chip" ]]; then
        IFS='|' read -r _ chip < <(infer_board_metadata "$fqbn")
    fi

    echo "$fqbn|$desc|$chip"
}

IFS='|' read -r FQBN DESC CHIP < <(parse_selection "$SELECTION")

if [[ ! -f "$SETTINGS_FILE" ]]; then
    echo "[ERROR] Missing settings file: $SETTINGS_FILE"
    exit 1
fi

echo "Setting board: $DESC${CHIP:+ [$CHIP]}"
echo "FQBN: $FQBN"

SETTINGS_FILE="$SETTINGS_FILE" \
ARDUINO_JSON_FILE="$ARDUINO_JSON_FILE" \
FQBN_VAL="$FQBN" \
DESC_VAL="$DESC" \
python3 << 'PYEOF'
import json
import os

settings_file = os.environ["SETTINGS_FILE"]
arduino_json_file = os.environ["ARDUINO_JSON_FILE"]
fqbn = os.environ["FQBN_VAL"]
desc = os.environ["DESC_VAL"]

with open(settings_file, "r", encoding="utf-8") as f:
    settings = json.load(f)

settings["arduino.fqbn"] = fqbn
settings["arduino.boardDescription"] = desc

with open(settings_file, "w", encoding="utf-8") as f:
    json.dump(settings, f, indent=4)
    f.write("\n")

if os.path.isfile(arduino_json_file):
    with open(arduino_json_file, "r", encoding="utf-8") as f:
        arduino_cfg = json.load(f)
    arduino_cfg["board"] = fqbn
    with open(arduino_json_file, "w", encoding="utf-8") as f:
        json.dump(arduino_cfg, f, indent=4)
        f.write("\n")

print("\nOK: settings updated")
print("Run: Arduino: Build")
print("Optional: Arduino: Refresh IntelliSense")
PYEOF
