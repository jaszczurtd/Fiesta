#!/usr/bin/env bash

fiesta_module_name() {
    basename "$1"
}

fiesta_module_token_for() {
    case "$1" in
        ECU)           printf '%s\n' "ECU" ;;
        Clocks)        printf '%s\n' "CLOCKS" ;;
        OilAndSpeed)   printf '%s\n' "OIL&SPD" ;;
        Fiesta_clock)  printf '%s\n' "RTC_CLK" ;;
        Adjustometer)  printf '%s\n' "ADJ" ;;
        *)
            return 1
            ;;
    esac
}

fiesta_usb_manufacturer() {
    printf '%s\n' "Jaszczur"
}

fiesta_usb_product_for() {
    case "$1" in
        ECU)           printf '%s\n' "Fiesta ECU" ;;
        Clocks)        printf '%s\n' "Fiesta Clocks" ;;
        OilAndSpeed)   printf '%s\n' "Fiesta OilAndSpeed" ;;
        Fiesta_clock)  printf '%s\n' "Fiesta RTC Clock" ;;
        Adjustometer)  printf '%s\n' "Fiesta Adjustometer" ;;
        *)             printf 'Fiesta %s\n' "$1" ;;
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

    cli_path=$(fiesta_read_json_setting "$settings_file" "jaszczurhal.cliPath" "")
    if [[ -n "$cli_path" ]] && command -v "$cli_path" >/dev/null 2>&1; then
        command -v "$cli_path"
        return 0
    fi

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
    local jh_project_file="$project_dir/.vscode/jaszczurhal.project.json"
    local arduino_json="$project_dir/.vscode/arduino.json"
    local fqbn board config

    if [[ -n "${FIESTA_ARDUINO_FQBN:-}" ]]; then
        printf '%s\n' "$FIESTA_ARDUINO_FQBN"
        return 0
    fi

    fqbn=$(fiesta_read_json_setting "$jh_project_file" "fqbn" "")
    if [[ -n "$fqbn" ]]; then
        printf '%s\n' "$fqbn"
        return 0
    fi

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

fiesta_cmake_bool() {
    if fiesta_truthy "${1:-0}"; then
        printf '%s\n' "ON"
    else
        printf '%s\n' "OFF"
    fi
}

fiesta_firmware_cmake_source_dir() {
    # JaszczurHAL multi-target dispatcher (replaces the retired in-repo
    # FiestaArduinoFirmware recipe). Arg: the libraries dir (parent of
    # JaszczurHAL). This script path targets rp2040.
    local libraries_dir="$1"
    printf '%s\n' "$libraries_dir/JaszczurHAL/cmake/jh_firmware_project"
}

fiesta_firmware_cmake_build_dir() {
    local project_dir="$1"
    printf '%s\n' "$project_dir/.build/cmake"
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
    local cmake_src cmake_build target
    local usb_manufacturer usb_product
    local cmake_werror cmake_verbose

    : "$sketch_dir"
    module=$(fiesta_module_name "$project_dir")
    settings_file="$project_dir/.vscode/settings.json"
    build_dir="$project_dir/.build"

    if [[ -n "${FIESTA_ARDUINO_CLI:-}" ]]; then
        cli="$FIESTA_ARDUINO_CLI"
    else
        cli=$(fiesta_find_arduino_cli "$settings_file") || return 1
    fi
    if ! command -v cmake >/dev/null 2>&1; then
        echo "cmake not found" >&2
        return 1
    fi

    if [[ -n "${FIESTA_ARDUINO_FQBN:-}" ]]; then
        fqbn="$FIESTA_ARDUINO_FQBN"
    else
        fqbn=$(fiesta_resolve_fqbn "$project_dir") || return 1
    fi
    sketchbook=$(fiesta_read_json_setting "$settings_file" "arduino.sketchbookPath" "")
    if [[ -n "${FIESTA_LIBRARIES_DIR:-}" ]]; then
        libraries_dir="$FIESTA_LIBRARIES_DIR"
    else
        libraries_dir=$(fiesta_resolve_libraries_dir "$sketchbook" "$project_dir" || true)
    fi
    usb_manufacturer=$(fiesta_usb_manufacturer)
    usb_product=$(fiesta_usb_product_for "$module")
    cmake_src=$(fiesta_firmware_cmake_source_dir "$libraries_dir")
    cmake_build=$(fiesta_firmware_cmake_build_dir "$project_dir")
    cmake_werror=$(fiesta_cmake_bool "$include_werror")
    cmake_verbose=$(fiesta_cmake_bool "$verbose")

    mkdir -p "$build_dir"

    # Build through the JaszczurHAL multi-target dispatcher (rp2040 target). The
    # former FIESTA_* cache vars map onto the dispatcher's names; the rp2040
    # recipe discovers JaszczurHAL/Credentials/canDefinitions via --libraries and
    # generates the Fiesta entry adapter from firmware_entry.h. --warnings all is
    # always on in the recipe (matches the modules' FIESTA_WARNINGS=true).
    cmake -S "$cmake_src" -B "$cmake_build" \
        -DJH_TARGET=rp2040 \
        -DJH_PROJECT_DIR="$project_dir" \
        -DARDUINO_CLI="$cli" \
        -DARDUINO_FQBN="$fqbn" \
        -DARDUINO_UPLOAD_PORT="$port" \
        -DARDUINO_VERBOSE="$cmake_verbose" \
        -DARDUINO_LIBRARIES="$libraries_dir" \
        -DARDUINO_WERROR="$cmake_werror" \
        -DJH_USB_MANUFACTURER="$usb_manufacturer" \
        -DJH_USB_PRODUCT="$usb_product" \
        >/dev/null

    case "$mode" in
        build)                target="firmware" ;;
        debug)                target="firmware_debug" ;;
        upload)               target="firmware_upload" ;;
        compilation-database) target="firmware_compile_db" ;;
        *)                    return 2 ;;
    esac

    cmake --build "$cmake_build" --target "$target"
}

fiesta_find_uf2_artifact() {
    local build_dir="$1"

    python3 - "$build_dir" <<'PYEOF'
import pathlib
import sys

build_dir = pathlib.Path(sys.argv[1])
if not build_dir.exists():
    raise SystemExit

module_name = build_dir.parent.name
preferred_names = []
if module_name:
    preferred_names.append(f"{module_name}.uf2")
preferred_names.append("firmware.uf2")

for name in preferred_names:
    preferred = build_dir / name
    if preferred.is_file():
        print(str(preferred))
        raise SystemExit

candidates = [p for p in build_dir.rglob("*.uf2") if p.is_file()]
if not candidates:
    raise SystemExit

latest = max(candidates, key=lambda p: p.stat().st_mtime)
print(str(latest))
PYEOF
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
