#!/usr/bin/env bash
# =============================================================================
# Odświeżenie konfiguracji IntelliSense
#
# Strategia:
# 1. Kompiluj projekt z --libraries żeby arduino-cli widział wszystko
# 2. Wygeneruj compile_commands.json (--only-compilation-database)
# 3. Parsuj WYŁĄCZNIE compile_commands.json — wyciągnij -I, -D, kompilator
# 4. Wygeneruj c_cpp_properties.json BEZ pola compileCommands
#    (cpptools nie radzi sobie z .ino → .ino.cpp remapowaniem)
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
    err "arduino-cli nie znalezione"
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
        err "Nie znaleziono pliku .ino w projekcie"
        exit 1
    fi
    echo "$sketch"
}

# ---------------------------------------------------------------------------
# Kompilacja + generowanie compile_commands.json
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

    info "Kompilacja projektu..."
    if ! "$cli" compile \
        --fqbn "$fqbn" \
        --build-path "$BUILD_DIR" \
        "${lib_args[@]}" \
        --warnings all \
        "$(dirname "$sketch")" 2>&1; then
        warn "Kompilacja nie powiodła się"
        warn "IntelliSense może być niekompletne, ale próbuję dalej..."
    fi

    info "Generowanie compile_commands.json..."
    if "$cli" compile \
        --fqbn "$fqbn" \
        --build-path "$BUILD_DIR" \
        "${lib_args[@]}" \
        --only-compilation-database \
        "$(dirname "$sketch")" 2>&1; then
        ok "compile_commands.json wygenerowany"
    else
        err "Nie udało się wygenerować compile_commands.json"
        exit 1
    fi

    if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
        err "Plik compile_commands.json nie istnieje po generacji"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Parsowanie compile_commands.json i generowanie c_cpp_properties.json
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

# Wczytaj settings
with open(settings_path) as f:
    settings = json.load(f)

board_desc = settings.get("arduino.boardDescription", "Arduino-Pico")
sketchbook_path = settings.get("arduino.sketchbookPath", "")

# Parsuj compile_commands.json
with open(compile_db_path) as f:
    commands = json.load(f)

includes = set()
defines = set()
compiler_path = ""
iprefix = ""

def process_response_file(resp_file, includes, defines, iprefix):
    """Parsuj response file — obsługuje -I, -D, -iwithprefixbefore."""
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
    # compile_commands.json ma albo "arguments" (lista) albo "command" (string)
    args = entry.get("arguments", [])
    if not args:
        cmd = entry.get("command", "")
        # Podziel z uwzględnieniem cudzysłowów
        import shlex
        try:
            args = shlex.split(cmd)
        except ValueError:
            args = cmd.split()

    if not args:
        continue

    # Pierwszy argument to kompilator
    if not compiler_path and args[0].endswith(("g++", "gcc", "arm-none-eabi-g++", "arm-none-eabi-gcc")):
        compiler_path = args[0]

    i = 0
    while i < len(args):
        arg = args[i]

        # -I/ścieżka lub -I ścieżka
        if arg.startswith("-I"):
            path = arg[2:] if len(arg) > 2 else (args[i+1] if i+1 < len(args) else "")
            if path:
                # Rozwiąż ścieżkę względną
                if not os.path.isabs(path):
                    directory = entry.get("directory", "")
                    if directory:
                        path = os.path.normpath(os.path.join(directory, path))
                includes.add(path)
                if len(arg) == 2:
                    i += 1

        # -isystem ścieżka
        elif arg == "-isystem":
            if i+1 < len(args):
                includes.add(args[i+1])
                i += 1

        # -iprefix — zapamiętaj prefix do użycia z -iwithprefixbefore
        elif arg.startswith("-iprefix"):
            iprefix = arg[8:] if len(arg) > 8 else (args[i+1] if i+1 < len(args) else "")
            if len(arg) == 8:
                i += 1

        # @plik — response file (arduino-pico używa platform_inc.txt z -iwithprefixbefore)
        elif arg.startswith("@") and arg.endswith(".txt"):
            process_response_file(arg[1:], includes, defines, iprefix)

        # -D definy
        elif arg.startswith("-D"):
            d = arg[2:] if len(arg) > 2 else (args[i+1] if i+1 < len(args) else "")
            if d:
                # Oczyść z cudzysłowów
                d = d.strip('"').strip("'")
                defines.add(d)
                if len(arg) == 2:
                    i += 1

        i += 1

