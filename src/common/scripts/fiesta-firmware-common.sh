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

    for candidate in "$HOME/libraries"; do
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
    # JaszczurHAL multi-target dispatcher. Arg: the libraries dir (parent of
    # JaszczurHAL).
    local libraries_dir="$1"
    printf '%s\n' "$libraries_dir/JaszczurHAL/cmake/jh_firmware_project"
}

fiesta_firmware_cmake_build_dir() {
    local project_dir="$1"
    printf '%s\n' "$project_dir/.build/cmake"
}

fiesta_cmake_cache_source_dir() {
    local cache_file="$1"

    [[ -f "$cache_file" ]] || return 1
    sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache_file" | head -n 1
}

fiesta_cmake_real_dir() {
    local dir="$1"

    (cd "$dir" 2>/dev/null && pwd -P)
}

fiesta_reset_stale_cmake_cache_if_needed() {
    local source_dir="$1"
    local build_dir="$2"
    local project_dir="$3"

    local cache_file cached_source source_real cached_real project_real build_real
    cache_file="$build_dir/CMakeCache.txt"

    [[ -f "$cache_file" ]] || return 0

    cached_source=$(fiesta_cmake_cache_source_dir "$cache_file" || true)
    [[ -n "$cached_source" ]] || return 0

    source_real=$(fiesta_cmake_real_dir "$source_dir") || return 1
    cached_real=$(fiesta_cmake_real_dir "$cached_source" || printf '%s\n' "$cached_source")

    if [[ "$cached_real" == "$source_real" ]]; then
        return 0
    fi

    project_real=$(fiesta_cmake_real_dir "$project_dir") || return 1
    mkdir -p "$build_dir"
    build_real=$(fiesta_cmake_real_dir "$build_dir") || return 1

    case "$build_real" in
        "$project_real"/*) ;;
        *)
            echo "refusing to reset CMake cache outside project: $build_real" >&2
            return 1
            ;;
    esac

    echo "stale CMake cache in $build_dir points to $cached_source; resetting it" >&2
    rm -f "$cache_file"
    rm -rf "$build_dir/CMakeFiles"
}

fiesta_run_compile() {
    local project_dir="$1"
    local mode="$2"
    local sketch_dir="${3:-$1}"
    local include_werror="${4:-0}"
    local include_warnings="${5:-0}"
    local verbose="${6:-0}"
    local port="${7:-}"

    local libraries_dir jh_entry action

    # Native builds always enable warnings-as-errors. Keep the legacy function
    # signature so bootstrap callers remain source-compatible.
    : "$sketch_dir" "$include_werror" "$include_warnings" "$verbose"

    if [[ -n "${FIESTA_LIBRARIES_DIR:-}" ]]; then
        libraries_dir="$FIESTA_LIBRARIES_DIR"
    else
        libraries_dir=$(fiesta_resolve_libraries_dir "" "$project_dir" || true)
    fi
    jh_entry="$libraries_dir/JaszczurHAL/vscode/entry/jh-vscode"
    if [[ ! -x "$jh_entry" ]]; then
        echo "JaszczurHAL VS Code entry not found: $jh_entry" >&2
        return 1
    fi

    case "$mode" in
        build)                action="build" ;;
        debug)                action="build-debug" ;;
        upload)               action="upload" ;;
        compilation-database) action="refresh-intellisense" ;;
        *)                    return 2 ;;
    esac

    if [[ "$mode" == "upload" && -n "$port" ]]; then
        "$jh_entry" "$action" --project "$project_dir" --port "$port"
    else
        "$jh_entry" "$action" --project "$project_dir"
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
