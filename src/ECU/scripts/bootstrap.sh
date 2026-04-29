#!/usr/bin/env bash
# =============================================================================
# Fiesta dev-environment bootstrap (Debian-like Linux)
#
# Installs system deps (incl. Python 3, cppcheck, GTK-4 dev headers),
# arduino-cli + rp2040 core, syncs required Arduino library (JaszczurHAL),
# runs host tests, compiles firmware for every Fiesta
# module, and finally builds + tests the SerialConfigurator desktop tool:
#   - ECU                (host tests + firmware, -Werror)
#   - Clocks             (host tests + firmware)
#   - OilAndSpeed        (host tests + firmware)
#   - Adjustometer       (host tests + firmware, -Werror)
#   - SerialConfigurator (CMake desktop build + 8 CTest targets, GTK-4 GUI)
# Idempotent - safe to re-run. Also covers the deps used by
# misra/check_misra.sh (cppcheck + Python 3; the MISRA addon ships with the
# cppcheck package).
#
# Env overrides:
#   LIB_DIR         default: $HOME/libraries   (parent of cloned libs)
#   ARDUINO_CLI     default: arduino-cli       (path to binary)
#   SKIP_APT=1      skip apt-get steps
#   SKIP_TESTS=1    skip firmware host test build+run
#   SKIP_BUILD=1    skip firmware compile
#   SKIP_DESKTOP=1  skip SerialConfigurator build + tests
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SRC_ROOT="$(dirname "$PROJECT_DIR")"   # repo_root/src
COMMON_SCRIPT="$SRC_ROOT/common/scripts/fiesta-arduino-common.sh"

# shellcheck source=/dev/null
source "$COMMON_SCRIPT"

# Fallback FQBN for modules that do not ship a .vscode/arduino.json
# (every Fiesta board in use is an RP2040 Pi Pico with the same flash layout).
DEFAULT_FQBN="rp2040:rp2040:rpipico:flash=2097152_0,freq=125,dbgport=Serial,dbglvl=None,usbstack=picosdk"

# Per-module build matrix.
#   TEST_MODULES = modules that ship a CMakeLists.txt at src/<Module>/.
#   FW_MODULES   = "module:werror" - werror=1 enables -Werror for that module
#                  (matches the per-module policy used by the shared Arduino
#                  build/upload/refresh wrappers).
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
    err "Do not run bootstrap.sh as root - it uses sudo only for apt and arduino-cli install."
    err "Re-run as a regular user (you will be prompted for the sudo password)."
    err "To proceed anyway, set ALLOW_ROOT=1."
    exit 1
fi

# -----------------------------------------------------------------------------
# 1. Platform sanity check
# -----------------------------------------------------------------------------
if ! command -v apt-get >/dev/null 2>&1; then
    err "apt-get not found - this script supports Debian-like systems only."
    exit 1
fi

# -----------------------------------------------------------------------------
# 2. System packages
# -----------------------------------------------------------------------------
APT_PKGS=(
    # Common toolchain
    git build-essential cmake python3 curl ca-certificates cppcheck
    # SerialConfigurator desktop build (CMake pkg_check_modules + GTK-4)
    pkg-config libgtk-4-dev
)

