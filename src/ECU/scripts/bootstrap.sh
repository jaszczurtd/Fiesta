#!/usr/bin/env bash
# =============================================================================
# Fiesta dev-environment bootstrap (Debian-like Linux)
#
# Installs system deps (incl. Python 3, cppcheck, GTK-4 dev headers),
# arduino-cli + rp2040 core, syncs required Arduino library (JaszczurHAL),
# runs host tests, compiles firmware for every Fiesta
# module, and finally builds + tests + packages the SerialConfigurator
# desktop tool:
#   - ECU                (host tests + firmware, -Werror)
#   - Clocks             (host tests + firmware)
#   - OilAndSpeed        (host tests + firmware)
#   - Fiesta_clock       (firmware)
#   - Adjustometer       (host tests + firmware, -Werror)
#   - SerialConfigurator (CMake desktop build + tests + .deb package)
# Idempotent - safe to re-run. Also covers the deps used by
# misra/check_misra.sh (cppcheck + Python 3; the MISRA addon ships with the
# cppcheck package).
#
# Env overrides:
#   LIB_DIR         default: $HOME/libraries   (parent of cloned libs)
#   ARDUINO_CLI     default: arduino-cli       (path to binary)
#   SKIP_APT=1      skip apt-get steps
#   SKIP_TESTS=1    skip host QA run (`runalltests.sh`)
#   SKIP_BUILD=1    skip firmware compile
#   SKIP_DESKTOP=1          skip SerialConfigurator build + tests + package
#   SKIP_DESKTOP_PACKAGE=1  skip SerialConfigurator .deb packaging only
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SRC_ROOT="$(dirname "$PROJECT_DIR")"   # repo_root/src
COMMON_SCRIPT="$SRC_ROOT/common/scripts/fiesta-arduino-common.sh"

# shellcheck source=/dev/null
source "$COMMON_SCRIPT"

# Fallback FQBN for modules that do not ship a VS Code project manifest.
# This is only a last-resort bootstrap default; migrated firmware modules keep
# their board contract in .vscode/jaszczurhal.project.json.
DEFAULT_FQBN="rp2040:rp2040:rpipico:flash=2097152_0,freq=125,dbgport=Serial,dbglvl=None,usbstack=picosdk"

# Per-module build matrix.
#   FW_MODULES   = "module:werror" - werror=1 enables -Werror for that module
#                  (matches the per-module policy used by the shared Arduino
#                  build/upload/refresh wrappers).
FW_MODULES=(
    "ECU:1"
    "Clocks:0"
    "OilAndSpeed:0"
    "Fiesta_clock:0"
    "Adjustometer:1"
)

