#!/usr/bin/env python3
"""Synchronize Fiesta firmware VS Code files with JaszczurHAL generators."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any

from fiesta_modules import Module, load as load_module_registry
from fiesta_modules import apply_shared_sources


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_JH_ROOT = REPO_ROOT / "src" / "JaszczurHAL"
HOOK_TASK_LABEL = "Project: Configure Git hooks"
LINUX_ONLY_TASK_LABELS = {"Quality: Cppcheck baseline", "Quality: MISRA scan"}
# Everything the generated files are derived from, including the files
# themselves (custom tasks and settings survive a sync).
GENERATOR_INPUTS = ("modules.json", "src", ".vscode")
GITLINK_MODE = "160000"


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


def manifest_selection(module_dir: Path, manifest: dict[str, Any]) -> tuple[str, str, str | None]:
    """Target, board and assembly of a module: with device_tree.json the
    assembly the manifest selects (the only one when it selects none), else
    the manifest's target and board."""
    import device_tree_project

    if device_tree_project.device_tree_path(module_dir) is None:
        return str(manifest["target"]), str(manifest["board"]), None
    entry, _ = device_tree_project.choose(module_dir, None, manifest.get("assembly"))
    return entry["target"], entry["board"], entry["name"]


def desired_project_files(
    module: Module,
    registry: dict[str, dict[str, Any]],
    jh_root: Path,
    root: Path = REPO_ROOT,
) -> dict[Path, str]:
    module_dir = root / "src" / module.name
    vscode_dir = module_dir / ".vscode"
    manifest = load_json(vscode_dir / "jaszczurhal.project.json")
    settings = load_json(vscode_dir / "settings.json")
    current_tasks = load_json(vscode_dir / "tasks.json")

    from vscode_task_config import (
        apply_project_view_settings,
        assembly_debug_builds,
        assembly_launch_document,
        keybindings_reference,
        project_tasks_document,
        sync_cortex_debug_launch_document,
        vscode_entry_settings,
    )

    target, board, assembly = manifest_selection(module_dir, manifest)
    identity = (
        manifest.get("identity")
        if isinstance(manifest.get("identity"), dict)
        else {}
    )
    usb_product = str(identity.get("usbProduct") or "")
    # Variant tasks follow JH_PROJECT_VARIANTS in the module's hal_project_config.h.
    desired_tasks = project_tasks_document(
        registry,
        target,
        board,
        module=module.name,
        usb_product=usb_product,
        project_dir=module_dir,
        selected_assembly=assembly,
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
        or "../JaszczurHAL/vscode/entry/jh-vscode"
    )
    settings.update(vscode_entry_settings(unix_entry))
    # IntelliSense follows the firmware database of Project: Refresh
    # IntelliSense, never CMake Tools on the module's host-test CMakeLists.txt.
    apply_project_view_settings(
        settings,
        str(settings.get("C_Cpp.default.compileCommands")
            or "${workspaceFolder}/.build/compile_commands_patched.json"),
    )

    # The firmware compiles the shared source sets through JH_EXTRA_SOURCES;
    # every firmware manifest is managed, so a module dropped from
    # "sharedSources" loses the stale value on the next sync.
    apply_shared_sources(manifest, module, root)
    outputs = {vscode_dir / "jaszczurhal.project.json": json_text(manifest)}
    if assembly is not None:
        # One debug profile per assembly and variant, as sync-board-picker
        # writes it; profiles of the module's own stay.
        launch_path = vscode_dir / "launch.json"
        launch = load_json(launch_path) if launch_path.is_file() else {}
        sync_cortex_debug_launch_document(
            launch,
            assembly_launch_document(
                manifest,
                assembly_debug_builds(module_dir),
                workspace_dir=module_dir,
                jh_root=jh_root,
                default_assembly=assembly,
            ),
        )
        outputs[launch_path] = json_text(launch)

    return outputs | {
        vscode_dir / "settings.json": json_text(settings),
        vscode_dir / "tasks.json": json_text(desired_tasks, indent=2),
        vscode_dir / "keybindings.reference.json": json_text(keybindings_reference()),
    }


def desired_root_tasks() -> dict[str, Any]:
    return {
        "version": "2.0.0",
        "tasks": [hook_task("${workspaceFolder}/scripts/configure_git_hooks.py")],
    }


