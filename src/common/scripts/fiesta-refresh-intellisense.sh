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
CPP_PROPS_FILE="$PROJECT_DIR/.vscode/c_cpp_properties.json"
BUILD_DIR="$PROJECT_DIR/.build"

generate_compile_db() {
    local project_dir="$1"
    local sketch="$2"
    local sketchbook="$3"
    local sketch_dir libraries_dir werror_flag

    sketch_dir="$(dirname "$sketch")"
    libraries_dir="$(fiesta_resolve_libraries_dir "$sketchbook" || true)"
    werror_flag="$(fiesta_module_use_werror "$(fiesta_module_name "$project_dir")")"
    if [[ -n "$libraries_dir" ]]; then
        info "  Libraries: $libraries_dir"
    fi

    mkdir -p "$BUILD_DIR"

    info "Compiling project..."
    if ! fiesta_run_compile "$project_dir" build "$sketch_dir" "$werror_flag" 1 0 ""; then
        warn "Compilation failed"
        warn "IntelliSense may be incomplete, continuing anyway..."
    fi

    info "Generating compile_commands.json..."
    if fiesta_run_compile "$project_dir" compilation-database "$sketch_dir" "$werror_flag" 0 0 ""; then
        ok "compile_commands.json generated"
    else
        err "Failed to generate compile_commands.json"
        exit 1
    fi

    if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
        err "compile_commands.json does not exist after generation"
        exit 1
    fi
}

generate_cpp_properties() {
    python3 << 'PYEOF'
import json
import os

project_dir = os.environ["PROJECT_DIR"]
build_dir = os.environ["BUILD_DIR"]
settings_path = os.path.join(project_dir, ".vscode", "settings.json")
cpp_props_path = os.path.join(project_dir, ".vscode", "c_cpp_properties.json")
compile_db_path = os.path.join(build_dir, "compile_commands.json")

with open(settings_path) as f:
    settings = json.load(f)

board_desc = settings.get("arduino.boardDescription", "Arduino-Pico")
sketchbook_path = settings.get("arduino.sketchbookPath", "")

with open(compile_db_path) as f:
    commands = json.load(f)

includes = set()
defines = set()
compiler_path = ""
iprefix = ""

def process_response_file(resp_file, includes, defines, iprefix):
    if not os.path.isfile(resp_file):
        return
    with open(resp_file) as rf:
        for line in rf:
            line = line.strip()
            if not line:
                continue
            if line.startswith("-iwithprefixbefore"):
                rel = line[len("-iwithprefixbefore"):]
                if iprefix and rel:
                    full = os.path.normpath(iprefix + rel)
                    includes.add(full)
            elif line.startswith("-I"):
                path = line[2:]
                if path:
                    includes.add(path)
            elif line.startswith("-D"):
                define = line[2:]
                if define:
                    defines.add(define)

for entry in commands:
    args = entry.get("arguments", [])
    if not args:
        cmd = entry.get("command", "")
        import shlex
        try:
            args = shlex.split(cmd)
        except ValueError:
            args = cmd.split()

    if not args:
        continue

    if not compiler_path and args[0].endswith(("g++", "gcc", "arm-none-eabi-g++", "arm-none-eabi-gcc")):
        compiler_path = args[0]

    i = 0
    while i < len(args):
        arg = args[i]

        if arg.startswith("-I"):
            path = arg[2:] if len(arg) > 2 else (args[i + 1] if i + 1 < len(args) else "")
            if path:
                if not os.path.isabs(path):
                    directory = entry.get("directory", "")
                    if directory:
                        path = os.path.normpath(os.path.join(directory, path))
                includes.add(path)
                if len(arg) == 2:
                    i += 1

        elif arg == "-isystem":
            if i + 1 < len(args):
                includes.add(args[i + 1])
                i += 1

        elif arg.startswith("-iprefix"):
            iprefix = arg[8:] if len(arg) > 8 else (args[i + 1] if i + 1 < len(args) else "")
            if len(arg) == 8:
                i += 1

        elif arg.startswith("@") and arg.endswith(".txt"):
            process_response_file(arg[1:], includes, defines, iprefix)

        elif arg.startswith("-D"):
            define = arg[2:] if len(arg) > 2 else (args[i + 1] if i + 1 < len(args) else "")
            if define:
                define = define.strip('"').strip("'")
                defines.add(define)
                if len(arg) == 2:
                    i += 1

        i += 1

if sketchbook_path:
    user_libs = os.path.join(sketchbook_path, "libraries")
    if os.path.isdir(user_libs):
        for lib in os.listdir(user_libs):
            lib_path = os.path.join(user_libs, lib)
            if os.path.isdir(lib_path):
                src = os.path.join(lib_path, "src")
                if os.path.isdir(src):
                    includes.add(src)
                else:
                    includes.add(lib_path)

if os.path.isdir(build_dir):
    includes.add(build_dir)
    core_dir = os.path.join(build_dir, "core")
    if os.path.isdir(core_dir):
        includes.add(core_dir)

# R1: every Fiesta firmware module pulls the scDefinitions sources via a
# relative `#include "../common/scDefinitions/..."` from its config.{c,cpp}
# (and the sc_param_handlers_glue.c shim transitively pulls the .c). The
# arduino-cli compile_commands.json does not list that dir as `-I` because
# the includes are file-relative - but VS Code IntelliSense is happier when
# the path is on `includePath` explicitly, so jump-to-definition on
# k_ecu_params[] / sc_param_reply_* / SC_CMD_* lands inside the shared
# headers instead of falling back to the workspaceFolder/** glob.
sc_definitions_dir = os.path.normpath(
    os.path.join(project_dir, "..", "common", "scDefinitions"))
if os.path.isdir(sc_definitions_dir):
    includes.add(sc_definitions_dir)

includes_list = sorted([path for path in includes if os.path.isdir(path)])
defines_list = sorted(defines)
includes_list.append("${workspaceFolder}/**")

patched_db_path = os.path.join(build_dir, "compile_commands_patched.json")

with open(compile_db_path) as f:
    original_commands = json.load(f)

patched_commands = list(original_commands)

for entry in original_commands:
    src_file = entry.get("file", "")
    if src_file.endswith(".ino.cpp"):
        basename = os.path.basename(src_file)
        ino_name = basename.replace(".ino.cpp", ".ino")

        for root, dirs, files in os.walk(project_dir):
            if ".build" in root:
                continue
            if ino_name in files:
                original_ino = os.path.join(root, ino_name)
                patched_entry = dict(entry)
                patched_entry["file"] = original_ino
                patched_commands.append(patched_entry)
                break

with open(patched_db_path, "w") as f:
    json.dump(patched_commands, f, indent=4)
    f.write("\n")

config = {
    "configurations": [
        {
            "name": board_desc,
            "includePath": includes_list,
            "defines": defines_list,
            "compilerPath": compiler_path if compiler_path and os.path.isfile(compiler_path) else "",
            "compileCommands": patched_db_path,
            "cStandard": "c17",
            "cppStandard": "gnu++17",
            "intelliSenseMode": "gcc-arm-none-eabi"
        }
    ],
    "version": 4
}

os.makedirs(os.path.dirname(cpp_props_path), exist_ok=True)
with open(cpp_props_path, "w") as f:
    json.dump(config, f, indent=4)
    f.write("\n")

print(f"  Include paths:    {len(includes_list) - 1}")
print(f"  Defines:          {len(defines_list)}")
print(f"  Compiler:         {compiler_path or 'not found'}")
print(f"  compile_commands: {patched_db_path} ({len(patched_commands)} entries, {len(patched_commands) - len(original_commands)} added for .ino)")
print(f"  Output:           {cpp_props_path}")
PYEOF
}

