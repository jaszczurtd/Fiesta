#!/usr/bin/env python3
"""Check the module registry, its generated files and the lists that use it."""

from __future__ import annotations

import copy
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import fiesta_modules  # noqa: E402
from fiesta_modules import RegistryError  # noqa: E402

WORKFLOWS = REPO_ROOT / ".github" / "workflows"


def identity(product: str, hint: str) -> dict:
    return {"enabled": True, "usbManufacturer": "Jaszczur", "usbProduct": product,
            "byIdHint": hint}


BASE_MODULES = [
    {"name": "ECU", "kind": "firmware", "token": "ECU", "macro": "ECU",
     "serialConfigurator": {"index": 0, "displayName": "ECU", "tests": True},
     "hostTests": "build_test", "analysis": ["cppcheck"]},
    {"name": "Fiesta_clock", "kind": "firmware", "token": "RTC_CLK", "macro": "CLOCK",
     "serialConfigurator": {"index": 1, "displayName": "RTC_Clock"}},
    {"name": "Adjustometer", "kind": "firmware", "token": "ADJ", "macro": "ADJUSTOMETER",
     "hostTests": "build_test"},
    {"name": "SerialConfigurator", "kind": "host-app", "hostTests": "build"},
]
BASE_IDENTITIES = {
    "ECU": identity("Fiesta ECU", "Fiesta_ECU"),
    "Fiesta_clock": identity("Fiesta RTC Clock", "Fiesta_RTC_Clock"),
    "Adjustometer": identity("Fiesta Adjustometer", "Fiesta_Adjustometer"),
}


class FixtureRepo:
    """A throwaway repository with a registry and module manifests."""

    def __init__(self, modules=None, identities=None, schema=1):
        self._temporary = tempfile.TemporaryDirectory(prefix="fiesta-registry-")
        self.root = Path(self._temporary.name)
        modules = copy.deepcopy(BASE_MODULES if modules is None else modules)
        identities = copy.deepcopy(BASE_IDENTITIES if identities is None else identities)
        for module in modules:
            (self.root / "src" / module["name"]).mkdir(parents=True, exist_ok=True)
        for name, ident in identities.items():
            vscode = self.root / "src" / name / ".vscode"
            vscode.mkdir(parents=True, exist_ok=True)
            (vscode / "jaszczurhal.project.json").write_text(
                json.dumps({"identity": ident}), encoding="utf-8")
        (self.root / "modules.json").write_text(
            json.dumps({"schemaVersion": schema, "modules": modules}), encoding="utf-8")

    def load(self) -> fiesta_modules.Registry:
        return fiesta_modules.load(self.root)

    def close(self) -> None:
        self._temporary.cleanup()


def modified(index: int, **changes) -> list:
    modules = copy.deepcopy(BASE_MODULES)
    for key, value in changes.items():
        if value is None:
            modules[index].pop(key, None)
        else:
            modules[index][key] = value
    return modules


class NativeRegistryTests(unittest.TestCase):
    def test_native_registry_cases(self) -> None:
        for path in (REPO_ROOT / "runalltests.sh", WORKFLOWS / "ecu-tests.yml"):
            self.assertIn("cmake -DFIESTA_TOOL_MEMCHECK=ON -P scripts/fiesta_tool.cmake",
                          path.read_text(encoding="utf-8"))
        result = subprocess.run(
            ["cmake", "-DFIESTA_TOOL_TESTS=ON", "-P",
             str(REPO_ROOT / "scripts/fiesta_tool.cmake")],
            capture_output=True, text=True, check=False, timeout=180)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class GeneratedOutputTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("bash"), "the shell fragment is Bash")
    def test_shell_fragment_as_seen_by_bash(self) -> None:
        repo = FixtureRepo(modules=modified(2, token="A&D"))
        self.addCleanup(repo.close)
        fragment = repo.root / "fragment.sh"
        fragment.write_text(fiesta_modules.shell_fragment(repo.load()), encoding="utf-8")
        script = (
            f'source "{fragment}"\n'
            'printf "%s\\n" "${FIESTA_FIRMWARE_MODULES[*]}"\n'
            'printf "%s\\n" "${FIESTA_HOST_TEST_MODULES[*]}"\n'
            'printf "%s\\n" "${FIESTA_CPPCHECK_MODULES[*]}"\n'
            'fiesta_module_token_for Adjustometer\n'
            'fiesta_module_token_for SerialConfigurator || echo no-token\n'
        )
        result = subprocess.run(["bash", "-c", script], capture_output=True, text=True,
                                check=True)
        self.assertEqual(result.stdout.splitlines(), [
            "ECU Fiesta_clock Adjustometer",
            "ECU:src/ECU:src/ECU/build_test "
            "Adjustometer:src/Adjustometer:src/Adjustometer/build_test "
            "SerialConfigurator:src/SerialConfigurator:src/SerialConfigurator/build",
            "ECU",
            "A&D",
            "no-token",
        ])

    def test_check_reports_stale_files(self) -> None:
        repo = FixtureRepo()
        self.addCleanup(repo.close)
        for relative in fiesta_modules.outputs(repo.load()):
            (repo.root / relative).parent.mkdir(parents=True, exist_ok=True)
        script = REPO_ROOT / "scripts" / "fiesta_modules.py"
        args = [sys.executable, str(script), "--repo-root", str(repo.root)]
        self.assertEqual(subprocess.run(args + ["--check"], capture_output=True).returncode, 1)
        self.assertEqual(subprocess.run(args, capture_output=True).returncode, 0)
        self.assertEqual(subprocess.run(args + ["--check"], capture_output=True).returncode, 0)


class RepositoryRegistryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.registry = fiesta_modules.load()

    def test_generated_files_are_current(self) -> None:
        self.assertEqual(fiesta_modules.stale_outputs(self.registry), [])

    @unittest.skipUnless(shutil.which("clang-format"), "clang-format is not installed")
    def test_pre_commit_formatter_leaves_generated_headers_alone(self) -> None:
        # The pre-commit hook runs clang-format on staged C headers; a changed
        # header would fail the check above right after the commit.
        for relative in (fiesta_modules.TOKENS_HEADER, fiesta_modules.SC_TABLE_HEADER):
            path = REPO_ROOT / relative
            formatted = subprocess.run(["clang-format", str(path)], check=True,
                                       capture_output=True, text=True).stdout
            self.assertEqual(formatted, path.read_text(encoding="utf-8"), str(relative))

    def test_generated_files_are_ascii(self) -> None:
        for relative, content in fiesta_modules.outputs(self.registry).items():
            self.assertTrue(content.isascii(), str(relative))

    def ci_modules(self, firmware_only: bool) -> set[str]:
        return {m.name for m in self.registry.ci() if m.is_firmware or not firmware_only}

    def test_firmware_build_workflow_matches_registry(self) -> None:
        text = (WORKFLOWS / "firmware-build-scripts.yml").read_text(encoding="utf-8")
        matrix = re.search(r"\n(\s+)module:\n((?:\1  - .+\n)+)", text)
        self.assertIsNotNone(matrix)
        modules = set(re.findall(r"- (\S+)", matrix.group(2)))
        self.assertEqual(modules, self.ci_modules(firmware_only=True))
        roots = set(re.findall(r"--input-root src/(\S+)", text))
        self.assertEqual(roots, self.ci_modules(firmware_only=False) | {"common"})

    def test_tooling_tests_run_when_registry_or_read_modules_change(self) -> None:
        text = (WORKFLOWS / "ecu-tests.yml").read_text(encoding="utf-8")
        self.assertIn("unittest discover -s tests", text)
        # Both the push and the pull_request path filters.
        self.assertEqual(text.count('- "modules.json"'), 2)
        # Every module whose files the Python tests read or compile.
        suite = "".join(path.read_text(encoding="utf-8")
                        for path in sorted((REPO_ROOT / "tests").glob("*.py")))
        read = {m.name for m in self.registry.modules
                if (REPO_ROOT / "src" / m.name).is_dir()
                and re.search(rf'src/{re.escape(m.name)}\b', suite)}
        self.assertTrue({"ECU", "Clocks", "Fiesta_clock"} <= read, read)
        for name in sorted(read):
            self.assertEqual(text.count(f'- "src/{name}/**"'), 2, name)

    def test_windows_workflow_matches_registry(self) -> None:
        text = (WORKFLOWS / "windows-firmware.yml").read_text(encoding="utf-8")
        listed = re.search(r"\$modules = @\(([^)]*)\)", text)
        self.assertIsNotNone(listed)
        self.assertEqual(set(re.findall(r"'([^']+)'", listed.group(1))),
                         self.ci_modules(firmware_only=True))

    def test_linux_workflows_fetch_hal_components_before_building(self) -> None:
        # A fresh CI checkout has no JaszczurHAL third_party components (jhbase
        # and the rest); a developer tree usually has them, so only this check
        # catches a workflow that configures against HAL without fetching them.
        preparation = (REPO_ROOT / "src/common/scripts/ensure-jaszczurhal-dependencies.sh").read_text(
            encoding="utf-8")
        self.assertIn('"$JASZCZURHAL_DIR/scripts/jh" --version', preparation)
        build = re.compile(r"cmake -S src/|jh-vscode (?:build|clean)|generate_hal_features\.py")
        for path in sorted(WORKFLOWS.glob("*.yml")):
            text = path.read_text(encoding="utf-8")
            first_build = build.search(text)
            if first_build is None:
                continue
            with self.subTest(workflow=path.name):
                fetch = text.find("ensure-jaszczurhal-dependencies.sh")
                self.assertNotEqual(-1, fetch)
                self.assertLess(fetch, first_build.start())
                if "cmake -S src/" in first_build.group():
                    self.assertIn("ensure-jaszczurhal-dependencies.sh src/JaszczurHAL host",
                                  text[:first_build.start()])


if __name__ == "__main__":
    unittest.main()
