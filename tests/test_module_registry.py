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
            "usbVid": "0x2e8a", "usbPid": "0x000a", "byIdHint": hint}


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


class RegistryRulesTests(unittest.TestCase):
    def assert_rejected(self, pattern: str, **fixture) -> None:
        repo = FixtureRepo(**fixture)
        self.addCleanup(repo.close)
        with self.assertRaisesRegex(RegistryError, pattern):
            repo.load()

    def test_valid_fixture_loads(self) -> None:
        repo = FixtureRepo()
        self.addCleanup(repo.close)
        registry = repo.load()
        self.assertEqual([m.name for m in registry.firmware()],
                         ["ECU", "Fiesta_clock", "Adjustometer"])
        self.assertEqual([m.name for m in registry.host_tests()],
                         ["ECU", "Adjustometer", "SerialConfigurator"])

    def test_rejects_wrong_schema(self) -> None:
        self.assert_rejected("schemaVersion", schema=2)

    def test_rejects_duplicate_names_tokens_and_macros(self) -> None:
        self.assert_rejected("duplicate token", modules=modified(1, token="ECU"))
        self.assert_rejected("duplicate macro", modules=modified(1, macro="ECU"))
        duplicate = copy.deepcopy(BASE_MODULES) + [copy.deepcopy(BASE_MODULES[3])]
        self.assert_rejected("duplicate module name", modules=duplicate)

    def test_rejects_missing_module_directory(self) -> None:
        repo = FixtureRepo()
        self.addCleanup(repo.close)
        shutil.rmtree(repo.root / "src" / "SerialConfigurator")
        with self.assertRaisesRegex(RegistryError, "not a module directory"):
            repo.load()

    def test_rejects_unknown_keys_and_kinds(self) -> None:
        self.assert_rejected("unknown keys", modules=modified(0, board="x"))
        self.assert_rejected("kind must be", modules=modified(3, kind="library"))
        self.assert_rejected("only firmware modules",
                             modules=modified(3, token="SC"))

    def test_serial_configurator_indices_must_be_contiguous(self) -> None:
        gap = modified(1, serialConfigurator={"index": 2, "displayName": "RTC_Clock"})
        self.assert_rejected("without gaps", modules=gap)
        twice = modified(1, serialConfigurator={"index": 0, "displayName": "RTC_Clock"})
        self.assert_rejected("without gaps", modules=twice)

    def test_analysis_needs_host_tests(self) -> None:
        self.assert_rejected("needs hostTests",
                             modules=modified(0, hostTests=None))
        self.assert_rejected("allowed values", modules=modified(0, analysis=["lint"]))

    def test_usb_identity_rules(self) -> None:
        for field, value, pattern in (
            ("usbVid", "0x1234", "VID/PID"),
            ("usbProduct", "ECU", "usbProduct"),
            ("byIdHint", "ECU", "byIdHint must start"),
        ):
            identities = copy.deepcopy(BASE_IDENTITIES)
            identities["ECU"][field] = value
            self.assert_rejected(pattern, identities=identities)

    def test_rejects_non_ascii_text(self) -> None:
        self.assert_rejected("ASCII", modules=modified(
            1, serialConfigurator={"index": 1, "displayName": "RTC\u2013Clock"}))

    def test_by_id_hints_must_not_match_each_other(self) -> None:
        identities = copy.deepcopy(BASE_IDENTITIES)
        identities["Fiesta_clock"]["byIdHint"] = "Fiesta_ECU_Clock"
        self.assert_rejected("also matches", identities=identities)
        identities["Fiesta_clock"]["byIdHint"] = "Fiesta_ECU"
        self.assert_rejected("duplicate byIdHint", identities=identities)


class GeneratedOutputTests(unittest.TestCase):
    def test_serial_configurator_table_follows_index_not_json_order(self) -> None:
        swapped = copy.deepcopy(BASE_MODULES)
        swapped[0]["serialConfigurator"]["index"] = 1
        swapped[1]["serialConfigurator"]["index"] = 0
        repo = FixtureRepo(modules=swapped)
        self.addCleanup(repo.close)
        registry = repo.load()
        table = fiesta_modules.sc_table_header(registry)
        self.assertLess(table.index("SC_MODULE_CLOCK,"), table.index("SC_MODULE_ECU,"))
        tokens = fiesta_modules.tokens_header(registry)
        self.assertLess(tokens.index('SC_MODULE_CLOCK "RTC_Clock"'),
                        tokens.index('SC_MODULE_ECU "ECU"'))

    def test_modules_outside_serial_configurator(self) -> None:
        repo = FixtureRepo()
        self.addCleanup(repo.close)
        registry = repo.load()
        tokens = fiesta_modules.tokens_header(registry)
        self.assertIn("#define SC_MODULE_COUNT 2u", tokens)
        self.assertIn('#define SC_MODULE_TOKEN_ADJUSTOMETER "ADJ"', tokens)
        self.assertNotIn("SC_MODULE_ADJUSTOMETER ", tokens)
        self.assertIn('{"Fiesta_Adjustometer", NULL}',
                      fiesta_modules.sc_table_header(registry))

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

    def test_tooling_tests_run_when_only_the_registry_changes(self) -> None:
        text = (WORKFLOWS / "ecu-tests.yml").read_text(encoding="utf-8")
        self.assertIn("unittest discover -s tests", text)
        # Both the push and the pull_request path filters.
        self.assertEqual(text.count('- "modules.json"'), 2)

    def test_windows_workflow_matches_registry(self) -> None:
        text = (WORKFLOWS / "windows-firmware.yml").read_text(encoding="utf-8")
        listed = re.search(r"\$modules = @\(([^)]*)\)", text)
        self.assertIsNotNone(listed)
        self.assertEqual(set(re.findall(r"'([^']+)'", listed.group(1))),
                         self.ci_modules(firmware_only=True))


if __name__ == "__main__":
    unittest.main()
