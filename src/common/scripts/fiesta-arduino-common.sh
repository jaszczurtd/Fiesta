#!/usr/bin/env bash

fiesta_module_name() {
    basename "$1"
}

fiesta_usb_manufacturer() {
    printf '%s\n' "Jaszczur"
}

fiesta_usb_product_for() {
    case "$1" in
        ECU)           printf '%s\n' "Fiesta ECU" ;;
        Clocks)        printf '%s\n' "Fiesta Clocks" ;;
        OilAndSpeed)   printf '%s\n' "Fiesta OilAndSpeed" ;;
        Adjustometer)  printf '%s\n' "Fiesta Adjustometer" ;;
        *)             printf 'Fiesta %s\n' "$1" ;;
    esac
}

fiesta_module_use_werror() {
    case "$1" in
        ECU|Adjustometer) printf '%s\n' "1" ;;
        *)                printf '%s\n' "0" ;;
    esac
}

fiesta_truthy() {
    case "${1:-}" in
        1|true|TRUE|True|yes|YES|Yes|on|ON|On)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

fiesta_read_json_setting() {
    local file="$1" key="$2" default="${3:-}"

    [[ -f "$file" ]] || {
        printf '%s\n' "$default"
        return 0
    }

    python3 - "$file" "$key" "$default" <<'PYEOF' 2>/dev/null
import json
import sys

file_path, key, default = sys.argv[1:4]

try:
    with open(file_path) as handle:
        data = json.load(handle)
except Exception:
    print(default)
    raise SystemExit

value = data.get(key, default)
if value is None:
    value = default
print(value)
PYEOF
}

fiesta_find_arduino_cli() {
    local settings_file="$1"
    local cli_path

    cli_path=$(fiesta_read_json_setting "$settings_file" "arduino.cliPath" "")
    if [[ -n "$cli_path" ]] && command -v "$cli_path" >/dev/null 2>&1; then
        command -v "$cli_path"
        return 0
    fi

    if command -v arduino-cli >/dev/null 2>&1; then
        command -v arduino-cli
        return 0
    fi

    if [[ -x "$HOME/.local/bin/arduino-cli" ]]; then
        printf '%s\n' "$HOME/.local/bin/arduino-cli"
        return 0
    fi

    return 1
}

fiesta_resolve_fqbn() {
    local project_dir="$1"
    local settings_file="$project_dir/.vscode/settings.json"
    local arduino_json="$project_dir/.vscode/arduino.json"
    local fqbn board config

    fqbn=$(fiesta_read_json_setting "$settings_file" "arduino.fqbn" "")
    if [[ -n "$fqbn" ]]; then
        printf '%s\n' "$fqbn"
        return 0
    fi

    board=$(fiesta_read_json_setting "$arduino_json" "board" "")
    config=$(fiesta_read_json_setting "$arduino_json" "configuration" "")
    if [[ -z "$board" ]]; then
        return 1
    fi

    if [[ -n "$config" ]]; then
        printf '%s:%s\n' "$board" "$config"
    else
        printf '%s\n' "$board"
    fi
}

fiesta_resolve_libraries_dir() {
    local sketchbook="$1"
    local project_dir="${2:-}"
    local candidate

    if [[ -n "$sketchbook" && -d "$sketchbook/libraries" ]]; then
        printf '%s\n' "$sketchbook/libraries"
        return 0
    fi

    if [[ -n "$project_dir" ]]; then
        candidate="$(cd "$project_dir/../../.." 2>/dev/null && pwd)/libraries"
        if [[ -d "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    fi

    for candidate in "$HOME/libraries" "$HOME/Arduino/libraries"; do
        if [[ -d "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

fiesta_find_sketch() {
    local project_dir="$1"
    local sketch

    sketch=$(find "$project_dir" -maxdepth 2 -name "*.ino" -not -path "*/.build/*" | head -1)
    if [[ -z "$sketch" ]]; then
        return 1
    fi

    printf '%s\n' "$sketch"
}

fiesta_compiler_extra_flags() {
    local include_dir="$1"
    local include_werror="${2:-0}"
    local werror_flag=""

    if fiesta_truthy "$include_werror"; then
        werror_flag=" -Werror"
    fi

    printf -- "-I '%s'%s" "$include_dir" "$werror_flag"
}

fiesta_run_compile() {
    local project_dir="$1"
    local mode="$2"
    local sketch_dir="${3:-$1}"
    local include_werror="${4:-0}"
    local include_warnings="${5:-0}"
    local verbose="${6:-0}"
    local port="${7:-}"

    local module settings_file build_dir cli fqbn sketchbook libraries_dir
    local extra_flags usb_manufacturer usb_product
    local cmd=()

    module=$(fiesta_module_name "$project_dir")
    settings_file="$project_dir/.vscode/settings.json"
    build_dir="$project_dir/.build"

    cli=$(fiesta_find_arduino_cli "$settings_file") || return 1
    fqbn=$(fiesta_resolve_fqbn "$project_dir") || return 1
    sketchbook=$(fiesta_read_json_setting "$settings_file" "arduino.sketchbookPath" "")
    libraries_dir=$(fiesta_resolve_libraries_dir "$sketchbook" "$project_dir" || true)
    extra_flags=$(fiesta_compiler_extra_flags "$sketch_dir" "$include_werror")
    usb_manufacturer=$(fiesta_usb_manufacturer)
    usb_product=$(fiesta_usb_product_for "$module")

    mkdir -p "$build_dir"

    cmd=("$cli" compile --fqbn "$fqbn" --build-path "$build_dir")

    if [[ -n "$libraries_dir" ]]; then
        cmd+=(--libraries "$libraries_dir")
    fi

    cmd+=(
        --build-property "compiler.cpp.extra_flags=$extra_flags"
        --build-property "compiler.c.extra_flags=$extra_flags"
        --build-property "build.usb_manufacturer=\"$usb_manufacturer\""
        --build-property "build.usb_product=\"$usb_product\""
    )

    case "$mode" in
        build)
            ;;
        debug)
            cmd+=(--optimize-for-debug)
            ;;
        upload)
            cmd+=(--upload)
            if [[ -n "$port" ]]; then
                cmd+=(--port "$port")
            fi
            ;;
        compilation-database)
            cmd+=(--only-compilation-database)
            ;;
        *)
            return 2
            ;;
    esac

    if fiesta_truthy "$include_warnings"; then
        cmd+=(--warnings all)
    fi

    if fiesta_truthy "$verbose"; then
        cmd+=(-v)
    fi

    cmd+=("$sketch_dir")
    "${cmd[@]}"
}

fiesta_start_persistent_monitor() {
    local project_dir="$1"
    local monitor="$project_dir/scripts/serial-persistent.py"

    [[ -f "$monitor" ]] || return 0

    if ! pgrep -f "serial-persistent.py -m pico" >/dev/null 2>&1; then
        nohup python3 "$monitor" -m pico >/tmp/fiesta-persistent-monitor.log 2>&1 &
    fi
}

fiesta_find_uf2_artifact() {
    local build_dir="$1"
    find "$build_dir" -name '*.uf2' -type f | head -1
}

fiesta_find_bootsel_mount() {
    local user_name="${1:-$USER}"
    local base name mount

    for base in "/media/$user_name" "/run/media/$user_name"; do
        [[ -d "$base" ]] || continue
        for name in RPI-RP2 RP2350 RPI-RP2350; do
            mount=$(find "$base" -maxdepth 1 -name "$name" -type d 2>/dev/null | head -1)
            if [[ -n "$mount" ]]; then
                printf '%s\n' "$mount"
                return 0
            fi
        done
    done

    return 1
}