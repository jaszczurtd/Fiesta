#!/usr/bin/env python3
"""Validate the generated cross-platform firmware VS Code projects."""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import fiesta_modules  # noqa: E402

MODULES = tuple(module.name for module in fiesta_modules.load().firmware())
LINUX_ONLY_TASKS = {"Quality: Cppcheck baseline", "Quality: MISRA scan"}
# Every module builds from the RP assemblies of its device_tree.json (ECU:
# RP2350-Plus and RP2040-Plus on the same PCB); the probe gets one profile per
# assembly and variant, each for its own ELF and the OpenOCD target of its MCU.
DEBUG_PROFILES = {
    "ECU": {
        "Project: Debug rp2350-plus (RP2350 ARM)":
            ("rp2350-arm/rp2350-plus", "Project: Build (Debug) rp2350-plus",
             "target/rp2350.cfg"),
        "Project: Debug rp2350-plus variant: BENCH (RP2350 ARM)":
            ("rp2350-arm/rp2350-plus/variants/BENCH",
             "Project: Build (Debug) rp2350-plus variant: BENCH", "target/rp2350.cfg"),
        "Project: Debug rp2040-plus (RP2040)":
            ("rp2040/rp2040-plus", "Project: Build (Debug) rp2040-plus",
             "target/rp2040.cfg"),
        "Project: Debug rp2040-plus variant: BENCH (RP2040)":
            ("rp2040/rp2040-plus/variants/BENCH",
             "Project: Build (Debug) rp2040-plus variant: BENCH", "target/rp2040.cfg"),
    },
    "Clocks": {
        "Project: Debug rp2040-zero (RP2040)":
            ("rp2040/rp2040-zero", "Project: Build (Debug) rp2040-zero",
             "target/rp2040.cfg"),
        "Project: Debug rp2040-zero variant: TIMING (RP2040)":
            ("rp2040/rp2040-zero/variants/TIMING",
             "Project: Build (Debug) rp2040-zero variant: TIMING",
             "target/rp2040.cfg"),
    },
    "Fiesta_clock": {
        "Project: Debug rp2040-zero (RP2040)":
            ("rp2040/rp2040-zero", "Project: Build (Debug) rp2040-zero",
             "target/rp2040.cfg"),
        "Project: Debug rp2040-zero variant: DS3231 (RP2040)":
            ("rp2040/rp2040-zero/variants/DS3231",
             "Project: Build (Debug) rp2040-zero variant: DS3231",
             "target/rp2040.cfg"),
    },
    "Adjustometer": {"Project: Debug rp2040-zero (RP2040)":
                     ("rp2040/rp2040-zero", "Project: Build (Debug) rp2040-zero",
                      "target/rp2040.cfg")},
    "VP37TestBench": {"Project: Debug pico (RP2040)":
                      ("rp2040/pico", "Project: Build (Debug) pico", "target/rp2040.cfg")},
}


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

    def test_pre_commit_regenerates_and_stages_generated_files(self) -> None:
        hook = (REPO_ROOT / ".githooks" / "pre-commit").read_text(encoding="utf-8")
        self.assertIn("sync_vscode_projects.py --stage", hook)
        self.assertLess(
            hook.index("sync_vscode_projects.py --stage"),
            hook.index("git diff --cached"),
        )

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
                "../JaszczurHAL/vscode/entry/jh-vscode.cmd",
                module,
            )
            manifest = json.loads(
                (vscode_dir / "jaszczurhal.project.json").read_text(encoding="utf-8")
            )
            self.assertTrue(manifest["identity"]["enabled"], module)
            self.assertNotIn("usbVid", manifest["identity"], module)
            self.assertNotIn("usbPid", manifest["identity"], module)

    def test_module_paths_resolve_to_the_pinned_checkout(self) -> None:
        hal_root = (REPO_ROOT / "src" / "JaszczurHAL").resolve()
        for module in MODULES:
            project = REPO_ROOT / "src" / module
            vscode_dir = project / ".vscode"
            settings = json.loads((vscode_dir / "settings.json").read_text())
            manifest = json.loads((vscode_dir / "jaszczurhal.project.json").read_text())
            self.assertEqual((project / settings["jaszczurhal.root"]).resolve(), hal_root)
            for key in ("jaszczurhal.vscodeEntry", "jaszczurhal.vscodeEntryWindows"):
                entry = (project / settings[key]).resolve()
                self.assertTrue(entry.is_file(), entry)
                self.assertTrue(entry.is_relative_to(hal_root), entry)
            schema = (vscode_dir / manifest["$schema"]).resolve()
            self.assertTrue(schema.is_file(), schema)
            source = Path(manifest["cmake"]["sourceDir"].replace("${project}", str(project)))
            self.assertEqual(source.resolve(), hal_root / "cmake" / "jh_firmware_project")

    def test_ecu_usb_identity_matches_each_mcu(self) -> None:
        entry = REPO_ROOT / "src/JaszczurHAL/vscode/entry/jh_vscode.py"
        for assembly, pid in (("rp2040-plus", 0x000A),
                              ("rp2350-plus", 0x0009)):
            with self.subTest(assembly=assembly):
                result = subprocess.run(
                    [sys.executable, str(entry), "config-dump", "--project",
                     str(REPO_ROOT / "src/ECU"), "--assembly", assembly, "--json"],
                    capture_output=True, text=True, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                identity = json.loads(result.stdout)["identity"]
                self.assertEqual(identity["usbVid"], 0x2E8A)
                self.assertEqual(identity["usbPid"], pid)
                self.assertEqual(identity["usbProduct"], "Fiesta ECU")

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
            paths.extend(
                path
                for path in (REPO_ROOT / "src" / module / ".vscode").glob("*.json")
                # The ignored local file holds this machine's port by design.
                if path.name not in ("c_cpp_properties.json", "jaszczurhal.local.json")
            )
        for path in paths:
            content = path.read_text(encoding="utf-8")
            for forbidden in ("/dev/", "/home/", ".arduino15"):
                self.assertNotIn(forbidden, content, str(path.relative_to(REPO_ROOT)))

    def test_launch_profiles_need_no_private_cortex_debug_settings(self) -> None:
        for module in MODULES:
            launch_path = REPO_ROOT / "src" / module / ".vscode" / "launch.json"
            launch = launch_path.read_text(encoding="utf-8")
            self.assertNotIn("${config:cortex-debug.", launch, module)
            document = json.loads(launch)
            profiles = {
                profile["name"]: profile
                for profile in document["configurations"]
            }
            self.assertEqual(set(profiles), set(DEBUG_PROFILES[module]), module)
            for name, (build, task, target) in DEBUG_PROFILES[module].items():
                profile = profiles[name]
                self.assertEqual("${workspaceFolder}/.build/" + build + "/firmware.elf",
                                 profile["executable"], name)
                self.assertEqual(task, profile["preLaunchTask"], name)
                self.assertEqual(["interface/cmsis-dap.cfg", target],
                                 profile["configFiles"], name)


if __name__ == "__main__":
    unittest.main()
