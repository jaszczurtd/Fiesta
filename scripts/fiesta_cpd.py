#!/usr/bin/env python3
"""Run the PMD CPD duplicate gate over Fiesta sources.

Production code is scanned per module: every module in modules.json and every
component directory under src/common gets its own scan, so modules are never
compared with each other. Host tests are scanned together, across modules.
Python tooling is one more scan. PMD, the thresholds and the report parser come
from the pinned JaszczurHAL submodule (scripts/run_cpd.py).
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import importlib.util
from pathlib import Path, PurePosixPath
import subprocess
import sys

from fiesta_modules import load as load_module_registry


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HAL_ROOT = REPO_ROOT / "src" / "JaszczurHAL"
COMMON_DIR = "common"
TESTS_DIR = "tests"


def load_hal_cpd(hal_root: Path):
    """Import run_cpd.py from the JaszczurHAL checkout."""
    path = hal_root / "scripts" / "run_cpd.py"
    spec = importlib.util.spec_from_file_location("jh_run_cpd", path)
    if spec is None or spec.loader is None or not path.is_file():
        raise FileNotFoundError(f"JaszczurHAL CPD runner not found: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


@dataclass(frozen=True)
class Scope:
    label: str
    language: str
    minimum_tokens: int
    sources: tuple[Path, ...]


def owned_files(repo_root: Path) -> list[str]:
    """Tracked and untracked, not ignored files; build outputs are ignored."""
    result = subprocess.run(
        ["git", "-C", str(repo_root), "ls-files", "-z", "--cached", "--others",
         "--exclude-standard"],
        check=True, capture_output=True,
    )
    names = {name for name in result.stdout.decode("utf-8").split("\0") if name}
    return sorted(name for name in names if (repo_root / name).is_file())


def build_scopes(repo_root: Path, module_names: list[str], hal_cpd) -> list[Scope]:
    """Split owned C/C++ and Python files into CPD scans."""
    production: dict[str, list[str]] = {}
    tests: list[str] = []
    python: list[str] = []
    for name in owned_files(repo_root):
        path = PurePosixPath(name)
        suffix = path.suffix.lower()
        if suffix in hal_cpd.PYTHON_SOURCE_EXTENSIONS:
            python.append(name)
            continue
        if suffix not in hal_cpd.CPP_SOURCE_EXTENSIONS or path.parts[0] != "src":
            continue
        if TESTS_DIR in path.parts:
            tests.append(name)
            continue
        top = path.parts[1]
        if top == COMMON_DIR:
            label = "common" if len(path.parts) == 3 else f"common_{path.parts[2]}"
        elif top in module_names:
            label = top
        else:
            raise hal_cpd.CpdError(f"{name}: src/{top} is not a module in modules.json")
        production.setdefault(label, []).append(name)

    def scope(label: str, language: str, minimum: int, names: list[str]) -> Scope:
        return Scope(label, language, minimum,
                     tuple(repo_root / name for name in sorted(names)))

    scopes = [scope(f"production_{label}", "cpp", hal_cpd.MINIMUM_TOKENS, names)
              for label, names in sorted(production.items())]
    if tests:
        scopes.append(scope("tests", "cpp", hal_cpd.MINIMUM_TOKENS, tests))
    if python:
        scopes.append(scope("python", "python",
                            hal_cpd.SCRIPTS_PYTHON_MINIMUM_TOKENS, python))
    return scopes


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output-dir", type=Path, default=REPO_ROOT / ".build" / "cpd")
    parser.add_argument("--pmd", default="", help="PMD executable instead of the "
                        "one pinned by JaszczurHAL")
    parser.add_argument("--jaszczurhal-root", type=Path, default=DEFAULT_HAL_ROOT)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT,
                        help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    output_dir = args.output_dir.resolve()
    hal_cpd = load_hal_cpd(args.jaszczurhal_root.resolve())
    try:
        modules = [module.name for module in load_module_registry(repo_root).modules]
        scopes = build_scopes(repo_root, modules, hal_cpd)
        pmd = hal_cpd.resolve_pmd(args.jaszczurhal_root.resolve(), args.pmd)
        output_dir.mkdir(parents=True, exist_ok=True)
        reports = [(item, hal_cpd.run_scan(pmd, repo_root, output_dir, item.label,
                                           item.language, list(item.sources),
                                           item.minimum_tokens))
                   for item in scopes]
    except (hal_cpd.CpdError, subprocess.CalledProcessError) as error:
        print(f"fiesta_cpd.py: {error}", file=sys.stderr)
        return 2

    total = 0
    for item, report in reports:
        total += len(report.groups)
        print(f"CPD {item.label}: {len(item.sources)} files, {len(report.groups)} "
              f"duplicate group(s) at >= {item.minimum_tokens} tokens")
        for group in report.groups:
            places = " <-> ".join(f"{o.path}:{o.line}" for o in group.occurrences)
            print(f"  {group.tokens:4d} tokens: {places}")
    if total:
        sys.stdout.flush()
        print(f"CPD gate failed with {total} duplicate group(s); XML reports in "
              f"{output_dir}", file=sys.stderr)
        return 1
    print(f"CPD gate passed: no duplicate groups in {len(scopes)} scans.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
