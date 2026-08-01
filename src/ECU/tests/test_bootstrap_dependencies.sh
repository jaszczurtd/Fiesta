#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPENDENCY_SCRIPT="$SCRIPT_DIR/../scripts/ensure-system-dependencies.sh"
BOOTSTRAP_SCRIPT="$SCRIPT_DIR/../scripts/bootstrap.sh"
DAILY_RUNNER="$SCRIPT_DIR/../scripts/systemd/fiesta-bootstrap-run.sh"

# shellcheck source=/dev/null
source "$DEPENDENCY_SCRIPT"

fail() {
    echo "test_bootstrap_dependencies: $*" >&2
    exit 1
}

packages="$(ensure_system_dependencies --print-packages)"
grep -qx 'gcc-arm-none-eabi' <<<"$packages" || fail 'GNU Arm package missing'
grep -qx 'libstdc++-arm-none-eabi-newlib' <<<"$packages" || \
    fail 'Arm C++ runtime package missing'
grep -qx 'ninja-build' <<<"$packages" || fail 'Ninja package missing'
grep -Fq '"$SYSTEM_DEPENDENCY_SCRIPT" "${arguments[@]}"' "$BOOTSTRAP_SCRIPT" || \
    fail 'bootstrap does not invoke the system dependency helper'
grep -Fq 'SKIP_APT=0 APT_NONINTERACTIVE=1' "$DAILY_RUNNER" || \
    fail 'daily runner does not enable unattended package installation'

fixture_installed=0
declare -a apt_calls=()

dpkg-query() {
    if [[ "$fixture_installed" == '1' ]]; then
        printf 'install ok installed'
        return 0
    fi
    return 1
}

apt-get() {
    apt_calls+=("$*")
    if [[ "$1" == 'install' ]]; then
        fixture_installed=1
    fi
}

sudo() {
    [[ "$1" == '-n' ]] || fail 'unattended install omitted sudo -n'
    shift
    if [[ "$1" == 'true' ]]; then
        return 0
    fi
    "$@"
}

ensure_system_dependencies --install --non-interactive
[[ ${#apt_calls[@]} -eq 2 ]] || fail 'expected apt update and install calls'
[[ "${apt_calls[0]}" == 'update' ]] || fail 'apt update was not first'
[[ "${apt_calls[1]}" == *'ninja-build'* ]] || fail 'Ninja was not installed'
[[ "${apt_calls[1]}" == *'gcc-arm-none-eabi'* ]] || fail 'GNU Arm was not installed'

apt_calls=()
ensure_system_dependencies --check
[[ ${#apt_calls[@]} -eq 0 ]] || fail 'check mode changed package state'

fixture_installed=0
sudo() { return 1; }
if ensure_system_dependencies --install --non-interactive 2>/dev/null; then
    fail 'unattended install accepted unavailable sudo privileges'
fi

echo 'test_bootstrap_dependencies: PASS'