install_apt() {
    if [[ "${SKIP_APT:-0}" = "1" ]]; then
        info "SKIP_APT=1 - skipping apt step"
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
        err "python3 not found after apt step - install it manually and re-run."
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
        err "cppcheck not found after apt step - install it manually and re-run."
        exit 1
    fi
    ok "cppcheck present: $(cppcheck --version 2>&1 | head -1)"
    local addon
    if addon=$(find_misra_addon); then
        ok "cppcheck MISRA addon: $addon"
    else
        warn "cppcheck misra.py addon not found - misra/check_misra.sh may fail"
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
        local installed_version
        installed_version=$("$ARDUINO_CLI" core list 2>/dev/null \
            | awk '$1=="rp2040:rp2040" {print $2}')
        ok "rp2040:rp2040 core already installed (version: ${installed_version:-unknown})"
        # The arduino-pico core ships new board IDs across releases (e.g.
        # `waveshare_rp2040_plus` was added in v2.0, RP2350 boards in v4.0).
        # The presence-only check above does NOT catch a stale install that
        # predates a board the operator's settings.json targets - `compile`
        # then dies with a terse "board ... not found". Upgrade
        # unconditionally so already-current installs become a no-op while
        # stale ones are brought up to date.
        info "Upgrading rp2040:rp2040 to the latest version (no-op if current)"
        if ! "$ARDUINO_CLI" core upgrade rp2040:rp2040; then
            warn "core upgrade exited non-zero - continuing with whatever is installed"
        fi
    else
        info "Installing rp2040:rp2040 core (this can take a few minutes)"
        "$ARDUINO_CLI" core install rp2040:rp2040
        ok "rp2040 core installed"
    fi

    # Sanity-check: every Fiesta module currently targets the
    # `waveshare_rp2040_plus` board (see src/*/.vscode/settings.json). If the
    # core is too old and lacks that ID, fail fast with an actionable error
    # instead of letting the operator re-run the whole pipeline only to hit
    # arduino-cli's terse "Invalid FQBN: board ... not found" later on.
    local probe_board="rp2040:rp2040:waveshare_rp2040_plus"
    if ! "$ARDUINO_CLI" board listall rp2040:rp2040 2>/dev/null \
            | awk '{print $NF}' | grep -qx "$probe_board"; then
        err "Required board '$probe_board' missing from rp2040:rp2040."
        err "Likely cause: an arduino-pico install older than v2.0 (this board was"
        err "added in 2.0). Fix:"
        err "  $ARDUINO_CLI core install --force rp2040:rp2040"
        err "Then re-run bootstrap.sh. If the failure persists, verify the board"
        err "manager URL is the earlephilhower one ($BOARD_URL)."
        exit 1
    fi
    ok "rp2040:rp2040 board catalogue includes the project default ($probe_board)"
}

# -----------------------------------------------------------------------------
# 4. GitHub libraries
# -----------------------------------------------------------------------------
resolve_origin_default_branch() {
    local dest="$1"
    local remote_head=""

    git -C "$dest" remote set-head origin --auto >/dev/null 2>&1 || true
    remote_head=$(git -C "$dest" symbolic-ref --quiet --short refs/remotes/origin/HEAD 2>/dev/null || true)

    if [[ -z "$remote_head" ]]; then
        if git -C "$dest" show-ref --verify --quiet refs/remotes/origin/main; then
            remote_head="origin/main"
        elif git -C "$dest" show-ref --verify --quiet refs/remotes/origin/master; then
            remote_head="origin/master"
        fi
    fi

    if [[ -z "$remote_head" ]]; then
        return 1
    fi

    printf '%s\n' "${remote_head#origin/}"
}

sync_lib() {
    local name="$1" url="$2" dest="$LIB_DIR/$1"
    local default_branch=""

    if [[ -e "$dest" ]] && git -C "$dest" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        warn "$name checkout exists at $dest - discarding local changes and resetting to the remote default branch"

        if git -C "$dest" remote get-url origin >/dev/null 2>&1; then
            git -C "$dest" remote set-url origin "$url"
        else
            git -C "$dest" remote add origin "$url"
        fi

        git -C "$dest" fetch --depth 1 --prune origin
        default_branch=$(resolve_origin_default_branch "$dest") || {
            err "Could not determine origin default branch for $name at $dest"
            return 1
        }

        git -C "$dest" reset --hard
        git -C "$dest" clean -fdx
        git -C "$dest" checkout --force -B "$default_branch" "origin/$default_branch"
        git -C "$dest" reset --hard "origin/$default_branch"

        ok "$name reset to origin/$default_branch at $dest"
        return
    fi

    if [[ -e "$dest" ]]; then
        err "$dest exists but is not a git checkout - leaving it alone"
        return 1
    fi

    info "Cloning $name into $dest"
    git clone --depth 1 "$url" "$dest"
}

fetch_libraries() {
    mkdir -p "$LIB_DIR"
    sync_lib JaszczurHAL   https://github.com/jaszczurtd/JaszczurHAL.git
}

