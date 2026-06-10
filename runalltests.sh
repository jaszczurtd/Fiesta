#!/usr/bin/env bash
# =============================================================================
# runalltests.sh
#
# Run the Fiesta multi-module quality gates locally, inspired by the
# JaszczurHAL runalltests workflow.
#
# Gates (in order):
#   1. Tool presence check
#   2. Host runtime tests for all modules (cmake + ctest)
#   3. Static analysis: cppcheck (ECU)
#   4. Valgrind memcheck targets for all modules
#   5. clang-tidy targets for all modules
#
# Covered modules:
#   - ECU
#   - Adjustometer
#   - Clocks
#   - OilAndSpeed
#   - SerialConfigurator
#
# Usage:
#   ./runalltests.sh
#   ./runalltests.sh -j8
#   ./runalltests.sh --skip-cppcheck
#   ./runalltests.sh --skip-valgrind
#   ./runalltests.sh --skip-clang-tidy
#   ./runalltests.sh --help
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

info()   { echo -e "${CYAN}[INFO]${NC} $*"; }
pass()   { echo -e "${GREEN}[PASS]${NC} $*"; }
fail()   { echo -e "${RED}[FAIL]${NC} $*"; }
header() {
    echo -e "\n${BOLD}══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  $*${NC}"
    echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
}

run_logged() {
    local log_file="$1"
    shift

    if ! "$@" 2>&1 | tee "${log_file}"; then
        fail "Command failed: $*"
        if [[ -s "${log_file}" ]]; then
            echo ""
            tail -80 "${log_file}"
        fi
        exit 1
    fi
}

JOBS="$(nproc 2>/dev/null || echo 4)"
SKIP_CPPCHECK=0
SKIP_VALGRIND=0
SKIP_CLANG_TIDY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -j*)
            JOBS="${1#-j}"
            shift
            ;;
        --skip-cppcheck)
            SKIP_CPPCHECK=1
            shift
            ;;
        --skip-valgrind)
            SKIP_VALGRIND=1
            shift
            ;;
        --skip-clang-tidy)
            SKIP_CLANG_TIDY=1
            shift
            ;;
        -h|--help)
            awk 'NR >= 4 { if ($0 ~ /^# =/) exit; print }' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

SECONDS=0

# name:source_dir:build_dir
MODULE_MATRIX=(
    "ECU:src/ECU:src/ECU/build_test"
    "Adjustometer:src/Adjustometer:src/Adjustometer/build_test"
    "Clocks:src/Clocks:src/Clocks/build_test"
    "OilAndSpeed:src/OilAndSpeed:src/OilAndSpeed/build_test"
    "SerialConfigurator:src/SerialConfigurator:src/SerialConfigurator/build"
)

configure_build_and_test_module() {
    local name="$1"
    local src_rel="$2"
    local build_rel="$3"

    local src_abs="${SCRIPT_DIR}/${src_rel}"
    local build_abs="${SCRIPT_DIR}/${build_rel}"

    info "[${name}] configuring"
    run_logged "/tmp/fiesta_${name}_configure.log" \
        cmake -S "${src_abs}" -B "${build_abs}" \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    info "[${name}] building"
    run_logged "/tmp/fiesta_${name}_build.log" \
        cmake --build "${build_abs}" --parallel "${JOBS}"

    info "[${name}] running ctest (runtime-only, static-analysis label excluded)"
    run_logged "/tmp/fiesta_${name}_ctest.log" \
        ctest --test-dir "${build_abs}" --progress --output-on-failure -LE static-analysis

    pass "[${name}] host tests passed"
}

run_module_target() {
    local name="$1"
    local build_rel="$2"
    local target="$3"

    local build_abs="${SCRIPT_DIR}/${build_rel}"
    info "[${name}] running target: ${target}"
    run_logged "/tmp/fiesta_${name}_${target}.log" \
        cmake --build "${build_abs}" --target "${target}" --parallel "${JOBS}"
    pass "[${name}] ${target} passed"
}

header "Gate 1/5: Checking required tools"

REQUIRED_TOOLS=(cmake ctest gcc g++ make)
if [[ "${SKIP_CPPCHECK}" -eq 0 ]]; then
    REQUIRED_TOOLS+=(cppcheck)
fi
if [[ "${SKIP_VALGRIND}" -eq 0 ]]; then
    REQUIRED_TOOLS+=(valgrind)
fi
if [[ "${SKIP_CLANG_TIDY}" -eq 0 ]]; then
    REQUIRED_TOOLS+=(clang-tidy run-clang-tidy)
fi

missing=0
for tool in "${REQUIRED_TOOLS[@]}"; do
    if command -v "${tool}" >/dev/null 2>&1; then
        printf '  %-20s %s\n' "${tool}" "$(command -v "${tool}")"
    else
        fail "MISSING: ${tool}"
        missing=1
    fi
done

if [[ "${missing}" -ne 0 ]]; then
    fail "Missing required tools. Install prerequisites and retry."
    exit 1
fi

pass "All required tools present."

header "Gate 2/5: Host runtime tests for all modules"
for entry in "${MODULE_MATRIX[@]}"; do
    IFS=':' read -r name src_rel build_rel <<< "${entry}"
    configure_build_and_test_module "${name}" "${src_rel}" "${build_rel}"
done
pass "All module host tests passed."

if [[ "${SKIP_CPPCHECK}" -eq 0 ]]; then
    header "Gate 3/5: Static analysis - cppcheck (ECU)"
    run_module_target "ECU" "src/ECU/build_test" "check-cppcheck"
    pass "cppcheck gate passed."
else
    info "Gate 3/5 skipped (--skip-cppcheck)."
fi

if [[ "${SKIP_VALGRIND}" -eq 0 ]]; then
    header "Gate 4/5: Valgrind memcheck targets"
    for entry in "${MODULE_MATRIX[@]}"; do
        IFS=':' read -r name _ build_rel <<< "${entry}"
        run_module_target "${name}" "${build_rel}" "check-valgrind"
    done
    pass "Valgrind gate passed for all modules."
else
    info "Gate 4/5 skipped (--skip-valgrind)."
fi

if [[ "${SKIP_CLANG_TIDY}" -eq 0 ]]; then
    header "Gate 5/5: clang-tidy targets"
    for entry in "${MODULE_MATRIX[@]}"; do
        IFS=':' read -r name _ build_rel <<< "${entry}"
        run_module_target "${name}" "${build_rel}" "check-clang-tidy"
    done
    pass "clang-tidy gate passed for all modules."
else
    info "Gate 5/5 skipped (--skip-clang-tidy)."
fi

echo ""
echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}${BOLD}  ALL REQUESTED GATES PASSED ✓${NC}"
echo -e "${BOLD}══════════════════════════════════════════════════════════════${NC}"
echo ""
echo "  Modules tested: ECU, Adjustometer, Clocks, OilAndSpeed, SerialConfigurator"
echo "  Total time: ${SECONDS}s"
echo ""