def desired_files(
    root: Path,
    registry: dict[str, dict[str, Any]],
    jh_root: Path,
) -> dict[Path, str]:
    """Every generated file below root, derived from the inputs below root."""
    expected: dict[Path, str] = {}
    for module in load_module_registry(root).firmware():
        expected.update(desired_project_files(module, registry, jh_root, root))
    expected[root / ".vscode" / "tasks.json"] = json_text(desired_root_tasks())
    return expected


def git(*args: str, data: bytes | None = None) -> subprocess.CompletedProcess:
    """Run git in the repository; GIT_INDEX_FILE of a running commit applies."""
    return subprocess.run(
        ["git", "-C", str(REPO_ROOT), *args],
        input=data,
        capture_output=True,
        check=False,
    )


def git_failed(result: subprocess.CompletedProcess, what: str) -> bool:
    if result.returncode == 0:
        return False
    detail = result.stderr.decode("utf-8", errors="replace").strip()
    print(f"error: {what}: {detail}", file=sys.stderr)
    return True


def read_optional(path: Path) -> str | None:
    return path.read_text(encoding="utf-8") if path.is_file() else None


def stage_from_index(registry: dict[str, dict[str, Any]], jh_location: Path) -> int:
    """Generate from the staged inputs and stage every output that differs.

    A commit then holds generated files that match the sources it commits,
    whatever else the working tree has not staged; nothing is added from the
    working tree.
    """
    listing = git("ls-files", "-s", "-z", "--", *GENERATOR_INPUTS)
    if git_failed(listing, "cannot list the staged generator inputs"):
        return 1
    modes: dict[str, str] = {}
    for record in listing.stdout.split(b"\0"):
        if record:
            meta, name = record.split(b"\t", 1)
            modes[name.decode("utf-8")] = meta.split()[0].decode("ascii")
    files = [name for name, mode in modes.items() if mode != GITLINK_MODE]

    with tempfile.TemporaryDirectory(prefix="fiesta-vscode-index-") as temporary:
        snapshot = Path(temporary)
        exported = git(
            "checkout-index",
            f"--prefix={snapshot.as_posix()}/",
            "-z",
            "--stdin",
            data=b"".join(name.encode("utf-8") + b"\0" for name in files),
        )
        if git_failed(exported, "cannot export the staged generator inputs"):
            return 1
        # Launch profiles hold the HAL path relative to each module, so the
        # snapshot names the HAL at the same place; nothing is read there.
        try:
            snapshot_jh_root = snapshot / jh_location.relative_to(REPO_ROOT)
        except ValueError:
            snapshot_jh_root = jh_location
        expected = desired_files(snapshot, registry, snapshot_jh_root)

        staged = 0
        for path, content in expected.items():
            relative = path.relative_to(snapshot).as_posix()
            indexed = read_optional(path)
            if indexed == content:
                continue
            blob = git(
                "hash-object", "-w", "--stdin", f"--path={relative}",
                data=content.encode("utf-8"),
            )
            if git_failed(blob, f"cannot store {relative}"):
                return 1
            sha = blob.stdout.decode("ascii").strip()
            mode = modes.get(relative, "100644")
            update = git(
                "update-index", "--add", "--cacheinfo", f"{mode},{sha},{relative}"
            )
            if git_failed(update, f"cannot stage {relative}"):
                return 1
            staged += 1
            print(f"staged {relative}")
    if staged == 0:
        print(f"Fiesta VS Code files match the staged sources ({len(expected)} files).")
    else:
        print(f"staged {staged} generated VS Code file(s) from the staged sources")
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="Fail if generated files differ.")
    mode.add_argument(
        "--stage",
        action="store_true",
        help="Also generate from the staged sources and stage the outputs that "
        "differ (the pre-commit hook); unstaged working-tree changes stay out.",
    )
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
    expected = desired_files(REPO_ROOT, registry, jh_root)

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

    # The working tree follows its own sources, staged or not.
    for path in mismatches:
        content = expected[path]
        write_text_lf(path, content)
        print(f"generated {path.relative_to(REPO_ROOT)}")
    if args.stage:
        return stage_from_index(registry, args.jaszczurhal_root.absolute())
    if not mismatches:
        print(f"Fiesta VS Code files are synchronized ({len(expected)} files).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
