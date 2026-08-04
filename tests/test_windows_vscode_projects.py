#!/usr/bin/env python3
"""Validate the generated cross-platform firmware VS Code projects."""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULES = ("ECU", "Clocks", "OilAndSpeed", "Adjustometer", "Fiesta_clock")
LINUX_ONLY_TASKS = {"Quality: Cppcheck baseline", "Quality: MISRA scan"}


class WindowsVscodeProjectTests(unittest.TestCase):
    def test_generated_files_are_current(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "scripts" / "sync_vscode_projects.py"),
                "--check",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_module_tasks_are_unique_and_have_windows_commands(self) -> None:
        for module in MODULES:
            vscode_dir = REPO_ROOT / "src" / module / ".vscode"
            document = json.loads((vscode_dir / "tasks.json").read_text(encoding="utf-8"))
            tasks = document["tasks"]
            labels = [task["label"] for task in tasks]
            self.assertEqual(len(labels), len(set(labels)), module)
            for task in tasks:
                self.assertIn("windows", task, f"{module}: {task['label']}")
                self.assertTrue(
                    task["windows"].get("command"),
                    f"{module}: {task['label']}",
                )

    def test_settings_keep_port_local_and_use_windows_entry(self) -> None:
        for module in MODULES:
            vscode_dir = REPO_ROOT / "src" / module / ".vscode"
            settings = json.loads((vscode_dir / "settings.json").read_text(encoding="utf-8"))
            self.assertNotIn("jaszczurhal.uploadPort", settings, module)
            self.assertEqual(
                settings["jaszczurhal.vscodeEntryWindows"],
                "../../../libraries/JaszczurHAL/vscode/entry/jh-vscode.cmd",
                module,
            )
            manifest = json.loads(
                (vscode_dir / "jaszczurhal.project.json").read_text(encoding="utf-8")
            )
            self.assertTrue(manifest["identity"]["enabled"], module)
            self.assertEqual(manifest["identity"]["usbVid"], "0x2e8a", module)
            self.assertEqual(manifest["identity"]["usbPid"], "0x000a", module)

    def test_linux_only_quality_tasks_fail_explicitly_on_windows(self) -> None:
        found: set[str] = set()
        for module in MODULES:
            tasks = json.loads(
                (REPO_ROOT / "src" / module / ".vscode" / "tasks.json").read_text(
                    encoding="utf-8"
                )
            )["tasks"]
            for task in tasks:
                if task["label"] not in LINUX_ONLY_TASKS:
                    continue
                found.add(task["label"])
                windows = task["windows"]
                self.assertEqual(windows["command"], "powershell.exe")
                self.assertIn("Linux-only", " ".join(windows["args"]))
                self.assertIn("exit 8", " ".join(windows["args"]))
        self.assertEqual(found, LINUX_ONLY_TASKS)

    def test_generated_json_has_no_unix_machine_paths(self) -> None:
        paths = [REPO_ROOT / ".vscode" / "tasks.json"]
        for module in MODULES:
            paths.extend((REPO_ROOT / "src" / module / ".vscode").glob("*.json"))
        for path in paths:
            content = path.read_text(encoding="utf-8")
            for forbidden in ("/dev/", "/home/", ".arduino15"):
                self.assertNotIn(forbidden, content, str(path.relative_to(REPO_ROOT)))

    def test_launch_profiles_need_no_private_cortex_debug_settings(self) -> None:
        for module in MODULES:
            launch = (
                REPO_ROOT / "src" / module / ".vscode" / "launch.json"
            ).read_text(encoding="utf-8")
            self.assertNotIn("${config:cortex-debug.", launch, module)


if __name__ == "__main__":
    unittest.main()
