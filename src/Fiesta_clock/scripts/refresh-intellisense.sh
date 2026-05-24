#!/usr/bin/env bash
# =============================================================================
# Refresh IntelliSense configuration
#
# Strategy:
# 1. Compile with --libraries so arduino-cli resolves all dependencies
# 2. Generate compile_commands.json (--only-compilation-database)
# 3. Parse ONLY compile_commands.json to extract -I, -D, and compiler path
# 4. Generate c_cpp_properties.json with robust include/define fallback
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
CPP_PROPS_FILE="$PROJECT_DIR/.vscode/c_cpp_properties.json"
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

# ---------------------------------------------------------------------------
find_arduino_cli() {
    if [[ -f "$SETTINGS_FILE" ]]; then
        local cli_path
        cli_path=$(python3 -c "
import json
with open('$SETTINGS_FILE') as f:
    s = json.load(f)
print(s.get('arduino.cliPath', ''))
" 2>/dev/null)
        if [[ -n "$cli_path" ]] && command -v "$cli_path" &>/dev/null; then
            command -v "$cli_path"
            return
        fi
    fi
    if command -v arduino-cli &>/dev/null; then
        command -v arduino-cli
        return
    fi
    if [[ -x "$HOME/.local/bin/arduino-cli" ]]; then
        echo "$HOME/.local/bin/arduino-cli"
        return
    fi
    err "arduino-cli not found"
    exit 1
}

# ---------------------------------------------------------------------------
read_setting() {
    local key="$1"
    local default="${2:-}"
    python3 -c "
import json
with open('$SETTINGS_FILE') as f:
    s = json.load(f)
print(s.get('$key', '$default'))
" 2>/dev/null
}

# ---------------------------------------------------------------------------
find_sketch() {
    local sketch
    sketch=$(find "$PROJECT_DIR" -maxdepth 2 -name "*.ino" -not -path "*/.build/*" | head -1)
    if [[ -z "$sketch" ]]; then
        err "No .ino file found in the project"
        exit 1
    fi
    echo "$sketch"
}

# ---------------------------------------------------------------------------
# Compile + generate compile_commands.json
# ---------------------------------------------------------------------------
generate_compile_db() {
    local cli="$1"
    local fqbn="$2"
    local sketch="$3"
    local sketchbook="$4"

    local lib_args=()
    if [[ -n "$sketchbook" && -d "$sketchbook/libraries" ]]; then
        lib_args=("--libraries" "$sketchbook/libraries")
        info "  Libraries: $sketchbook/libraries"
    fi

    mkdir -p "$BUILD_DIR"

    info "Compiling project..."
    if ! "$cli" compile \
        --fqbn "$fqbn" \
        --build-path "$BUILD_DIR" \
        "${lib_args[@]}" \
        --build-property "compiler.cpp.extra_flags=-I '$(dirname "$sketch")'" \
        --build-property "compiler.c.extra_flags=-I '$(dirname "$sketch")'" \
        --warnings all \
        "$(dirname "$sketch")" 2>&1; then
        warn "Compilation failed"
        warn "IntelliSense may be incomplete, continuing anyway..."
    fi

    info "Generating compile_commands.json..."
    if "$cli" compile \
        --fqbn "$fqbn" \
        --build-path "$BUILD_DIR" \
        "${lib_args[@]}" \
        --build-property "compiler.cpp.extra_flags=-I '$(dirname "$sketch")'" \
        --build-property "compiler.c.extra_flags=-I '$(dirname "$sketch")'" \
        --only-compilation-database \
        "$(dirname "$sketch")" 2>&1; then
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

# ---------------------------------------------------------------------------
# Parse compile_commands.json and generate c_cpp_properties.json
# ---------------------------------------------------------------------------
generate_cpp_properties() {
    python3 << 'PYEOF'
import json
import os
import sys

project_dir = os.environ["PROJECT_DIR"]
build_dir = os.environ["BUILD_DIR"]
settings_path = os.path.join(project_dir, ".vscode", "settings.json")
cpp_props_path = os.path.join(project_dir, ".vscode", "c_cpp_properties.json")
compile_db_path = os.path.join(build_dir, "compile_commands.json")

# Read settings
with open(settings_path) as f:
    settings = json.load(f)

board_desc = settings.get("arduino.boardDescription", "Arduino-Pico")
sketchbook_path = settings.get("arduino.sketchbookPath", "")

# Parse compile_commands.json
with open(compile_db_path) as f:
    commands = json.load(f)

includes = set()
defines = set()
compiler_path = ""
iprefix = ""

def process_response_file(resp_file, includes, defines, iprefix):
    """Parse response file handling -I, -D, and -iwithprefixbefore."""
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
                d = line[2:]
                if d:
                    defines.add(d)

for entry in commands:
    # compile_commands.json can have either "arguments" (list) or "command" (string)
    args = entry.get("arguments", [])
    if not args:
        cmd = entry.get("command", "")
        # Split while preserving quoted values
        import shlex
        try:
            args = shlex.split(cmd)
        except ValueError:
            args = cmd.split()

    if not args:
        continue

    # First argument should be the compiler executable
    if not compiler_path and args[0].endswith(("g++", "gcc", "arm-none-eabi-g++", "arm-none-eabi-gcc")):
        compiler_path = args[0]

    i = 0
    while i < len(args):
        arg = args[i]

        # -I/path or -I path
        if arg.startswith("-I"):
            path = arg[2:] if len(arg) > 2 else (args[i+1] if i+1 < len(args) else "")
            if path:
            # Resolve relative path from command directory
                if not os.path.isabs(path):
                    directory = entry.get("directory", "")
                    if directory:
                        path = os.path.normpath(os.path.join(directory, path))
                includes.add(path)
                if len(arg) == 2:
                    i += 1

        # -isystem path
        elif arg == "-isystem":
            if i+1 < len(args):
                includes.add(args[i+1])
                i += 1

        # -iprefix used later by -iwithprefixbefore
        elif arg.startswith("-iprefix"):
            iprefix = arg[8:] if len(arg) > 8 else (args[i+1] if i+1 < len(args) else "")
            if len(arg) == 8:
                i += 1

        # @file response (arduino-pico uses platform_inc.txt with -iwithprefixbefore)
        elif arg.startswith("@") and arg.endswith(".txt"):
            process_response_file(arg[1:], includes, defines, iprefix)

        # -D defines
        elif arg.startswith("-D"):
            d = arg[2:] if len(arg) > 2 else (args[i+1] if i+1 < len(args) else "")
            if d:
                # Strip wrapping quotes
                d = d.strip('"').strip("'")
                defines.add(d)
                if len(arg) == 2:
                    i += 1

        i += 1

# Add sketchbook library include paths
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

# Add build directory (generated headers)
if os.path.isdir(build_dir):
    includes.add(build_dir)
    core_dir = os.path.join(build_dir, "core")
    if os.path.isdir(core_dir):
        includes.add(core_dir)

# Keep only existing directories (plus workspace wildcard added later)
includes_list = sorted([p for p in includes if os.path.isdir(p)])
defines_list = sorted(defines)

# Add workspace fallback include
includes_list.append("${workspaceFolder}/**")

# IntelliSense mode
intellisense_mode = "gcc-arm-none-eabi"

# Generuj c_cpp_properties.json
#
# Strategy: compileCommands for per-file precision (.c/.cpp),
# with includePath + defines fallback for files not present in
# compile_commands.json (e.g. .h/.hpp headers).
#
# .ino issue: arduino-cli records sketch.ino as sketch.ino.cpp in build dir.
# cpptools cannot map that reliably, so we patch compile_commands by adding
# duplicated entries that point to original .ino paths.

# --- Patch compile_commands.json ---
patched_db_path = os.path.join(build_dir, "compile_commands_patched.json")

with open(compile_db_path) as f:
    original_commands = json.load(f)

patched_commands = list(original_commands)

for entry in original_commands:
    src_file = entry.get("file", "")
    # Find .ino.cpp files in build dir (converted Arduino sketch units)
    if src_file.endswith(".ino.cpp"):
        # Find original .ino in project
        basename = os.path.basename(src_file)  # np. sketch.ino.cpp
        ino_name = basename.replace(".ino.cpp", ".ino")  # → sketch.ino

        # Search for this .ino in project tree
        for root, dirs, files in os.walk(project_dir):
            # Skip build artifacts
            if ".build" in root:
                continue
            if ino_name in files:
                original_ino = os.path.join(root, ino_name)
                # Add duplicated entry with original .ino path
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
            "intelliSenseMode": intellisense_mode
        }
    ],
    "version": 4
}