main() {
    echo ""
    info "Refreshing IntelliSense configuration..."
    echo ""

    export PROJECT_DIR
    export BUILD_DIR

    local cli fqbn sketch sketchbook

    cli="$(fiesta_find_arduino_cli "$SETTINGS_FILE" || true)"
    if [[ -z "$cli" ]]; then
        err "arduino-cli not found"
        exit 1
    fi
    info "arduino-cli: $cli"

    fqbn="$(fiesta_resolve_fqbn "$PROJECT_DIR" || true)"
    if [[ -z "$fqbn" ]]; then
        err "FQBN not set: add 'arduino.fqbn' to .vscode/settings.json or 'board' to .vscode/arduino.json"
        exit 1
    fi
    info "FQBN: $fqbn"

    sketch="$(fiesta_find_sketch "$PROJECT_DIR" || true)"
    if [[ -z "$sketch" ]]; then
        err "No .ino file found in the project"
        exit 1
    fi
    info "Sketch: $sketch"

    sketchbook="$(fiesta_read_json_setting "$SETTINGS_FILE" "arduino.sketchbookPath" "")"

    echo ""
    generate_compile_db "$PROJECT_DIR" "$sketch" "$sketchbook"

    echo ""
    info "Generating c_cpp_properties.json..."
    generate_cpp_properties

    echo ""
    ok "IntelliSense configuration refreshed"
    echo ""
    echo "  Next steps in VS Code:"
    echo "  1. Ctrl+Shift+P -> C/C++: Reset IntelliSense Database"
    echo "  2. Ctrl+Shift+P -> Developer: Reload Window"
    echo ""
}

main "$@"