# Dodaj ścieżki ze sketchbooka
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

# Dodaj katalog build (wygenerowane headery)
if os.path.isdir(build_dir):
    includes.add(build_dir)
    core_dir = os.path.join(build_dir, "core")
    if os.path.isdir(core_dir):
        includes.add(core_dir)

# Filtruj — zachowaj tylko istniejące katalogi + wzorce ${...}
includes_list = sorted([p for p in includes if os.path.isdir(p)])
defines_list = sorted(defines)

# Dodaj workspace do include
includes_list.append("${workspaceFolder}/**")

# Określ IntelliSense mode
intellisense_mode = "gcc-arm-none-eabi"

# Generuj c_cpp_properties.json
#
# Strategia: compileCommands dla precyzyjnych flag per-plik (.c, .cpp),
# a includePath + defines jako fallback dla plików które nie mają
# wpisu w compile_commands.json (np. headery .h, .hpp).
#
# Problem .ino: arduino-cli w compile_commands.json rejestruje
# sketch.ino jako sketch.ino.cpp w build dir. Cpptools nie potrafi
# tego zmapować. Rozwiązanie: patchujemy compile_commands.json —
# dodajemy zduplikowane wpisy z oryginalną ścieżką .ino.

# --- Patch compile_commands.json ---
patched_db_path = os.path.join(build_dir, "compile_commands_patched.json")

with open(compile_db_path) as f:
    original_commands = json.load(f)

patched_commands = list(original_commands)

for entry in original_commands:
    src_file = entry.get("file", "")
    # Szukaj plików .ino.cpp w build dir — to są skonwertowane .ino
    if src_file.endswith(".ino.cpp"):
        # Znajdź oryginalny .ino w projekcie
        basename = os.path.basename(src_file)  # np. sketch.ino.cpp
        ino_name = basename.replace(".ino.cpp", ".ino")  # → sketch.ino

        # Szukaj tego .ino w katalogu projektu
        for root, dirs, files in os.walk(project_dir):
            # Pomiń .build
            if ".build" in root:
                continue
            if ino_name in files:
                original_ino = os.path.join(root, ino_name)
                # Dodaj zduplikowany wpis z oryginalną ścieżką
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

print(f"  Include paths:    {len(includes_list) - 1}")  # -1 bo workspaceFolder
print(f"  Defines:          {len(defines_list)}")
print(f"  Compiler:         {compiler_path or 'nie znaleziony'}")
print(f"  compile_commands: {patched_db_path} ({len(patched_commands)} entries, {len(patched_commands) - len(original_commands)} added for .ino)")
print(f"  Output:           {cpp_props_path}")
PYEOF
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo ""
    info "Odświeżanie konfiguracji IntelliSense..."
    echo ""

    export PROJECT_DIR
    export BUILD_DIR

    local cli fqbn sketch sketchbook

    cli=$(find_arduino_cli)
    info "arduino-cli: $cli"

    fqbn=$(read_setting "arduino.fqbn")
    if [[ -z "$fqbn" ]]; then
        err "Brak FQBN w settings.json. Uruchom: ./scripts/select-board.sh"
        exit 1
    fi
    info "FQBN: $fqbn"

    sketch=$(find_sketch)
    info "Sketch: $sketch"

    sketchbook=$(read_setting "arduino.sketchbookPath")

    echo ""
    generate_compile_db "$cli" "$fqbn" "$sketch" "$sketchbook"

    echo ""
    info "Generowanie c_cpp_properties.json..."
    generate_cpp_properties

    echo ""
    ok "Konfiguracja IntelliSense odświeżona!"
    echo ""
    echo "  Następne kroki w VS Code:"
    echo "  1. Ctrl+Shift+P → C/C++: Reset IntelliSense Database"
    echo "  2. Ctrl+Shift+P → Developer: Reload Window"
    echo ""
}

main "$@"
