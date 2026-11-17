#!/usr/bin/env python3
"""Compatibility adapter for the local C module registry in tools/modules."""

from __future__ import annotations

from dataclasses import dataclass, field
from functools import lru_cache
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO_ROOT = Path(__file__).resolve().parents[1]
TOKENS_HEADER = Path("src/common/scDefinitions/sc_fiesta_module_tokens.h")
SC_TABLE_HEADER = Path("src/SerialConfigurator/src/core/sc_module_table.h")
SHELL_FRAGMENT = Path("src/common/scripts/fiesta-modules.sh")


class RegistryError(ValueError):
    """The native registry reader rejected its input or could not run."""


@lru_cache(maxsize=1)
def _tool() -> str:
    override = os.environ.get("FIESTA_MODULE_TOOL")
    if override:
        return override
    result = subprocess.run(
        ["cmake", "-P", str(REPO_ROOT / "scripts/fiesta_tool.cmake")],
        capture_output=True, text=True, check=False)
    if result.returncode:
        raise RegistryError(result.stderr.strip())
    return result.stdout.strip()


def _command(*args: str) -> str:
    result = subprocess.run([_tool(), *args], capture_output=True,
                            text=True, encoding="utf-8", check=False)
    if result.returncode:
        raise RegistryError(result.stderr.strip())
    return result.stdout


@dataclass(frozen=True)
class ScEntry:
    index: int
    display_name: str
    tests: bool
    tests_drive: bool


@dataclass(frozen=True)
class Module:
    name: str
    kind: str
    token: str | None
    macro: str | None
    serial_configurator: ScEntry | None
    host_tests: str | None
    analysis: tuple[str, ...]
    ci: bool
    by_id_hint: str | None
    shared_sources: tuple[str, ...] = ()

    @property
    def is_firmware(self) -> bool:
        return self.kind == "firmware"


@dataclass(frozen=True)
class Registry:
    modules: tuple[Module, ...]
    _outputs: dict[Path, str] = field(repr=False)

    def firmware(self) -> list[Module]:
        return [module for module in self.modules if module.is_firmware]

    def serial_configurator(self) -> list[Module]:
        return sorted((m for m in self.modules if m.serial_configurator is not None),
                      key=lambda m: m.serial_configurator.index)

    def host_tests(self) -> list[Module]:
        return [module for module in self.modules if module.host_tests]

    def ci(self) -> list[Module]:
        return [module for module in self.modules if module.ci]


def load(repo_root: Path = REPO_ROOT) -> Registry:
    """Adapt the validated native result to the former import API."""
    result = json.loads(_command("--json", "--repo-root", str(repo_root)))
    if result.get("schemaVersion") != 1:
        raise RegistryError("unsupported native registry result")
    modules = []
    for entry in result["modules"]:
        raw = entry["definition"]
        sc = raw.get("serialConfigurator")
        modules.append(Module(
            raw["name"], raw["kind"], raw.get("token"), raw.get("macro"),
            ScEntry(sc["index"], sc["displayName"], sc.get("tests", False),
                    sc.get("testsDrive", False)) if sc is not None else None,
            raw.get("hostTests"), tuple(raw.get("analysis", [])),
            raw.get("ci", raw["kind"] != "inactive"), entry["byIdHint"],
            tuple(raw.get("sharedSources", []))))
    return Registry(tuple(modules), {Path(p): text for p, text in result["outputs"].items()})


def apply_shared_sources(manifest: dict, module: Module,
                         repo_root: Path = REPO_ROOT) -> None:
    with tempfile.TemporaryDirectory(prefix="fiesta-manifest-") as directory:
        path = Path(directory) / "manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        result = json.loads(_command("--repo-root", str(repo_root), "--extra-sources",
                                     module.name, "--manifest", str(path)))
    manifest.clear()
    manifest.update(result)


def outputs(registry: Registry) -> dict[Path, str]:
    return dict(registry._outputs)


def shell_fragment(registry: Registry) -> str:
    return registry._outputs[SHELL_FRAGMENT]


def stale_outputs(registry: Registry, repo_root: Path = REPO_ROOT) -> list[Path]:
    return [relative for relative, content in registry._outputs.items()
            if not (repo_root / relative).is_file()
            or (repo_root / relative).read_text(encoding="utf-8") != content]


def main(argv: list[str]) -> int:
    try:
        result = subprocess.run([_tool(), "--repo-root", str(REPO_ROOT), *argv],
                                check=False)
        return result.returncode
    except (OSError, RegistryError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
