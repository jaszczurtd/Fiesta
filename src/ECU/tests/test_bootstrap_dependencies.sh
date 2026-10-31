#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPENDENCY_SCRIPT="$SCRIPT_DIR/../scripts/ensure-system-dependencies.sh"
BOOTSTRAP_SCRIPT="$SCRIPT_DIR/../scripts/bootstrap.sh"
DAILY_RUNNER="$SCRIPT_DIR/../scripts/systemd/fiesta-bootstrap-run.sh"

# shellcheck source=/dev/null
source "$DEPENDENCY_SCRIPT"

# Failures report on the original stderr, also from calls that hide theirs.
exec 3>&2
fail() {
    echo "test_bootstrap_dependencies: $*" >&3
    exit 1
}

packages="$(ensure_system_dependencies --print-packages)"
grep -qx 'gcc-arm-none-eabi' <<<"$packages" || fail 'GNU Arm package missing'
grep -qx 'libstdc++-arm-none-eabi-newlib' <<<"$packages" || \
    fail 'Arm C++ runtime package missing'
grep -qx 'ninja-build' <<<"$packages" || fail 'Ninja package missing'
grep -qx 'default-jre-headless' <<<"$packages" || \
    fail 'Java runtime for PMD CPD missing'
grep -Fq '"$SYSTEM_DEPENDENCY_SCRIPT" "${arguments[@]}"' "$BOOTSTRAP_SCRIPT" || \
    fail 'bootstrap does not invoke the system dependency helper'
grep -Fq 'SKIP_APT=0 APT_NONINTERACTIVE=1' "$DAILY_RUNNER" || \
    fail 'daily runner does not enable unattended package installation'

fixture_installed=0
# Locales the fixture system provides, and how its locale-gen works: from its
# arguments (Ubuntu), from the configuration only (Debian), or not at all.
fixture_generated=''
fixture_locale_gen='arguments'
fixture_dir="$(mktemp -d)"
trap 'rm -rf "$fixture_dir"' EXIT
# Locale checks, edits and generation must never use the host configuration.
FIESTA_LOCALE_GEN_CONFIG="$fixture_dir/absent.gen"
declare -a apt_calls=()
declare -a locale_calls=()

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

