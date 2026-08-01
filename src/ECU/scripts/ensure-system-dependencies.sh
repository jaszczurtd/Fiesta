#!/usr/bin/env bash
set -euo pipefail

FIESTA_APT_PACKAGES=(
    git build-essential cmake ninja-build python3 curl ca-certificates perl
    gcc-arm-none-eabi libstdc++-arm-none-eabi-newlib
    libusb-1.0-0-dev pkg-config
    libgtk-4-dev dpkg-dev libshumate-dev
    clang-format clang-tidy clang-tools valgrind cppcheck
)

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
        return 0
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
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    ensure_system_dependencies "$@"
fi