# src/ECU/CMakeLists.txt resolves libraries at ${PROJECT_DIR}/../../../libraries
# (i.e. parent of the repo root). The default must match that path or the host
# test build will fail to find JaszczurHAL sources.
DEFAULT_LIB_DIR="$(cd "$PROJECT_DIR/../../.." && pwd)/libraries"
LIB_DIR="${LIB_DIR:-$DEFAULT_LIB_DIR}"
ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
BOARD_URL="https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"
RP2040_CORE_VERSION="5.4.0"

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
    git build-essential cmake python3 curl ca-certificates perl
    # SerialConfigurator desktop build + Debian package
    pkg-config libgtk-4-dev dpkg-dev
    # SerialConfigurator Map tab (Phase 8.7). Optional at build time -
    # CMake degrades to a placeholder when shumate-1.0 is missing - but
    # bootstrap.sh sets up the full feature surface so first-time devs
    # get a working live GPS map without extra steps.
    libshumate-dev
    # quality checking
    clang-format clang-tidy valgrind cppcheck
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
    # Fast path: dpkg knows exactly which files cppcheck ships. Capture once and
    # test the result; `grep -q` inside a pipe can hit the pipefail+SIGPIPE false
    # negative, and it re-ran the same query twice.
    if command -v dpkg >/dev/null 2>&1; then
        local dpkg_hit
        dpkg_hit=$(dpkg -L cppcheck 2>/dev/null | grep -m1 -E '/misra\.py$') || true
        if [[ -n "$dpkg_hit" ]]; then
            echo "$dpkg_hit"
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
    # Capture then match on a here-string: a `grep -q` in the pipe can hit the
    # pipefail+SIGPIPE false negative and needlessly overwrite the CLI config.
    local config_dump=""
    config_dump=$("$ARDUINO_CLI" config dump 2>/dev/null) || true
    if ! grep -qF "$BOARD_URL" <<<"$config_dump"; then
        "$ARDUINO_CLI" config init --overwrite >/dev/null 2>&1 || true
        "$ARDUINO_CLI" config add board_manager.additional_urls "$BOARD_URL" \
            2>/dev/null || "$ARDUINO_CLI" config set board_manager.additional_urls "$BOARD_URL"
    fi
    "$ARDUINO_CLI" core update-index >/dev/null
    local installed_version
    installed_version=$("$ARDUINO_CLI" core list 2>/dev/null \
        | awk '$1=="rp2040:rp2040" {print $2}')
    if [[ "$installed_version" == "$RP2040_CORE_VERSION" ]]; then
        ok "rp2040:rp2040 core already installed at pinned version $RP2040_CORE_VERSION"
    else
        if [[ -n "$installed_version" ]]; then
            info "Replacing rp2040:rp2040 $installed_version with pinned version $RP2040_CORE_VERSION"
        else
            info "Installing pinned rp2040:rp2040 core $RP2040_CORE_VERSION (this can take a few minutes)"
        fi
        "$ARDUINO_CLI" core install "rp2040:rp2040@$RP2040_CORE_VERSION"
        ok "rp2040 core $RP2040_CORE_VERSION installed"
    fi

    # Sanity-check: every Fiesta module currently targets the
    # `waveshare_rp2040_plus` board (see src/*/.vscode/settings.json). If the
    # core is too old and lacks that ID, fail fast with an actionable error
    # instead of letting the operator re-run the whole pipeline only to hit
    # arduino-cli's terse "Invalid FQBN: board ... not found" later on.
    # Capture the catalogue, then test membership. Piping `board listall` into
    # `grep -q` lets grep close the pipe on first match; under `set -o pipefail`
    # the upstream SIGPIPE (141) then reads as a false "board missing".
    local probe_board="rp2040:rp2040:waveshare_rp2040_plus"
    local board_catalogue=""
    if ! board_catalogue=$("$ARDUINO_CLI" board listall rp2040:rp2040 2>/dev/null); then
        err "Failed to query the rp2040:rp2040 board catalogue via arduino-cli."
        err "Verify the core installed cleanly:"
        err "  $ARDUINO_CLI core install rp2040:rp2040@$RP2040_CORE_VERSION"
        exit 1
    fi
    if ! awk -v want="$probe_board" '$NF==want{hit=1} END{exit hit?0:1}' \
            <<<"$board_catalogue"; then
        err "Required board '$probe_board' missing from rp2040:rp2040."
        err "Likely cause: a wrong or incomplete arduino-pico core. Fix:"
        err "  $ARDUINO_CLI core install rp2040:rp2040@$RP2040_CORE_VERSION"
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

    # Keep dependency validation generic: bootstrap already hard-resets to
    # origin/<default-branch>, so we should not gate on implementation details
    # of a specific upstream commit.
    local hal_dir="$LIB_DIR/JaszczurHAL"
    if [[ ! -d "$hal_dir/.git" ]]; then
        err "JaszczurHAL checkout missing or invalid at: $hal_dir"
        return 1
    fi
    local required_hal_paths=(
        "$hal_dir/src/JaszczurHAL.h"
        "$hal_dir/src/hal/hal_config.cpp"
        "$hal_dir/src/hal/impl/.mock/hal_mock.h"
        "$hal_dir/src/hal/impl/shared/debug/hal_debug_format.cpp"
        "$hal_dir/src/hal/impl/shared/drivers/mcp2515/hal_can_mcp2515_config.cpp"
        "$hal_dir/src/hal/impl/shared/frameworks/smart_timers/SmartTimers.cpp"
        "$hal_dir/src/hal/impl/shared/frameworks/wireguard/crypto/chacha20.c"
        "$hal_dir/src/utils/tools.cpp"
        "$hal_dir/src/utils/unity.c"
    )
    local missing=()
    local path
    for path in "${required_hal_paths[@]}"; do
        [[ -e "$path" ]] || missing+=("$path")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        err "JaszczurHAL checkout layout is not compatible with this Fiesta tree."
        err "Missing expected path(s):"
        for path in "${missing[@]}"; do
            err "  $path"
        done
        return 1
    fi
    local hal_branch hal_rev
    hal_branch="$(resolve_origin_default_branch "$hal_dir" || echo "unknown")"
    hal_rev="$(git -C "$hal_dir" rev-parse --short HEAD 2>/dev/null || echo "unknown")"
    ok "JaszczurHAL synchronized (${hal_branch}@${hal_rev})"
}

