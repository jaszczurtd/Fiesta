#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: check_misra.sh [options]

Run a project-local MISRA screening pass for src/ECU using cppcheck's MISRA addon.

Options:
  -c, --cppcheck <path>       cppcheck executable or absolute path
  -o, --out <dir>             output directory for generated artifacts
  -q, --quiet                 suppress cppcheck "Checking ..." progress lines
      --rule-texts <file>     licensed local MISRA Appendix A text extract
      --addon-python <path>   Python interpreter for cppcheck addons
      --fail-on-findings      return non-zero when active MISRA findings exist
  -h, --help                  show this help text

Environment:
  CPPCHECK_BIN                default for --cppcheck
  CPPCHECK_ADDON_PYTHON       default for --addon-python
  MISRA_RULE_TEXTS            default for --rule-texts

Notes:
  - This runner does not vendor MISRA rule texts.
  - If rule texts are not provided, the addon still reports rule IDs, but
    severity split by Mandatory / Required / Advisory is unavailable.
EOF
}

json_escape() {
    local value="$1"
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    printf '%s' "$value"
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd "$script_dir/.." && pwd)
out_dir="$script_dir/.results"
cppcheck_bin="${CPPCHECK_BIN:-cppcheck}"
addon_python="${CPPCHECK_ADDON_PYTHON:-python3}"
rule_texts="${MISRA_RULE_TEXTS:-}"
quiet=0
fail_on_findings=0
temp_addon_config=""
hal_src="$project_root/../../../libraries/JaszczurHAL/src"
can_defs_dir="$project_root/../../../libraries/canDefinitions"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--cppcheck)
            cppcheck_bin="$2"
            shift 2
            ;;
        -o|--out)
            out_dir="$2"
            shift 2
            ;;
        -q|--quiet)
            quiet=1
            shift
            ;;
        --rule-texts)
            rule_texts="$2"
            shift 2
            ;;
        --addon-python)
            addon_python="$2"
            shift 2
            ;;
        --fail-on-findings)
            fail_on_findings=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$rule_texts" && -f "$script_dir/rule-texts.local.txt" ]]; then
    rule_texts="$script_dir/rule-texts.local.txt"
fi

if ! command -v "$cppcheck_bin" >/dev/null 2>&1; then
    echo "cppcheck not found: $cppcheck_bin" >&2
    exit 2
fi

if ! command -v "$addon_python" >/dev/null 2>&1; then
    echo "Python interpreter for cppcheck addons not found: $addon_python" >&2
    exit 2
fi

if [[ -n "$rule_texts" && ! -f "$rule_texts" ]]; then
    echo "MISRA rule texts file not found: $rule_texts" >&2
    exit 2
fi

mkdir -p "$out_dir"

if [[ -n "$rule_texts" ]]; then
    temp_addon_config=$(mktemp "$out_dir/misra-addon.XXXXXX.json")
    escaped_rule_texts=$(json_escape "$rule_texts")
    cat > "$temp_addon_config" <<EOF
{"script":"misra.py","args":["--rule-texts=$escaped_rule_texts"]}
EOF
fi

cleanup() {
    if [[ -n "$temp_addon_config" && -f "$temp_addon_config" ]]; then
        rm -f "$temp_addon_config"
    fi
}

trap cleanup EXIT

results_file="$out_dir/results.txt"
active_file="$out_dir/active-findings.txt"
rule_counts_file="$out_dir/rule-counts.txt"
summary_file="$out_dir/summary.txt"
count_file="$out_dir/error_count.txt"
severity_counts_file="$out_dir/severity-counts.txt"
suppressions_args=()

addon_arg="misra"
if [[ -n "$temp_addon_config" ]]; then
    addon_arg="$temp_addon_config"
fi

if grep -Eq '^[[:space:]]*[^#[:space:]]' "$script_dir/suppressions.txt"; then
    suppressions_args=(--suppressions-list="$script_dir/suppressions.txt")
fi

quiet_args=()
if [[ $quiet -eq 1 ]]; then
    quiet_args=(--quiet)
fi

