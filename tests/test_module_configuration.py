#!/usr/bin/env python3
"""Check the firmware module configuration against the pinned JaszczurHAL."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
HAL_ROOT = REPO_ROOT / "src" / "JaszczurHAL"
sys.path.insert(0, str(REPO_ROOT / "scripts"))
sys.path.insert(0, str(HAL_ROOT / "scripts"))
sys.path.append(str(HAL_ROOT))

import fiesta_modules  # noqa: E402
import project_config  # noqa: E402
from vscode.runtime import jh_vscode  # noqa: E402


class ModuleConfigurationTests(unittest.TestCase):
    def test_manifests_leave_project_configuration_to_the_header(self) -> None:
        # The HAL build refuses variants and definitions in a manifest.
        for module in fiesta_modules.load().firmware():
            path = REPO_ROOT / "src" / module.name / ".vscode" / "jaszczurhal.project.json"
            manifest = json.loads(path.read_text(encoding="utf-8"))
            with self.subTest(module=module.name):
                self.assertEqual([], jh_vscode.manifest_configuration_findings(manifest))

    def test_ecu_bench_variant_enables_the_functional_tests(self) -> None:
        header = REPO_ROOT / "src" / "ECU" / project_config.HEADER_NAME
        variants = project_config.read_project_config(header).variants
        self.assertEqual([("BENCH", ("ECU_FUNCTIONAL_TESTS_ENABLED=1",))],
                         [(variant.id, variant.definitions) for variant in variants])

    def test_effective_feature_lint_passes(self) -> None:
        # The "Validate HAL feature configuration" step of the firmware
        # workflow; test_module_registry keeps its roots equal to these.
        command = [sys.executable, str(HAL_ROOT / "scripts" / "generate_hal_features.py"),
                   "--lint", "--effective"]
        for root in [module.name for module in fiesta_modules.load().ci()] + ["common"]:
            command += ["--input-root", f"src/{root}"]
        with tempfile.TemporaryDirectory(prefix="fiesta-features-") as temp:
            command += ["--resolution-output", str(Path(temp) / "resolution.json")]
            result = subprocess.run(command, cwd=REPO_ROOT, capture_output=True,
                                    text=True, check=False)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