# -----------------------------------------------------------------------------
# 4b. Git hooks
# -----------------------------------------------------------------------------
setup_git_hooks() {
    local repo_root hooks_dir
    if ! repo_root=$(git -C "$PROJECT_DIR/.." rev-parse --show-toplevel 2>/dev/null); then
        warn "Could not resolve git repo root - skipping git hooks setup"
        return
    fi

    hooks_dir="$repo_root/.githooks"
    if [[ ! -d "$hooks_dir" ]]; then
        warn "No .githooks directory at $hooks_dir - skipping git hooks setup"
        return
    fi

    info "Configuring git hooks path: $hooks_dir"
    git -C "$repo_root" config core.hooksPath "$hooks_dir"

    # Ensure hooks are executable even after fresh clone / archive extraction.
    while IFS= read -r -d '' hook_file; do
        chmod +x "$hook_file"
    done < <(find "$hooks_dir" -mindepth 1 -maxdepth 1 -type f -print0)

    ok "Git hooks configured (core.hooksPath=.githooks)"
}

run_tests() {
    if [[ "${SKIP_TESTS:-0}" = "1" ]]; then
        info "SKIP_TESTS=1 - skipping host tests"
        return
    fi

    local repo_root runalltests_script
    repo_root="$(dirname "$SRC_ROOT")"
    runalltests_script="$repo_root/runalltests.sh"

    if [[ ! -x "$runalltests_script" ]]; then
        err "runalltests.sh not found or not executable at: $runalltests_script"
        err "Cannot continue host QA from bootstrap."
        return 1
    fi

    info "Running host QA via runalltests.sh"
    "$runalltests_script"
    ok "runalltests.sh completed"
}

# -----------------------------------------------------------------------------
# 6. Firmware compile (per module)
# -----------------------------------------------------------------------------
read_json_key() {
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
    local manifest="$src/.vscode/jaszczurhal.project.json"
    local ajson="$src/.vscode/arduino.json"

    local board config fqbn=""
    fqbn=$(read_json_key "$manifest" fqbn)
    if [[ -z "$fqbn" ]]; then
        board=$(read_json_key "$ajson" board)
        config=$(read_json_key "$ajson" configuration)
        if [[ -n "$board" ]]; then
            fqbn="$board"
            [[ -n "$config" ]] && fqbn="${board}:${config}"
        fi
    fi
    if [[ -z "$fqbn" ]]; then
        fqbn="$DEFAULT_FQBN"
        info "[$module] no VS Code project manifest/arduino.json FQBN - using default FQBN"
    fi

    info "[$module] compiling firmware (FQBN: $fqbn)"
    FIESTA_ARDUINO_CLI="$ARDUINO_CLI" \
    FIESTA_ARDUINO_FQBN="$fqbn" \
    FIESTA_LIBRARIES_DIR="$LIB_DIR" \
        fiesta_run_compile "$src" build "$src" "$werror" 1 0 ""

    local uf2
    uf2=$(fiesta_find_uf2_artifact "$build" || true)
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
    local sc_build_script="$sc_dir/scripts/desktop-build.sh"

    if [[ ! -f "$sc_build_script" ]]; then
        warn "SerialConfigurator: desktop-build.sh not found at $sc_build_script - skipping"
        return
    fi

    info "[SerialConfigurator] calling desktop-build.sh for build + tests"
    if ! bash "$sc_build_script" build; then
        err "[SerialConfigurator] build failed"
        return 1
    fi

    if ! bash "$sc_build_script" test; then
        err "[SerialConfigurator] tests failed"
        return 1
    fi

    if [[ "${SKIP_DESKTOP_PACKAGE:-0}" = "1" ]]; then
        info "[SerialConfigurator] SKIP_DESKTOP_PACKAGE=1 - skipping .deb packaging"
    else
        info "[SerialConfigurator] calling desktop-build.sh for .deb package"
        if ! bash "$sc_build_script" package; then
            err "[SerialConfigurator] package build failed"
            return 1
        fi
    fi

    local sc_build="$sc_dir/build"
    local deb
    deb=$(find "$sc_build" -maxdepth 1 -type f -name '*.deb' -print -quit)
    if [[ -n "$deb" ]]; then
        ok "[SerialConfigurator] Debian package: $deb"
    elif [[ "${SKIP_DESKTOP_PACKAGE:-0}" != "1" ]]; then
        warn "[SerialConfigurator] package step completed but no .deb found in $sc_build"
    fi

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
setup_git_hooks
run_tests
compile_firmware
build_serial_configurator

ok "Bootstrap finished"
