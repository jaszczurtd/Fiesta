#!/usr/bin/env bash
# Initialize the HAL revision recorded in Fiesta's index without discarding work.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
hal_path="src/JaszczurHAL"
hal_dir="$repo_root/$hal_path"
if [[ -e "$hal_dir/.git" ]]; then
    if [[ -n "$(git -C "$hal_dir" status --porcelain --untracked-files=normal)" ]]; then
        echo "JaszczurHAL has local changes at $hal_dir; preserve them before updating the submodule." >&2
        exit 1
    fi
fi
git -C "$repo_root" submodule update --init --checkout -- "$hal_path"
if [[ ! -e "$hal_dir/.git" || ! -f "$hal_dir/src/JaszczurHAL.h" ]]; then
    echo "JaszczurHAL submodule is missing at $hal_dir." >&2
    exit 1
fi
