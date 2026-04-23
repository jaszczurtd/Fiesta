#!/usr/bin/env bash
# =============================================================================
# Fiesta dev-environment bootstrap (Debian-like Linux)
#
# Installs system deps (incl. Python 3 and cppcheck), arduino-cli + rp2040
# core, clones required Arduino libraries (JaszczurHAL, canDefinitions), runs
# host tests, then compiles firmware for every Fiesta module:
#   - ECU           (host tests + firmware, -Werror)
#   - Clocks        (host tests + firmware)
#   - OilAndSpeed   (host tests + firmware)
#   - Adjustometer  (host tests + firmware, -Werror)
# Idempotent — safe to re-run. Also covers the deps used by
# misra/check_misra.sh (cppcheck + Python 3; the MISRA addon ships with the
# cppcheck package).
#
# Env overrides:
#   LIB_DIR        default: $HOME/libraries   (parent of cloned libs)
#   ARDUINO_CLI    default: arduino-cli       (path to binary)
#   SKIP_APT=1     skip apt-get steps
#   SKIP_TESTS=1   skip host test build+run
#   SKIP_BUILD=1   skip firmware compile
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SRC_ROOT="$(dirname "$PROJECT_DIR")"   # repo_root/src

# Fallback FQBN for modules that do not ship a .vscode/arduino.json
# (every Fiesta board in use is an RP2040 Pi Pico with the same flash layout).
DEFAULT_FQBN="rp2040:rp2040:rpipico:flash=2097152_0,freq=125,dbgport=Serial,dbglvl=None,usbstack=picosdk"

# Per-module build matrix.
#   TEST_MODULES = modules that ship a CMakeLists.txt at src/<Module>/.
#   FW_MODULES   = "module:werror" — werror=1 enables -Werror for that module
#                  (matches what each module's own scripts/upload-uf2.sh uses).
TEST_MODULES=(ECU Clocks OilAndSpeed Adjustometer)
FW_MODULES=(
    "ECU:1"
    "Clocks:0"
    "OilAndSpeed:0"
    "Adjustometer:1"
)

# src/ECU/CMakeLists.txt resolves libraries at ${PROJECT_DIR}/../../../libraries
# (i.e. parent of the repo root). The default must match that path or the host
# test build will fail to find JaszczurHAL sources.
DEFAULT_LIB_DIR="$(cd "$PROJECT_DIR/../../.." && pwd)/libraries"
LIB_DIR="${LIB_DIR:-$DEFAULT_LIB_DIR}"
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
BOARD_URL="https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }

if [[ $EUID -eq 0 ]]; then SUDO=""; else SUDO="sudo"; fi

# Running as root puts arduino-cli config + rp2040 core under /root/.arduino15/
# and the rest of the workflow uses the wrong $HOME, which breaks later
# non-root use of arduino-cli compile. Require an explicit opt-in.
if [[ $EUID -eq 0 && "${ALLOW_ROOT:-0}" != "1" ]]; then
    err "Do not run bootstrap.sh as root — it uses sudo only for apt and arduino-cli install."
    err "Re-run as a regular user (you will be prompted for the sudo password)."
    err "To proceed anyway, set ALLOW_ROOT=1."
    exit 1
fi

# -----------------------------------------------------------------------------
# 1. Platform sanity check
# -----------------------------------------------------------------------------
if ! command -v apt-get >/dev/null 2>&1; then
    err "apt-get not found — this script supports Debian-like systems only."
    exit 1
fi

# -----------------------------------------------------------------------------
# 2. System packages
# -----------------------------------------------------------------------------
APT_PKGS=(git build-essential cmake python3 curl ca-certificates cppcheck)