os.makedirs(os.path.dirname(cpp_props_path), exist_ok=True)
with open(cpp_props_path, "w") as f:
    json.dump(config, f, indent=4)
    f.write("\n")

print(f"  Include paths:    {len(includes_list) - 1}")  # -1 excludes workspace wildcard
print(f"  Defines:          {len(defines_list)}")
print(f"  Compiler:         {compiler_path or 'not found'}")
print(f"  compile_commands: {patched_db_path} ({len(patched_commands)} entries, {len(patched_commands) - len(original_commands)} added for .ino)")
print(f"  Output:           {cpp_props_path}")
PYEOF
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo ""
    info "Refreshing IntelliSense configuration..."
    echo ""

    export PROJECT_DIR
    export BUILD_DIR

    local cli fqbn sketch sketchbook

    cli=$(find_arduino_cli)
    info "arduino-cli: $cli"

    fqbn=$(read_setting "arduino.fqbn")
    if [[ -z "$fqbn" ]]; then
        err "Missing FQBN in settings.json. Run: ./scripts/select-board.sh"
        exit 1
    fi
    info "FQBN: $fqbn"

    sketch=$(find_sketch)
    info "Sketch: $sketch"

    sketchbook=$(read_setting "arduino.sketchbookPath")

    echo ""
    generate_compile_db "$cli" "$fqbn" "$sketch" "$sketchbook"

    echo ""
    info "Generating c_cpp_properties.json..."
    generate_cpp_properties

    echo ""
    ok "IntelliSense configuration refreshed"
    echo ""
    echo "  Next steps in VS Code:"
    echo "  1. Ctrl+Shift+P → C/C++: Reset IntelliSense Database"
    echo "  2. Ctrl+Shift+P → Developer: Reload Window"
    echo ""
}

main "$@"
