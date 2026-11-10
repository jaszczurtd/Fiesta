#!/usr/bin/env python3
"""Validate the generated cross-platform firmware VS Code projects."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
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



class StageFromIndexTests(unittest.TestCase):
    """The pre-commit mode commits generated files that match the staged
    sources and never brings in unstaged working-tree changes (a PCB-only
    commit once took a manifest generated from an unstaged modules.json)."""

    MANIFEST = "src/Clocks/.vscode/jaszczurhal.project.json"
    UNRELATED = "src/common/scDefinitions/PROTOCOL.md"

    @classmethod
    def setUpClass(cls) -> None:
        # The fixture is a repository of the tracked generator inputs.
        listing = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "ls-files", "-s", "-z", "--",
             ".githooks", "scripts", "modules.json", "src", ".vscode"],
            capture_output=True, check=False,
        )
        if listing.returncode != 0:
            raise unittest.SkipTest("needs a git checkout of Fiesta")
        cls._temporary = tempfile.TemporaryDirectory(prefix="fiesta-stage-")
        cls.root = Path(cls._temporary.name) / "Fiesta"
        for record in listing.stdout.split(b"\0"):
            if not record:
                continue
            meta, name = record.split(b"\t", 1)
            relative = name.decode("utf-8")
            if meta.split()[0] == b"160000" or relative.startswith("src/JaszczurHAL/"):
                continue  # the HAL submodule is linked below
            target = cls.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(REPO_ROOT / relative, target)
        try:
            (cls.root / "src" / "JaszczurHAL").symlink_to(
                REPO_ROOT / "src" / "JaszczurHAL", target_is_directory=True)
        except OSError as error:
            cls._temporary.cleanup()
            raise unittest.SkipTest(f"directory symlinks unavailable: {error}")
        cls.git("init", "-q")
        cls.git("add", "-A")
        cls.commit("base")
        cls.base = cls.git("rev-parse", "HEAD").strip()

    @classmethod
    def tearDownClass(cls) -> None:
        cls._temporary.cleanup()

    def setUp(self) -> None:
        self.git("reset", "-q", "--hard", self.base)

    @classmethod
    def commit(cls, message: str) -> None:
        cls.git("-c", "user.name=test", "-c", "user.email=test@example.invalid",
                "commit", "-q", "--no-verify", "-m", message)

    @classmethod
    def git(cls, *args: str) -> str:
        return subprocess.run(
            ["git", "-C", str(cls.root), *args],
            capture_output=True, text=True, check=True,
        ).stdout

    def sync(self, *args: str) -> None:
        result = subprocess.run(
            [sys.executable, str(self.root / "scripts" / "sync_vscode_projects.py"),
             *args],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def staged(self) -> set[str]:
        return set(self.git("diff", "--cached", "--name-only").split())

    def toggle_clocks_shared_sources(self) -> bool:
        """Flip the test runner of Clocks in the working modules.json; return
        whether Clocks now compiles it."""
        path = self.root / "modules.json"
        registry = json.loads(path.read_text(encoding="utf-8"))
        clocks = next(m for m in registry["modules"] if m["name"] == "Clocks")
        if clocks.get("sharedSources"):
            clocks.pop("sharedSources")
        else:
            clocks["sharedSources"] = ["test_runner"]
        path.write_text(json.dumps(registry, indent=4) + "\n", encoding="utf-8")
        return bool(clocks.get("sharedSources"))

    def test_a_consistent_index_stages_nothing(self) -> None:
        self.sync("--stage")
        self.assertEqual(set(), self.staged())

    def test_unstaged_sources_and_outputs_stay_out_of_the_commit(self) -> None:
        self.toggle_clocks_shared_sources()
        self.sync()  # the working-tree manifest follows the unstaged registry
        working = (self.root / self.MANIFEST).read_text(encoding="utf-8")
        self.assertNotEqual(self.git("show", f"HEAD:{self.MANIFEST}"), working)
        with (self.root / self.UNRELATED).open("a", encoding="utf-8") as stream:
            stream.write("\nunrelated staged change\n")
        self.git("add", "--", self.UNRELATED)

        self.sync("--stage")

        self.assertEqual({self.UNRELATED}, self.staged())
        self.assertEqual(self.git("show", f"HEAD:{self.MANIFEST}"),
                         self.git("show", f":{self.MANIFEST}"))
        self.assertEqual(working,
                         (self.root / self.MANIFEST).read_text(encoding="utf-8"))

    def test_staged_sources_bring_their_outputs(self) -> None:
        compiles_runner = self.toggle_clocks_shared_sources()
        self.git("add", "--", "modules.json")

        self.sync("--stage")

        self.assertEqual({"modules.json", self.MANIFEST}, self.staged())
        staged = json.loads(self.git("show", f":{self.MANIFEST}"))
        self.assertEqual(compiles_runner,
                         "JH_EXTRA_SOURCES" in staged["cmake"]["cache"])
        self.assertEqual(self.git("show", f":{self.MANIFEST}"),
                         (self.root / self.MANIFEST).read_text(encoding="utf-8"))

    def test_the_hook_commits_outputs_of_the_staged_sources(self) -> None:
        if shutil.which("sh") is None or shutil.which("perl") is None:
            self.skipTest("the hook needs sh and perl")
        # A HEAD whose manifest disagrees with its registry, as after a
        # commit that took the manifest without modules.json; the working
        # tree keeps both changes, consistent with each other.
        compiles_runner = self.toggle_clocks_shared_sources()
        self.sync()
        working = (self.root / self.MANIFEST).read_text(encoding="utf-8")
        self.git("add", "--", self.MANIFEST)
        self.commit("manifest without its registry")
        with (self.root / self.UNRELATED).open("a", encoding="utf-8") as stream:
            stream.write("\nunrelated staged change\n")
        self.git("add", "--", self.UNRELATED)

        hook = subprocess.run(["sh", ".githooks/pre-commit"], cwd=self.root,
                              capture_output=True, text=True, check=False)

        self.assertEqual(hook.returncode, 0, hook.stdout + hook.stderr)
        staged = json.loads(self.git("show", f":{self.MANIFEST}"))
        self.assertEqual(not compiles_runner,
                         "JH_EXTRA_SOURCES" in staged["cmake"]["cache"])
        self.assertEqual({self.UNRELATED, self.MANIFEST}, self.staged())
        self.assertEqual(working,
                         (self.root / self.MANIFEST).read_text(encoding="utf-8"))

if __name__ == "__main__":
    unittest.main()