install_apt() {
    if [[ "${SKIP_APT:-0}" = "1" ]]; then
        info "SKIP_APT=1 — skipping apt step"
        return
    fi
    local missing=()
    for pkg in "${APT_PKGS[@]}"; do
        if ! dpkg -s "$pkg" >/dev/null 2>&1; then
            missing+=("$pkg")
        fi
    done
    if [[ ${#missing[@]} -eq 0 ]]; then
        ok "System packages already present: ${APT_PKGS[*]}"
        return
    fi
    info "Installing apt packages: ${missing[*]}"
    $SUDO apt-get update -y
    $SUDO apt-get install -y --no-install-recommends "${missing[@]}"
    ok "apt packages installed"
}

# -----------------------------------------------------------------------------
# 2b. Python 3 (used by helper scripts for .vscode/*.json parsing)
# -----------------------------------------------------------------------------
check_python() {
    if ! command -v python3 >/dev/null 2>&1; then
        err "python3 not found after apt step — install it manually and re-run."
        exit 1
    fi
    ok "python3 present: $(python3 --version 2>&1)"
}

# -----------------------------------------------------------------------------
# 2c. cppcheck (static analysis + MISRA screening via bundled addon)
# -----------------------------------------------------------------------------
find_misra_addon() {
    # Fast path: dpkg knows exactly which files cppcheck ships.
    if command -v dpkg >/dev/null 2>&1; then
        if dpkg -L cppcheck 2>/dev/null | grep -qE '/misra\.py$'; then
            dpkg -L cppcheck 2>/dev/null | grep -E '/misra\.py$' | head -1
            return 0
        fi
    fi
    # Direct probe of common locations (Debian/Ubuntu/Fedora/source).
    local candidates=(
        /usr/share/cppcheck/addons/misra.py
        /usr/local/share/cppcheck/addons/misra.py
        /usr/lib/cppcheck/addons/misra.py
        /usr/share/cppcheck-addons/misra.py
    )
    for p in "${candidates[@]}"; do
        [[ -f "$p" ]] && { echo "$p"; return 0; }
    done
    # Broad fallback.
    local found
    found=$(find /usr/share /usr/lib /usr/local/share /usr/local/lib 2>/dev/null \
        -name misra.py -path '*cppcheck*' -print -quit)
    if [[ -n "$found" ]]; then
        echo "$found"
        return 0
    fi
    return 1
}

check_cppcheck() {
    if ! command -v cppcheck >/dev/null 2>&1; then
        err "cppcheck not found after apt step — install it manually and re-run."
        exit 1
    fi
    ok "cppcheck present: $(cppcheck --version 2>&1 | head -1)"
    local addon
    if addon=$(find_misra_addon); then
        ok "cppcheck MISRA addon: $addon"
    else
        warn "cppcheck misra.py addon not found — misra/check_misra.sh may fail"
        warn "On Debian/Ubuntu the addon ships with the 'cppcheck' package; check 'dpkg -L cppcheck | grep misra'"
    fi
}

# -----------------------------------------------------------------------------
# 3. arduino-cli
# -----------------------------------------------------------------------------
install_arduino_cli() {
    if command -v "$ARDUINO_CLI" >/dev/null 2>&1; then
        ok "arduino-cli present: $("$ARDUINO_CLI" version | head -1)"
        return
    fi
    info "Installing arduino-cli to /usr/local/bin"
    local tmp
    tmp=$(mktemp -d)
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
        | BINDIR="$tmp" sh
    $SUDO install -m 0755 "$tmp/arduino-cli" /usr/local/bin/arduino-cli
    rm -rf "$tmp"
    ARDUINO_CLI="arduino-cli"
    ok "arduino-cli installed: $("$ARDUINO_CLI" version | head -1)"
}

setup_arduino_core() {
    info "Configuring arduino-cli board manager (rp2040)"
    if ! "$ARDUINO_CLI" config dump 2>/dev/null | grep -q "$BOARD_URL"; then
        "$ARDUINO_CLI" config init --overwrite >/dev/null 2>&1 || true
        "$ARDUINO_CLI" config add board_manager.additional_urls "$BOARD_URL" \
            2>/dev/null || "$ARDUINO_CLI" config set board_manager.additional_urls "$BOARD_URL"
    fi
    "$ARDUINO_CLI" core update-index >/dev/null
    if "$ARDUINO_CLI" core list 2>/dev/null | awk 'NR>1 {print $1}' | grep -qx "rp2040:rp2040"; then
        ok "rp2040:rp2040 core already installed"
    else
        info "Installing rp2040:rp2040 core (this can take a few minutes)"
        "$ARDUINO_CLI" core install rp2040:rp2040
        ok "rp2040 core installed"
    fi
}

# -----------------------------------------------------------------------------
# 4. GitHub libraries
# -----------------------------------------------------------------------------
clone_lib() {
    local name="$1" url="$2" dest="$LIB_DIR/$1"
    if [[ -d "$dest/.git" ]]; then
        ok "$name already cloned at $dest"
        return
    fi
    if [[ -e "$dest" ]]; then
        err "$dest exists but is not a git checkout — leaving it alone"
        return 1
    fi
    info "Cloning $name into $dest"
    git clone --depth 1 "$url" "$dest"
}

fetch_libraries() {
    mkdir -p "$LIB_DIR"
    clone_lib JaszczurHAL   https://github.com/jaszczurtd/JaszczurHAL.git
    clone_lib canDefinitions https://github.com/jaszczurtd/canDefinitions.git
}

# -----------------------------------------------------------------------------
# 5. Host tests (per module)
# -----------------------------------------------------------------------------
run_tests_for() {
    local module="$1"
    local src="$SRC_ROOT/$module"
    local build="$src/build_test"
    if [[ ! -f "$src/CMakeLists.txt" ]]; then
        info "[$module] no CMakeLists.txt — skipping tests"
        return 0
    fi
    info "[$module] configuring host tests"
    cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release
    info "[$module] building host tests"
    cmake --build "$build" --parallel
    info "[$module] running ctest"
    ctest --test-dir "$build" --output-on-failure
    ok "[$module] host tests passed"
}

run_tests() {
    if [[ "${SKIP_TESTS:-0}" = "1" ]]; then
        info "SKIP_TESTS=1 — skipping host tests"
        return
    fi
    local module
    for module in "${TEST_MODULES[@]}"; do
        run_tests_for "$module"
    done
}

# -----------------------------------------------------------------------------
# 6. Firmware compile (per module)
# -----------------------------------------------------------------------------
read_arduino_json_key() {
    local file="$1" key="$2"
    [[ -f "$file" ]] || { echo ""; return; }
    python3 -c "
import json, sys
try:
    with open('$file') as f:
        s = json.load(f)
except Exception:
    sys.exit(0)
print(s.get('$key', ''))
"
}

compile_firmware_for() {
    local module="$1" werror="$2"
    local src="$SRC_ROOT/$module"
    local build="$src/.build"
    local ajson="$src/.vscode/arduino.json"

    local board config fqbn=""
    board=$(read_arduino_json_key "$ajson" board)
    config=$(read_arduino_json_key "$ajson" configuration)
    if [[ -n "$board" ]]; then
        fqbn="$board"
        [[ -n "$config" ]] && fqbn="${board}:${config}"
    fi
    if [[ -z "$fqbn" ]]; then
        fqbn="$DEFAULT_FQBN"
        info "[$module] no .vscode/arduino.json — using default FQBN"
    fi

    local werror_flag=""
    [[ "$werror" = "1" ]] && werror_flag=" -Werror"

    info "[$module] compiling firmware (FQBN: $fqbn)"
    "$ARDUINO_CLI" compile \
        --fqbn "$fqbn" \
        --libraries "$LIB_DIR" \
        --build-path "$build" \
        --build-property "compiler.cpp.extra_flags=-I '$src'$werror_flag" \
        --build-property "compiler.c.extra_flags=-I '$src'$werror_flag" \
        "$src"
    local uf2
    uf2=$(find "$build" -maxdepth 2 -name '*.uf2' -type f | head -1)
    if [[ -n "$uf2" ]]; then
        ok "[$module] firmware: $uf2"
    else
        warn "[$module] compile finished but no .uf2 found in $build"
    fi
}

compile_firmware() {
    if [[ "${SKIP_BUILD:-0}" = "1" ]]; then
        info "SKIP_BUILD=1 — skipping firmware compile"
        return
    fi
    local entry module werror
    for entry in "${FW_MODULES[@]}"; do
        module="${entry%%:*}"
        werror="${entry##*:}"
        compile_firmware_for "$module" "$werror"
    done
}

# -----------------------------------------------------------------------------
# main
# -----------------------------------------------------------------------------
info "Fiesta bootstrap starting"
info "  SRC_ROOT: $SRC_ROOT"
info "  LIB_DIR:  $LIB_DIR"

install_apt
check_python
check_cppcheck
install_arduino_cli
setup_arduino_core
fetch_libraries
run_tests
compile_firmware

ok "Bootstrap finished"
