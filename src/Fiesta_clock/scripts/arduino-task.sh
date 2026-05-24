#!/usr/bin/env bash
# =============================================================================
# Unified Arduino task runner (build/build-debug/upload)
# Includes automatic Pico board/port detection before each action.
# =============================================================================
set -euo pipefail

MODE="${1:-}"
if [[ -z "$MODE" ]]; then
    echo "Usage: $0 <build|build-debug|upload>"
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
BUILD_DIR="$PROJECT_DIR/.build"
AUTO_DETECT="$SCRIPT_DIR/auto-detect-board.py"
MONITOR_SCRIPT="$SCRIPT_DIR/serial-persistent.py"

read_setting() {
    local key="$1"
    local default_val="${2:-}"
    python3 - "$SETTINGS_FILE" "$key" "$default_val" <<'PY'
import json
import sys

settings_file, key, default_value = sys.argv[1:4]
try:
    with open(settings_file, "r", encoding="utf-8") as f:
        data = json.load(f)
except Exception:
    print(default_value)
    raise SystemExit(0)

value = data.get(key, default_value)
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print(default_value)
else:
    print(value)
PY
}

if [[ -f "$AUTO_DETECT" ]]; then
    # Best-effort only. Do not block build/upload if detection fails.
    python3 "$AUTO_DETECT" --settings "$SETTINGS_FILE" --cli "$(read_setting "arduino.cliPath" "arduino-cli")" --quiet || true
fi

CLI="$(read_setting "arduino.cliPath" "arduino-cli")"
FQBN="$(read_setting "arduino.fqbn" "")"
SKETCHBOOK="$(read_setting "arduino.sketchbookPath" "")"
PORT="$(read_setting "arduino.uploadPort" "")"
VERBOSE="$(read_setting "arduino.verbose" "false")"

if [[ -z "$CLI" ]]; then
    CLI="arduino-cli"
fi

if [[ -z "$FQBN" ]]; then
    echo "[ERROR] Missing arduino.fqbn in $SETTINGS_FILE"
    exit 1
fi

LIB_ARGS=()
if [[ -n "$SKETCHBOOK" && -d "$SKETCHBOOK/libraries" ]]; then
    LIB_ARGS=(--libraries "$SKETCHBOOK/libraries")
fi

COMMON_ARGS=(
    --fqbn "$FQBN"
    --build-path "$BUILD_DIR"
    "${LIB_ARGS[@]}"
    --build-property "compiler.cpp.extra_flags=-I '$PROJECT_DIR'"
    --build-property "compiler.c.extra_flags=-I '$PROJECT_DIR'"
)

VFLAG=()
if [[ "$VERBOSE" == "true" ]]; then
    VFLAG=(-v)
fi

case "$MODE" in
    build)
        "$CLI" compile "${COMMON_ARGS[@]}" --warnings all "${VFLAG[@]}" "$PROJECT_DIR"
        ;;

    build-debug)
        "$CLI" compile "${COMMON_ARGS[@]}" --optimize-for-debug --warnings all "$PROJECT_DIR"
        ;;

    upload)
        UPLOAD_ARGS=(--upload)
        UPLOAD_TARGET=""
        if [[ -n "$PORT" && -e "$PORT" ]]; then
            UPLOAD_ARGS+=(--port "$PORT")
            UPLOAD_TARGET="$PORT"
        elif "$CLI" board list | grep -q '^UF2_Board[[:space:]]'; then
            echo "[INFO] Serial port '$PORT' not available; detected UF2_Board (BOOTSEL), forcing --port UF2_Board"
            UPLOAD_ARGS+=(--port UF2_Board)
            UPLOAD_TARGET="UF2_Board"
        else
            echo "[WARN] Port '$PORT' not available, using auto-detect"
        fi

        MONITOR_RESTART=0
        if [[ "$UPLOAD_TARGET" == /dev/tty* ]] && pgrep -f "serial-persistent.py -m pico" >/dev/null 2>&1; then
            echo "[INFO] Stopping persistent monitor before serial upload to release port lock"
            pkill -f "serial-persistent.py -m pico" || true
            MONITOR_RESTART=1
            sleep 0.3
        fi

        UPLOAD_STATUS=0
        if ! "$CLI" compile "${COMMON_ARGS[@]}" "${UPLOAD_ARGS[@]}" --warnings all "${VFLAG[@]}" "$PROJECT_DIR"; then
            UPLOAD_STATUS=$?
        fi

        if [[ -f "$AUTO_DETECT" ]]; then
            # Re-run detection after upload because Pico can re-enumerate on a new /dev/ttyACM*.
            python3 "$AUTO_DETECT" --settings "$SETTINGS_FILE" --cli "$CLI" --quiet || true
        fi

        if [[ "$MONITOR_RESTART" -eq 1 ]]; then
            setsid -f python3 "$MONITOR_SCRIPT" -m pico --replace-existing >/tmp/fiesta-persistent-monitor.log 2>&1 || true
            echo "[INFO] Persistent monitor restarted"
        fi

        exit "$UPLOAD_STATUS"
        ;;

    *)
        echo "[ERROR] Unknown mode: $MODE"
        echo "Usage: $0 <build|build-debug|upload>"
        exit 2
        ;;
esac
