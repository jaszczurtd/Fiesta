#!/usr/bin/env bash

fiesta_module_name() {
    basename "$1"
}

fiesta_module_token_for() {
    case "$1" in
        ECU)           printf '%s\n' "ECU" ;;
        Clocks)        printf '%s\n' "CLOCKS" ;;
        OilAndSpeed)   printf '%s\n' "OIL&SPD" ;;
        Adjustometer)  printf '%s\n' "ADJ" ;;
        *)
            return 1
            ;;
    esac
}

fiesta_module_usb_by_id_tag_for() {
    case "$1" in
        ECU)           printf '%s\n' "Fiesta_ECU" ;;
        Clocks)        printf '%s\n' "Fiesta_Clocks" ;;
        OilAndSpeed)   printf '%s\n' "Fiesta_OilAndSpeed" ;;
        Adjustometer)  printf '%s\n' "Fiesta_Adjustometer" ;;
        *)
            return 1
            ;;
    esac
}

fiesta_serial_by_id_dir() {
    printf '%s\n' "${FIESTA_SERIAL_BY_ID_DIR:-/dev/serial/by-id}"
}

fiesta_canonical_path() {
    local path="$1"
    if [[ -z "$path" ]]; then
        printf '\n'
        return 0
    fi
    readlink -f "$path" 2>/dev/null || printf '%s\n' "$path"
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

fiesta_run_upload_from_file() {
    local project_dir="$1"
    local uf2_path="$2"
    local port="${3:-}"
    local verbose="${4:-0}"

    local settings_file cli fqbn
    local cmd=()

    settings_file="$project_dir/.vscode/settings.json"

    cli=$(fiesta_find_arduino_cli "$settings_file") || return 1
    fqbn=$(fiesta_resolve_fqbn "$project_dir") || return 1

    cmd=("$cli" upload --fqbn "$fqbn" --input-file "$uf2_path")

    if [[ -n "$port" ]]; then
        cmd+=(--port "$port")
    fi

    if fiesta_truthy "$verbose"; then
        cmd+=(-v)
    fi

    "${cmd[@]}"
}

fiesta_start_persistent_monitor() {
    local project_dir="$1"
    local preferred_port="${2:-}"
    local monitor="$project_dir/scripts/serial-persistent.py"

    [[ -f "$monitor" ]] || return 0

    # Keep exactly one monitor instance per project to avoid mixed serial output.
    if pgrep -f -- "$project_dir/scripts/serial-persistent.py" >/dev/null 2>&1 \
        || pgrep -f -- "$project_dir/scripts/serial-monitor.py" >/dev/null 2>&1 \
        || pgrep -f -- "fiesta-serial-persistent.py .*--project-dir $project_dir" >/dev/null 2>&1; then
        return 0
    fi

    if [[ -n "$preferred_port" ]]; then
        nohup python3 "$monitor" "$preferred_port" -m pico >/tmp/fiesta-persistent-monitor.log 2>&1 &
    else
        nohup python3 "$monitor" -m pico >/tmp/fiesta-persistent-monitor.log 2>&1 &
    fi
}

fiesta_find_uf2_artifact() {
    local build_dir="$1"

    python3 - "$build_dir" <<'PYEOF'
import pathlib
import sys

build_dir = pathlib.Path(sys.argv[1])
if not build_dir.exists():
    raise SystemExit

candidates = [p for p in build_dir.rglob("*.uf2") if p.is_file()]
if not candidates:
    raise SystemExit

latest = max(candidates, key=lambda p: p.stat().st_mtime)
print(str(latest))
PYEOF
}

fiesta_list_module_serial_ports() {
    local module_name="$1"
    local by_id_dir tag
    by_id_dir="$(fiesta_serial_by_id_dir)"
    tag="$(fiesta_module_usb_by_id_tag_for "$module_name")" || return 1

    [[ -d "$by_id_dir" ]] || return 0

    declare -A seen=()
    local link resolved entry
    while IFS= read -r link; do
        resolved="$(fiesta_canonical_path "$link")"
        if [[ -z "$resolved" ]]; then
            continue
        fi
        if [[ -z "${seen[$resolved]+x}" ]]; then
            entry="$resolved|$(basename "$link")"
            seen[$resolved]="$entry"
            printf '%s\n' "$entry"
        fi
    done < <(find "$by_id_dir" -maxdepth 1 -type l -name "usb-*${tag}*" -print | sort)
}

fiesta_print_fiesta_port_map() {
    local by_id_dir link target
    by_id_dir="$(fiesta_serial_by_id_dir)"
    if [[ ! -d "$by_id_dir" ]]; then
        echo "  (no $by_id_dir directory)"
        return 0
    fi

    local any=0
    while IFS= read -r link; do
        any=1
        target="$(fiesta_canonical_path "$link")"
        echo "  $(basename "$link") -> $target"
    done < <(find "$by_id_dir" -maxdepth 1 -type l -name "usb-*Fiesta_*" -print | sort)

    if [[ "$any" -eq 0 ]]; then
        echo "  (no Fiesta serial entries in $by_id_dir)"
    fi
}

fiesta_is_debug_probe_device_name() {
    local base_name="$1"
    case "$base_name" in
        *Debug_Probe*|*Picoprobe*|*CMSIS-DAP*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

fiesta_is_fresh_pico_device_name() {
    local base_name="$1"
    case "$base_name" in
        *Fiesta_*)
            return 1
            ;;
        *Raspberry_Pi_Pico*|*RP2040*|*RP2350*|*MicroPython_Board*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

fiesta_list_fresh_pico_serial_ports() {
    local by_id_dir
    by_id_dir="$(fiesta_serial_by_id_dir)"

    [[ -d "$by_id_dir" ]] || return 0

    declare -A seen=()
    local link base_name resolved entry
    while IFS= read -r link; do
        base_name="$(basename "$link")"
        if fiesta_is_debug_probe_device_name "$base_name"; then
            continue
        fi
        if ! fiesta_is_fresh_pico_device_name "$base_name"; then
            continue
        fi

        resolved="$(fiesta_canonical_path "$link")"
        if [[ -z "$resolved" ]]; then
            continue
        fi
        if [[ -z "${seen[$resolved]+x}" ]]; then
            entry="$resolved|$base_name"
            seen[$resolved]="$entry"
            printf '%s\n' "$entry"
        fi
    done < <(find "$by_id_dir" -maxdepth 1 -type l -name "usb-*" -print | sort)
}

fiesta_resolve_upload_port() {
    local project_dir="$1"
    local preferred_port="${2:-}"

    local module_name preferred_canon by_id_dir
    module_name="$(fiesta_module_name "$project_dir")"
    preferred_canon="$(fiesta_canonical_path "$preferred_port")"
    by_id_dir="$(fiesta_serial_by_id_dir)"

    local detected=()
    local item
    while IFS= read -r item; do
        detected+=("$item")
    done < <(fiesta_list_module_serial_ports "$module_name" || true)

    if [[ "${#detected[@]}" -eq 1 ]]; then
        local det_port="${detected[0]%%|*}"
        local det_dev="${detected[0]#*|}"
        printf '%s|auto:%s|%s\n' "$det_port" "$module_name" "$det_dev"
        return 0
    fi

    if [[ "${#detected[@]}" -gt 1 ]]; then
        if [[ -n "$preferred_canon" ]]; then
            for item in "${detected[@]}"; do
                local item_port="${item%%|*}"
                local item_dev="${item#*|}"
                if [[ "$(fiesta_canonical_path "$item_port")" == "$preferred_canon" ]]; then
                    printf '%s|settings-among-multiple:%s|%s\n' "$item_port" "$module_name" "$item_dev"
                    return 0
                fi
            done
        fi

        echo "Multiple serial ports detected for module '$module_name':" >&2
        for item in "${detected[@]}"; do
            local item_port="${item%%|*}"
            local item_dev="${item#*|}"
            echo "  - $item_port ($item_dev)" >&2
        done
        echo "Set arduino.uploadPort to one of the listed ports." >&2
        echo "Visible Fiesta serial map:" >&2
        fiesta_print_fiesta_port_map >&2
        return 1
    fi

    local fresh_detected=()
    while IFS= read -r item; do
        fresh_detected+=("$item")
    done < <(fiesta_list_fresh_pico_serial_ports || true)

    if [[ "${#fresh_detected[@]}" -eq 1 ]]; then
        local fresh_port="${fresh_detected[0]%%|*}"
        local fresh_dev="${fresh_detected[0]#*|}"
        printf '%s|fresh:auto:%s|%s\n' "$fresh_port" "$module_name" "$fresh_dev"
        return 0
    fi

    if [[ "${#fresh_detected[@]}" -gt 1 ]]; then
        if [[ -n "$preferred_canon" ]]; then
            for item in "${fresh_detected[@]}"; do
                local fresh_port="${item%%|*}"
                local fresh_dev="${item#*|}"
                if [[ "$(fiesta_canonical_path "$fresh_port")" == "$preferred_canon" ]]; then
                    printf '%s|fresh:settings-among-multiple:%s|%s\n' "$fresh_port" "$module_name" "$fresh_dev"
                    return 0
                fi
            done
        fi

        echo "Multiple fresh Pico devices detected (not yet labeled as Fiesta modules):" >&2
        for item in "${fresh_detected[@]}"; do
            local fresh_port="${item%%|*}"
            local fresh_dev="${item#*|}"
            echo "  - $fresh_port ($fresh_dev)" >&2
        done
        echo "Set arduino.uploadPort to the intended fresh device and rerun upload." >&2
        return 1
    fi

    local visible_fiesta_links=()
    if [[ -d "$by_id_dir" ]]; then
        while IFS= read -r item; do
            visible_fiesta_links+=("$item")
        done < <(find "$by_id_dir" -maxdepth 1 -type l -name "usb-*Fiesta_*" -print)
    fi

    if [[ "${#visible_fiesta_links[@]}" -gt 0 ]]; then
        echo "No '$module_name' module detected on serial by-id, but other Fiesta modules are visible." >&2
        echo "Refusing fallback to prevent cross-module flash." >&2
        echo "Visible Fiesta serial map:" >&2
        fiesta_print_fiesta_port_map >&2
        return 1
    fi

    if [[ -n "$preferred_port" ]]; then
        if [[ -e "$preferred_port" || -e "$preferred_canon" ]]; then
            printf '%s|settings-fallback:%s|manual-or-unknown\n' "$preferred_canon" "$module_name"
            return 0
        fi
        echo "Preferred upload port does not exist: $preferred_port" >&2
    fi

    echo "No serial port auto-detected for module '$module_name'." >&2
    echo "Visible Fiesta serial map:" >&2
    fiesta_print_fiesta_port_map >&2
    return 1
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

fiesta_manifest_path_for_uf2() {
    local uf2_path="$1"

    case "$uf2_path" in
        *.uf2)
            printf '%s\n' "${uf2_path%.uf2}.manifest.json"
            ;;
        *)
            printf '%s.manifest.json\n' "$uf2_path"
            ;;
    esac
}

fiesta_sha256_hex() {
    local file_path="$1"

    python3 - "$file_path" <<'PYEOF'
import hashlib
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
if not path.is_file():
    raise SystemExit(1)

h = hashlib.sha256()
with path.open("rb") as handle:
    for chunk in iter(lambda: handle.read(1024 * 1024), b""):
        h.update(chunk)
print(h.hexdigest())
PYEOF
}

fiesta_manifest_fw_version_for() {
    local project_dir="$1"
    local default="${2:-0.1.0}"
    local config_file="$project_dir/config.h"

    python3 - "$config_file" "$default" <<'PYEOF'
import re
import sys

config_file, default = sys.argv[1:3]
try:
    text = open(config_file, "r", encoding="utf-8", errors="ignore").read()
except Exception:
    print(default)
    raise SystemExit

match = re.search(r'^\s*#\s*define\s+FW_VERSION\s+"([^"]+)"', text, flags=re.MULTILINE)
if match:
    print(match.group(1))
else:
    print(default)
PYEOF
}

fiesta_now_build_id() {
    date '+%Y-%m-%d %H:%M:%S'
}

fiesta_manifest_build_id_for() {
    local project_dir="$1"
    local default="${2:-$(fiesta_now_build_id)}"
    local config_file="$project_dir/config.h"

    python3 - "$config_file" "$default" <<'PYEOF'
import re
import sys

config_file, default = sys.argv[1:3]
try:
    text = open(config_file, "r", encoding="utf-8", errors="ignore").read()
except Exception:
    print(default)
    raise SystemExit

match = re.search(r'^\s*#\s*define\s+BUILD_ID\s+"([^"]+)"', text, flags=re.MULTILINE)
if match:
    print(match.group(1))
else:
    print(default)
PYEOF
}

fiesta_generate_manifest() {
    local project_dir="$1"
    local uf2_path="$2"
    local manifest_path="${3:-}"

    local module_name module_token fw_version build_id sha256_hex uf2_file

    module_name=$(fiesta_module_name "$project_dir")
    module_token=$(fiesta_module_token_for "$module_name") || return 1
    fw_version=$(fiesta_manifest_fw_version_for "$project_dir" "0.1.0")
    build_id=$(fiesta_manifest_build_id_for "$project_dir" "$(fiesta_now_build_id)")
    sha256_hex=$(fiesta_sha256_hex "$uf2_path") || return 1
    uf2_file=$(basename "$uf2_path")

    if [[ -z "$manifest_path" ]]; then
        manifest_path=$(fiesta_manifest_path_for_uf2 "$uf2_path")
    fi

    python3 - "$manifest_path" "$module_token" "$fw_version" "$build_id" "$sha256_hex" "$uf2_file" <<'PYEOF'
import json
import pathlib
import sys

manifest_path, module_name, fw_version, build_id, sha256_hex, uf2_file = sys.argv[1:7]

payload = {
    "module_name": module_name,
    "fw_version": fw_version,
    "build_id": build_id,
    "sha256": sha256_hex,
    "uf2_file": uf2_file,
}

path = pathlib.Path(manifest_path)
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
PYEOF

    printf '%s\n' "$manifest_path"
}

fiesta_verify_manifest() {
    local project_dir="$1"
    local manifest_path="$2"
    local uf2_path="$3"

    local module_name module_token

    module_name=$(fiesta_module_name "$project_dir")
    module_token=$(fiesta_module_token_for "$module_name") || return 1

    python3 - "$manifest_path" "$uf2_path" "$module_token" <<'PYEOF'
import hashlib
import json
import pathlib
import re
import sys

manifest_path, uf2_path, expected_module = sys.argv[1:4]

manifest_file = pathlib.Path(manifest_path)
uf2_file = pathlib.Path(uf2_path)
if not manifest_file.is_file():
    print(f"manifest not found: {manifest_file}", file=sys.stderr)
    raise SystemExit(1)
if not uf2_file.is_file():
    print(f"uf2 not found: {uf2_file}", file=sys.stderr)
    raise SystemExit(1)

try:
    data = json.loads(manifest_file.read_text(encoding="utf-8"))
except Exception as exc:
    print(f"manifest parse failed: {exc}", file=sys.stderr)
    raise SystemExit(1)

required = ("module_name", "fw_version", "build_id", "sha256")
for key in required:
    value = data.get(key)
    if not isinstance(value, str) or not value:
        print(f"manifest missing/invalid field: {key}", file=sys.stderr)
        raise SystemExit(1)

uf2_file_from_manifest = data.get("uf2_file")
if not isinstance(uf2_file_from_manifest, str) or not uf2_file_from_manifest:
    print("manifest missing/invalid field: uf2_file", file=sys.stderr)
    raise SystemExit(1)
if "/" in uf2_file_from_manifest or "\\" in uf2_file_from_manifest:
    print("manifest uf2_file must be a basename (no path separators)", file=sys.stderr)
    raise SystemExit(1)
if uf2_file_from_manifest in (".", ".."):
    print("manifest uf2_file cannot be '.' or '..'", file=sys.stderr)
    raise SystemExit(1)
if uf2_file_from_manifest != uf2_file.name:
    print(
        f"manifest uf2_file mismatch: manifest={uf2_file_from_manifest} artifact={uf2_file.name}",
        file=sys.stderr,
    )
    raise SystemExit(1)

if data["module_name"] != expected_module:
    print(
        f"manifest module mismatch: manifest={data['module_name']} expected={expected_module}",
        file=sys.stderr,
    )
    raise SystemExit(1)

sha = data["sha256"]
if re.fullmatch(r"[0-9a-f]{64}", sha) is None:
    print("manifest sha256 must be 64 lowercase hex chars", file=sys.stderr)
    raise SystemExit(1)

artifact_sha = hashlib.sha256(uf2_file.read_bytes()).hexdigest()
if artifact_sha != sha:
    print(
        f"manifest sha256 mismatch: manifest={sha} actual={artifact_sha}",
        file=sys.stderr,
    )
    raise SystemExit(1)
PYEOF
}

fiesta_prepare_manifest_for_uf2() {
    local project_dir="$1"
    local uf2_path="$2"
    local manifest_path="${3:-}"

    if [[ -z "$manifest_path" ]]; then
        manifest_path=$(fiesta_manifest_path_for_uf2 "$uf2_path")
    fi

    fiesta_generate_manifest "$project_dir" "$uf2_path" "$manifest_path" >/dev/null
    fiesta_verify_manifest "$project_dir" "$manifest_path" "$uf2_path"
    printf '%s\n' "$manifest_path"
}
