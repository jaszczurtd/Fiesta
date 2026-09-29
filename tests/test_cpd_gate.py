#!/usr/bin/env python3
"""Check the scans of the Fiesta duplicate gate and how it blocks."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import fiesta_cpd  # noqa: E402

HAL_CPD = fiesta_cpd.load_hal_cpd(fiesta_cpd.DEFAULT_HAL_ROOT)

def long_function(name: str) -> str:
    """A C function long enough to cross the 100-token threshold."""
    body = "".join(f"  if (a > {i}) {{ b = b * {i} + a - {i}; a = a - b / {i + 1}; }}\n"
                   for i in range(1, 9))
    return f"int {name}(int a, int b) {{\n{body}  return a + b;\n}}\n"


def git(repo: Path, *args: str) -> None:
    subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True)


class FixtureRepo:
    def __init__(self) -> None:
        self._temporary = tempfile.TemporaryDirectory(prefix="fiesta-cpd-")
        self.root = Path(self._temporary.name)
        git(self.root, "init", "-q")

    def write(self, relative: str, text: str = "int f(void) { return 0; }\n") -> None:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def scopes(self, modules=("A", "B")) -> dict[str, fiesta_cpd.Scope]:
        return {scope.label: scope
                for scope in fiesta_cpd.build_scopes(self.root, list(modules), HAL_CPD)}

    def relative(self, scope: fiesta_cpd.Scope) -> list[str]:
        return [path.relative_to(self.root).as_posix() for path in scope.sources]

    def close(self) -> None:
        self._temporary.cleanup()


class ScopeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repo = FixtureRepo()
        self.addCleanup(self.repo.close)

    def test_production_per_module_tests_together(self) -> None:
        repo = self.repo
        for relative in ("src/A/a.c", "src/A/tests/test_a.cpp", "src/B/b.cpp",
                         "src/common/root.c", "src/common/x/c.c",
                         "src/common/tests/helper.c", "scripts/tool.py",
                         "src/ECU/scripts/systemd/send.py", "legacy/old.cpp",
                         "src/A/build/CMakeFiles/CMakeCCompilerId.c", "src/B/gone.c"):
            repo.write(relative)
        repo.write(".gitignore", "build/\n")
        git(repo.root, "add", "-A")
        (repo.root / "src/B/gone.c").unlink()
        repo.write("src/B/untracked.c")
        scopes = repo.scopes()
        self.assertEqual(
            {label: repo.relative(scope) for label, scope in scopes.items()},
            {
                "production_A": ["src/A/a.c"],
                "production_B": ["src/B/b.cpp", "src/B/untracked.c"],
                "production_common": ["src/common/root.c"],
                "production_common_x": ["src/common/x/c.c"],
                "tests": ["src/A/tests/test_a.cpp", "src/common/tests/helper.c"],
                "python": ["scripts/tool.py", "src/ECU/scripts/systemd/send.py"],
            })
        self.assertEqual(scopes["production_A"].minimum_tokens, HAL_CPD.MINIMUM_TOKENS)
        self.assertEqual(scopes["tests"].language, "cpp")
        self.assertEqual((scopes["python"].language, scopes["python"].minimum_tokens),
                         ("python", HAL_CPD.SCRIPTS_PYTHON_MINIMUM_TOKENS))

    def test_source_directory_outside_the_registry_is_an_error(self) -> None:
        self.repo.write("src/Unknown/u.c")
        with self.assertRaisesRegex(HAL_CPD.CpdError, "not a module in modules.json"):
            self.repo.scopes()

    def test_repository_scans(self) -> None:
        scopes = {scope.label: scope for scope in fiesta_cpd.build_scopes(
            REPO_ROOT, [m.name for m in fiesta_cpd.load_module_registry().modules],
            HAL_CPD)}
        for label in ("production_ECU", "production_SerialConfigurator", "tests",
                      "python"):
            self.assertIn(label, scopes)
        for scope in scopes.values():
            for path in scope.sources:
                self.assertFalse(path.is_relative_to(fiesta_cpd.DEFAULT_HAL_ROOT), path)


class GateTests(unittest.TestCase):
    def run_main(self, groups: list) -> tuple[int, list]:
        report = HAL_CPD.CpdReport(groups, {})
        with tempfile.TemporaryDirectory(prefix="fiesta-cpd-out-") as output, \
                mock.patch.object(fiesta_cpd, "load_hal_cpd", return_value=HAL_CPD), \
                mock.patch.object(HAL_CPD, "resolve_pmd", return_value=Path("pmd")), \
                mock.patch.object(HAL_CPD, "run_scan", return_value=report) as run_scan, \
                mock.patch("sys.stdout"), mock.patch("sys.stderr"):
            result = fiesta_cpd.main(["--output-dir", output])
        return result, run_scan.call_args_list

    def test_any_group_blocks_and_none_passes(self) -> None:
        group = HAL_CPD.Duplication(120, (HAL_CPD.Occurrence("src/ECU/a.c", 1, 0, 119),
                                          HAL_CPD.Occurrence("src/ECU/b.c", 1, 0, 119)))
        self.assertEqual(self.run_main([group])[0], 1)
        result, calls = self.run_main([])
        self.assertEqual(result, 0)
        labels = [call.args[3] for call in calls]
        self.assertIn("production_ECU", labels)
        self.assertIn("tests", labels)

    def test_no_baseline_or_exception_list(self) -> None:
        with mock.patch("sys.stdout") as stdout, self.assertRaises(SystemExit):
            fiesta_cpd.main(["--help"])
        text = "".join(call.args[0] for call in stdout.write.call_args_list)
        for word in ("baseline", "exclude", "allow", "ignore", "minimum"):
            self.assertNotIn(word, text.lower())


def pmd_available() -> bool:
    try:
        HAL_CPD.resolve_pmd(fiesta_cpd.DEFAULT_HAL_ROOT)
    except HAL_CPD.CpdError:
        return False
    return shutil.which("java") is not None


@unittest.skipUnless(pmd_available(), "pinned PMD or Java is not installed")
class PmdTests(unittest.TestCase):
    def test_modules_are_not_compared_but_tests_are(self) -> None:
        repo = FixtureRepo()
        self.addCleanup(repo.close)
        repo.write("src/A/a.c", long_function("a"))
        repo.write("src/B/b.c", long_function("b"))
        repo.write("src/A/tests/test_a.c", long_function("ta"))
        repo.write("src/B/tests/test_b.c", long_function("tb"))
        repo.write("src/B/b2.c", long_function("b2"))
        pmd = HAL_CPD.resolve_pmd(fiesta_cpd.DEFAULT_HAL_ROOT)
        output = repo.root / ".build"
        output.mkdir()
        groups = {}
        for scope in repo.scopes().values():
            report = HAL_CPD.run_scan(pmd, repo.root, output, scope.label, scope.language,
                                      list(scope.sources), scope.minimum_tokens)
            groups[scope.label] = [sorted(o.path for o in g.occurrences)
                                   for g in report.groups]
        self.assertEqual(groups["production_A"], [])
        self.assertEqual(groups["production_B"], [["src/B/b.c", "src/B/b2.c"]])
        self.assertEqual(groups["tests"], [["src/A/tests/test_a.c",
                                            "src/B/tests/test_b.c"]])


if __name__ == "__main__":
    unittest.main()