python3() {
    if [[ "$#" -eq 1 && "$1" == '-' ]]; then
        cat >/dev/null
        local name
        local -a missing=()
        for name in en_US.UTF-8 pl_PL.UTF-8; do
            [[ " $fixture_generated " == *" $name "* ]] || missing+=("$name")
        done
        printf '%s\n' "${missing[*]}"
        return 0
    fi
    # The configuration edit runs for real, on the fixture only.
    [[ "$#" -ge 3 && "$1" == '-' && "$2" == "$FIESTA_LOCALE_GEN_CONFIG" &&
        "$2" == "$fixture_dir"/* ]] || fail "unexpected Python command: $*"
    command python3 "$@"
}

# What Debian's locale-gen builds: the enabled entries of its configuration.
enabled_fixture_locales() {
    local locale rest
    while read -r locale rest; do
        if [[ -n "$locale" && "$locale" != \#* ]]; then
            printf '%s ' "$locale"
        fi
    done < "$FIESTA_LOCALE_GEN_CONFIG"
}

locale-gen() {
    locale_calls+=("$*")
    case "$fixture_locale_gen" in
        arguments) fixture_generated="$*" ;;
        config) fixture_generated="$(enabled_fixture_locales)" ;;
        none) ;;
    esac
}

sudo() {
    [[ "$1" == '-n' ]] || fail 'unattended install omitted sudo -n'
    shift
    if [[ "$1" == 'true' ]]; then
        return 0
    fi
    case "$1" in
        apt-get|locale-gen|python3) "$@" ;;
        *) fail "unmocked privileged command: $1" ;;
    esac
}

ensure_system_dependencies --install --non-interactive
[[ ${#apt_calls[@]} -eq 2 ]] || fail 'expected apt update and install calls'
[[ "${apt_calls[0]}" == 'update' ]] || fail 'apt update was not first'
[[ "${apt_calls[1]}" == *'ninja-build'* ]] || fail 'Ninja was not installed'
[[ "${apt_calls[1]}" == *'gcc-arm-none-eabi'* ]] || fail 'GNU Arm was not installed'

[[ ${#locale_calls[@]} -eq 1 ]] || fail 'expected one locale generation call'
[[ "${locale_calls[0]}" == 'en_US.UTF-8 pl_PL.UTF-8' ]] || fail 'wrong test locales'

apt_calls=()
locale_calls=()
ensure_system_dependencies --check
[[ ${#apt_calls[@]} -eq 0 ]] || fail 'check mode changed package state'
[[ ${#locale_calls[@]} -eq 0 ]] || fail 'check mode generated locales'

# Raspberry Pi OS: pl_PL.UTF-8 enabled, en_US.UTF-8 only as a comment.
FIESTA_LOCALE_GEN_CONFIG="$fixture_dir/locale.gen"
printf '# Locales to build\n# en_US.UTF-8 UTF-8\npl_PL.UTF-8 UTF-8\n' \
    > "$FIESTA_LOCALE_GEN_CONFIG"
pi_configuration="$(<"$FIESTA_LOCALE_GEN_CONFIG")"
fixture_generated='pl_PL.UTF-8'
if ensure_system_dependencies --check 2>/dev/null; then
    fail 'check mode accepted missing test locales'
fi
[[ ${#apt_calls[@]} -eq 0 && ${#locale_calls[@]} -eq 0 ]] || \
    fail 'missing-locale check changed system state'
[[ "$(<"$FIESTA_LOCALE_GEN_CONFIG")" == "$pi_configuration" ]] || \
    fail 'check mode edited the locale-gen configuration'

# Debian's locale-gen ignores its arguments, so the entry has to be enabled.
fixture_locale_gen='config'
ensure_system_dependencies --install --non-interactive || \
    fail 'install left a locale that Debian locale-gen ignores'
[[ ${#locale_calls[@]} -eq 1 ]] || fail 'expected one locale generation call'
[[ "$(<"$FIESTA_LOCALE_GEN_CONFIG")" == \
    $'# Locales to build\nen_US.UTF-8 UTF-8\npl_PL.UTF-8 UTF-8' ]] || \
    fail 'the commented entry was not enabled in place'

# An entry the configuration lacks is appended once.
printf 'pl_PL.UTF-8 UTF-8\n' > "$FIESTA_LOCALE_GEN_CONFIG"
fixture_generated='pl_PL.UTF-8'
locale_calls=()
ensure_system_dependencies --install --non-interactive || \
    fail 'install did not provide a locale missing from the configuration'
[[ "$(<"$FIESTA_LOCALE_GEN_CONFIG")" == $'pl_PL.UTF-8 UTF-8\nen_US.UTF-8 UTF-8' ]] || \
    fail 'the missing entry was not appended once'
ensure_system_dependencies --install --non-interactive
[[ ${#locale_calls[@]} -eq 1 ]] || fail 'provided locales were generated again'

# A locale-gen that exits 0 without the locales fails; the result counts.
fixture_locale_gen='none'
fixture_generated=''
locale_calls=()
if ensure_system_dependencies --install --non-interactive 2>/dev/null; then
    fail 'install accepted locales that locale-gen did not provide'
fi
[[ ${#locale_calls[@]} -eq 1 ]] || fail 'expected one locale generation call'
fixture_generated='en_US.UTF-8 pl_PL.UTF-8'

fixture_installed=0
sudo() { return 1; }
if ensure_system_dependencies --install --non-interactive 2>/dev/null; then
    fail 'unattended install accepted unavailable sudo privileges'
fi

echo 'test_bootstrap_dependencies: PASS'
