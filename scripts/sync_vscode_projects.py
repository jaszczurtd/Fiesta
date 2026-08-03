#!/usr/bin/env python3
"""Synchronize Fiesta firmware VS Code files with JaszczurHAL generators."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_JH_ROOT = REPO_ROOT.parent / "libraries" / "JaszczurHAL"
MODULES = ("ECU", "Clocks", "OilAndSpeed", "Adjustometer", "Fiesta_clock")
HOOK_TASK_LABEL = "Project: Configure Git hooks"
LINUX_ONLY_TASK_LABELS = {"Quality: Cppcheck baseline", "Quality: MISRA scan"}


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def json_text(value: Any, *, indent: int = 4) -> str:
    return json.dumps(value, indent=indent, ensure_ascii=False) + "\n"


def hook_task(script_path: str) -> dict[str, Any]:
    return {
        "label": HOOK_TASK_LABEL,
        "detail": "Auto-run on folder open - set core.hooksPath to repo .githooks",
        "type": "process",
        "command": "python3",
        "windows": {
            "command": "py",
            "args": ["-3", script_path],
        },
        "args": [script_path],
        "runOptions": {"runOn": "folderOpen"},
        "presentation": {
            "echo": True,
            "reveal": "never",
            "focus": False,
            "panel": "dedicated",
            "showReuseMessage": False,
        },
        "problemMatcher": [],
    }


def mark_linux_only(task: dict[str, Any]) -> dict[str, Any]:
    result = dict(task)
    label = str(result.get("label") or "Quality task")
    message = (
        f"{label} is Linux-only (P2); use the existing Ubuntu CI or a Linux shell."
    )
    result["windows"] = {
        "command": "powershell.exe",
        "args": [
            "-NoProfile",
            "-Command",
            f"Write-Error '{message}'; exit 8",
        ],
    }
    return result


def desired_project_files(
    module: str,
    registry: dict[str, dict[str, Any]],
) -> dict[Path, str]:
    vscode_dir = REPO_ROOT / "src" / module / ".vscode"
    manifest = load_json(vscode_dir / "jaszczurhal.project.json")
    settings = load_json(vscode_dir / "settings.json")
    current_tasks = load_json(vscode_dir / "tasks.json")

    from vscode_task_config import (
        keybindings_reference,
        project_tasks_document,
        vscode_entry_settings,
    )

    target = str(manifest["target"])
    board = str(manifest["board"])
    identity = (
        manifest.get("identity")
        if isinstance(manifest.get("identity"), dict)
        else {}
    )
    usb_product = str(identity.get("usbProduct") or "")
    desired_tasks = project_tasks_document(
        registry,
        target,
        board,
        module=module,
        usb_product=usb_product,
    )
    shared_labels = {
        str(task.get("label"))
        for task in desired_tasks["tasks"]
        if isinstance(task, dict)
    }
    custom_tasks: list[dict[str, Any]] = []
    for task in current_tasks.get("tasks", []):
        if not isinstance(task, dict):
            continue
        label = str(task.get("label") or "")
        if label in shared_labels:
            continue
        if label == HOOK_TASK_LABEL:
            custom_tasks.append(hook_task("${workspaceFolder}/../../scripts/configure_git_hooks.py"))
        elif label in LINUX_ONLY_TASK_LABELS:
            custom_tasks.append(mark_linux_only(task))
        else:
            custom_tasks.append(task)
    desired_tasks["tasks"].extend(custom_tasks)

    settings.pop("jaszczurhal.uploadPort", None)
    unix_entry = str(
        settings.get("jaszczurhal.vscodeEntry")
        or "../../../libraries/JaszczurHAL/vscode/entry/jh-vscode"
    )
    settings.update(vscode_entry_settings(unix_entry))

    return {
        vscode_dir / "settings.json": json_text(settings),
        vscode_dir / "tasks.json": json_text(desired_tasks, indent=2),
        vscode_dir / "keybindings.reference.json": json_text(keybindings_reference()),
    }


def desired_root_tasks() -> dict[str, Any]:
    return {
        "version": "2.0.0",
        "tasks": [hook_task("${workspaceFolder}/scripts/configure_git_hooks.py")],
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if generated files differ.")
    parser.add_argument(
        "--jaszczurhal-root",
        type=Path,
        default=Path(os.environ.get("JASZCZURHAL_ROOT", DEFAULT_JH_ROOT)),
        help="JaszczurHAL checkout used as the generator source.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    jh_root = args.jaszczurhal_root.resolve()
    scripts_dir = jh_root / "scripts"
    if not scripts_dir.is_dir():
        print(f"error: JaszczurHAL scripts not found: {scripts_dir}", file=sys.stderr)
        return 1
    sys.path.insert(0, str(scripts_dir))

    from board_registry import tooling_target_registry
    from vscode_task_config import write_text_lf

    registry = tooling_target_registry(jh_root)
    expected: dict[Path, str] = {}
    for module in MODULES:
        expected.update(desired_project_files(module, registry))
    expected[REPO_ROOT / ".vscode" / "tasks.json"] = json_text(desired_root_tasks())

    mismatches = [
        path
        for path, content in expected.items()
        if not path.is_file() or path.read_text(encoding="utf-8") != content
    ]
    if args.check:
        if mismatches:
            print("error: Fiesta VS Code files are out of date:", file=sys.stderr)
            for path in mismatches:
                print(f"  {path.relative_to(REPO_ROOT)}", file=sys.stderr)
            return 1
        print(f"Fiesta VS Code files are synchronized ({len(expected)} files).")
        return 0

    for path, content in expected.items():
        write_text_lf(path, content)
        print(f"generated {path.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