# -----------------------------------------------------------------------------
# 5. Host tests (per module)
# -----------------------------------------------------------------------------
run_tests_for() {
    local module="$1"
    local src="$SRC_ROOT/$module"
    local build="$src/build_test"
    if [[ ! -f "$src/CMakeLists.txt" ]]; then
        info "[$module] no CMakeLists.txt - skipping tests"
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
        info "SKIP_TESTS=1 - skipping host tests"
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
        info "[$module] no .vscode/arduino.json - using default FQBN"
    fi

    local werror_flag=""
    [[ "$werror" = "1" ]] && werror_flag=" -Werror"

    local usb_manufacturer usb_product
    usb_manufacturer=$(fiesta_usb_manufacturer)
    usb_product=$(fiesta_usb_product_for "$module")

    info "[$module] compiling firmware (FQBN: $fqbn)"
    "$ARDUINO_CLI" compile \
        --fqbn "$fqbn" \
        --libraries "$LIB_DIR" \
        --build-path "$build" \
        --build-property "compiler.cpp.extra_flags=-I '$src'$werror_flag" \
        --build-property "compiler.c.extra_flags=-I '$src'$werror_flag" \
        --build-property "build.usb_manufacturer=\"$usb_manufacturer\"" \
        --build-property "build.usb_product=\"$usb_product\"" \
        "$src"
    local uf2
    uf2=$(find "$build" -maxdepth 2 -name '*.uf2' -type f | head -1)
    if [[ -n "$uf2" ]]; then
        ok "[$module] firmware: $uf2"
        local manifest
        manifest="$(fiesta_prepare_manifest_for_uf2 "$src" "$uf2" || true)"
        if [[ -z "$manifest" ]]; then
            err "[$module] manifest generation/verification failed for $uf2"
            return 1
        fi
        ok "[$module] manifest: $manifest"
    else
        warn "[$module] compile finished but no .uf2 found in $build"
    fi
}

compile_firmware() {
    if [[ "${SKIP_BUILD:-0}" = "1" ]]; then
        info "SKIP_BUILD=1 - skipping firmware compile"
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
# 7. SerialConfigurator desktop build + tests
# -----------------------------------------------------------------------------
# CMake here resolves JaszczurHAL via ${PROJECT_DIR}/../../../libraries, which
# is the same path that fetch_libraries() populates. The crypto backend
# binding (sc_crypto_jaszczurhal.cpp) propagates HAL_ENABLE_CRYPTO to the
# SerialConfigurator core target, so the firmware-shared hal_crypto.cpp gets
# its real implementation rather than the no-op fallback.
#
# GTK-4 is treated as optional in CMakeLists (pkg_check_modules without
# REQUIRED). When libgtk-4-dev is installed the GUI executable is emitted
# alongside the CLI; otherwise CMake prints a STATUS line and skips the GUI
# while still building core + CLI + all CTest targets.
build_serial_configurator() {
    if [[ "${SKIP_DESKTOP:-0}" = "1" ]]; then
        info "SKIP_DESKTOP=1 - skipping SerialConfigurator build/tests"
        return
    fi

    local sc_dir="$SRC_ROOT/SerialConfigurator"
    if [[ ! -f "$sc_dir/CMakeLists.txt" ]]; then
        warn "SerialConfigurator: CMakeLists.txt not found at $sc_dir - skipping"
        return
    fi

    local sc_build="$sc_dir/build"
    info "[SerialConfigurator] configuring (Release) at $sc_build"
    cmake -S "$sc_dir" -B "$sc_build" -DCMAKE_BUILD_TYPE=Release

    info "[SerialConfigurator] building"
    cmake --build "$sc_build" --parallel

    info "[SerialConfigurator] running ctest"
    ctest --test-dir "$sc_build" --output-on-failure

    if [[ -x "$sc_build/serial-configurator" ]]; then
        ok "[SerialConfigurator] GUI binary: $sc_build/serial-configurator"
    else
        warn "[SerialConfigurator] GUI binary not built (libgtk-4-dev missing?). CLI + tests still OK."
    fi
    if [[ -x "$sc_build/serial-configurator-cli" ]]; then
        ok "[SerialConfigurator] CLI binary: $sc_build/serial-configurator-cli"
    fi
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
build_serial_configurator

ok "Bootstrap finished"