include_args=(-I.)
if [[ -d "$can_defs_dir" ]]; then
    include_args+=(-I"$can_defs_dir")
fi
if [[ -d "$hal_src" ]]; then
    include_args+=(-I"$hal_src" -I"$hal_src/utils")
fi

define_args=(
    -DUNIT_TEST
    -DSERIAL_7N1=2
    -DSERIAL_8N1=6
)

pushd "$project_root" >/dev/null

set +e
"$cppcheck_bin" \
    --addon="$addon_arg" \
    --addon-python="$addon_python" \
    --enable=warning,style,performance,portability,information \
    --std=c99 \
    --language=c \
    --inline-suppr \
    "${include_args[@]}" \
    "${define_args[@]}" \
    "${suppressions_args[@]}" \
    --suppress=missingInclude \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --suppress=knownConditionTrueFalse \
    --suppress=checkersReport \
    "${quiet_args[@]}" \
    *.c \
    2>&1 | tee "$results_file"
cppcheck_status=${PIPESTATUS[0]}
set -e

popd >/dev/null

if [[ $cppcheck_status -ne 0 ]]; then
    echo "cppcheck MISRA scan failed with exit code $cppcheck_status" >&2
    exit $cppcheck_status
fi

grep -E '\[misra-c2012-[0-9]+\.[0-9]+\]$' "$results_file" \
    | awk -v root_prefix="$project_root/" '
        {
            split($0, parts, ":");
            file = parts[1];
            if (file == "nofile") {
                next;
            }
            if (file ~ /^\//) {
                if (index(file, root_prefix) == 1) {
                    print;
                }
                next;
            }
            print;
        }
    ' > "$active_file" || true

if [[ -s "$active_file" ]]; then
    sed -n 's/.*\[\(misra-c2012-[0-9]\+\.[0-9]\+\)\]$/\1/p' "$active_file" \
        | sort \
        | uniq -c \
        | awk '{print $2 " " $1}' > "$rule_counts_file"
else
    : > "$rule_counts_file"
fi

severity_breakdown_available=0
if [[ -n "$rule_texts" ]]; then
    severity_breakdown_available=1
    {
        printf 'Mandatory %s\n' "$(grep -Ec 'Mandatory - .*\[misra-c2012-[0-9]+\.[0-9]+\]$' "$active_file" || true)"
        printf 'Required %s\n' "$(grep -Ec 'Required - .*\[misra-c2012-[0-9]+\.[0-9]+\]$' "$active_file" || true)"
        printf 'Advisory %s\n' "$(grep -Ec 'Advisory - .*\[misra-c2012-[0-9]+\.[0-9]+\]$' "$active_file" || true)"
    } > "$severity_counts_file"
else
    : > "$severity_counts_file"
fi

active_count=$(wc -l < "$active_file")
printf '%s\n' "$active_count" > "$count_file"

{
    echo "ECU MISRA screening summary"
    echo "Generated: $(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    echo "Project root: $project_root"
    echo "Cppcheck: $("$cppcheck_bin" --version 2>&1 | head -n 1)"
    echo "Output directory: $out_dir"
    if [[ ${#suppressions_args[@]} -gt 0 ]]; then
        echo "Suppressions file: $script_dir/suppressions.txt"
    else
        echo "Suppressions file: not used (no active entries)"
    fi
    if [[ -n "$rule_texts" ]]; then
        echo "Rule texts: $rule_texts"
    else
        echo "Rule texts: not configured"
    fi
    echo "Active MISRA findings: $active_count"
    if [[ $severity_breakdown_available -eq 1 ]]; then
        echo "Severity breakdown: available"
        cat "$severity_counts_file"
    else
        echo "Severity breakdown: unavailable without a licensed local MISRA rule-text extract"
    fi
    if [[ -s "$rule_counts_file" ]]; then
        echo "Findings by rule:"
        cat "$rule_counts_file"
    else
        echo "Findings by rule: none"
    fi
} > "$summary_file"

echo "Active MISRA findings: $active_count"
echo "Artifacts written to: $out_dir"

if [[ $fail_on_findings -eq 1 && $active_count -gt 0 ]]; then
    exit 1
fi
