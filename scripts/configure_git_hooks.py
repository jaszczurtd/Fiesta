#!/usr/bin/env python3
"""Configure the Fiesta repository hooks without shell-specific commands."""

from __future__ import annotations

import os
from pathlib import Path
import stat
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]
HOOKS_DIR = REPO_ROOT / ".githooks"


def main() -> int:
    if not HOOKS_DIR.is_dir():
        print(f"error: hooks directory not found: {HOOKS_DIR}", file=sys.stderr)
        return 1

    result = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "config", "core.hooksPath", str(HOOKS_DIR)],
        check=False,
    )
    if result.returncode != 0:
        return result.returncode

    if os.name != "nt":
        executable_bits = stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        for hook in HOOKS_DIR.iterdir():
            if hook.is_file():
                hook.chmod(hook.stat().st_mode | executable_bits)

    print(f"Configured core.hooksPath: {HOOKS_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
