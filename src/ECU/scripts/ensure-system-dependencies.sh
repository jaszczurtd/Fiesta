#!/usr/bin/env bash
set -euo pipefail

FIESTA_APT_PACKAGES=(
    git build-essential cmake ninja-build python3 curl ca-certificates perl
    gcc-arm-none-eabi libstdc++-arm-none-eabi-newlib
    libusb-1.0-0-dev pkg-config
    libgtk-4-dev dpkg-dev libshumate-dev xvfb xauth locales
    clang-format clang-tidy clang-tools valgrind default-jre-headless
)

# The locales the GUI tests need, one per line; CI and the SerialConfigurator
# tests read the same file. Builtins only: callers may run without PATH.
if [[ "${BASH_SOURCE[0]}" == */* ]]; then
    FIESTA_TEST_LOCALES_FILE="$(cd "${BASH_SOURCE[0]%/*}" && pwd)"
else
    FIESTA_TEST_LOCALES_FILE="$PWD"
fi
FIESTA_TEST_LOCALES_FILE="${FIESTA_TEST_LOCALES_FILE}/../../../scripts/test-locales.txt"

# Debian's locale-gen, Raspberry Pi OS included, ignores its arguments and
# builds only the locales this file enables; Ubuntu's builds its arguments.
# Tests point it at a fixture.
FIESTA_LOCALE_GEN_CONFIG="${FIESTA_LOCALE_GEN_CONFIG:-/etc/locale.gen}"

dependency_info() {
    printf '[INFO] %s\n' "$*"
}

dependency_ok() {
    printf '[OK] %s\n' "$*"
}

dependency_error() {
    printf '[ERROR] %s\n' "$*" >&2
}

list_missing_system_packages() {
    MISSING_SYSTEM_PACKAGES=()
    local package status

    if ! command -v dpkg-query >/dev/null 2>&1; then
        dependency_error 'dpkg-query is required on Debian-like hosts.'
        return 1
    fi

    for package in "${FIESTA_APT_PACKAGES[@]}"; do
        status="$(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true)"
        if [[ "$status" != 'install ok installed' ]]; then
            MISSING_SYSTEM_PACKAGES+=("$package")
        fi
    done
}

run_dependency_command_as_root() {
    local non_interactive="$1"
    shift

    if [[ $EUID -eq 0 ]]; then
        "$@"
        return
    fi

    if ! command -v sudo >/dev/null 2>&1; then
        dependency_error "sudo is required to install: ${MISSING_SYSTEM_PACKAGES[*]}"
        return 1
    fi

    if [[ "$non_interactive" == '1' ]]; then
        if ! sudo -n true >/dev/null 2>&1; then
            dependency_error 'Unattended package installation requires passwordless sudo.'
            dependency_error "Missing packages: ${MISSING_SYSTEM_PACKAGES[*]}"
            return 1
        fi
        sudo -n "$@"
        return
    fi

    sudo "$@"
}

# Prints the listed locales the system does not provide.
missing_test_locales() {
    python3 - <<PYLOCALE
import locale
missing = []
for name in "$*".split():
    try:
        locale.setlocale(locale.LC_ALL, name)
    except locale.Error:
        missing.append(name)
print(" ".join(missing))
PYLOCALE
}

# Enables the listed locales in the locale-gen configuration, when the host
# has one, as "<locale> <charset>" entries. Uncomments an existing entry or
# appends a new one; leaves the file untouched when all are enabled.
enable_test_locales() {
    local non_interactive="$1"
    shift
    [[ -f "$FIESTA_LOCALE_GEN_CONFIG" ]] || return 0
    run_dependency_command_as_root "$non_interactive" python3 - \
        "$FIESTA_LOCALE_GEN_CONFIG" "$@" <<'PYENABLE'
import sys

path, names = sys.argv[1], sys.argv[2:]
with open(path, encoding="utf-8") as stream:
    lines = stream.read().splitlines()


def entry(line):
    """The (locale, charset) pair of an entry line, commented or not."""
    fields = line.strip().lstrip("#").split()
    return tuple(fields) if len(fields) == 2 else None


changed = False
for name in names:
    wanted = (name, name.partition(".")[2].partition("@")[0])
    if not wanted[1] or wanted in (
            entry(line) for line in lines if not line.lstrip().startswith("#")):
        continue
    for index, line in enumerate(lines):
        if line.lstrip().startswith("#") and entry(line) == wanted:
            lines[index] = " ".join(wanted)
            break
    else:
        lines.append(" ".join(wanted))
    changed = True
if changed:
    with open(path, "w", encoding="utf-8") as stream:
        stream.write("\n".join(lines) + "\n")
PYENABLE
}

ensure_test_locales() {
    local mode="$1" non_interactive="$2" missing
    local -a locales=()
    if ! mapfile -t locales < "$FIESTA_TEST_LOCALES_FILE" ||
        [[ ${#locales[@]} -eq 0 ]]; then
        dependency_error "No test locales in $FIESTA_TEST_LOCALES_FILE"
        return 1
    fi
    missing=$(missing_test_locales "${locales[@]}")
    if [[ -z "$missing" ]]; then
        return 0
    fi
    if [[ "$mode" == 'check' ]]; then
        dependency_error "Missing GUI test locales: $missing"
        return 1
    fi
    if ! enable_test_locales "$non_interactive" "${locales[@]}"; then
        dependency_error "Could not enable the GUI test locales in $FIESTA_LOCALE_GEN_CONFIG"
        return 1
    fi
    run_dependency_command_as_root "$non_interactive" locale-gen "${locales[@]}" || return 1
    # Some locale-gen versions ignore their arguments and still succeed.
    missing=$(missing_test_locales "${locales[@]}")
    if [[ -n "$missing" ]]; then
        dependency_error "locale-gen did not provide the GUI test locales: $missing"
        return 1
    fi
}

ensure_system_dependencies() {
    local mode='install'
    local non_interactive='0'

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --check)
                mode='check'
                ;;
            --install)
                mode='install'
                ;;
            --non-interactive)
                non_interactive='1'
                ;;
            --print-packages)
                printf '%s\n' "${FIESTA_APT_PACKAGES[@]}"
                return 0
                ;;
            *)
                dependency_error "Unknown option: $1"
                return 2
                ;;
        esac
        shift
    done

    list_missing_system_packages
    if [[ ${#MISSING_SYSTEM_PACKAGES[@]} -eq 0 ]]; then
        dependency_ok 'All required system packages are installed.'
        ensure_test_locales "$mode" "$non_interactive"
        return
    fi

    if [[ "$mode" == 'check' ]]; then
        dependency_error "Missing system packages: ${MISSING_SYSTEM_PACKAGES[*]}"
        return 1
    fi

    dependency_info "Installing system packages: ${MISSING_SYSTEM_PACKAGES[*]}"
    run_dependency_command_as_root "$non_interactive" apt-get update
    run_dependency_command_as_root "$non_interactive" \
        apt-get install -y --no-install-recommends "${MISSING_SYSTEM_PACKAGES[@]}"

    list_missing_system_packages
    if [[ ${#MISSING_SYSTEM_PACKAGES[@]} -ne 0 ]]; then
        dependency_error "Packages remain missing after apt: ${MISSING_SYSTEM_PACKAGES[*]}"
        return 1
    fi
    dependency_ok 'Required system packages are installed.'
    ensure_test_locales "$mode" "$non_interactive"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    ensure_system_dependencies "$@"
fi
