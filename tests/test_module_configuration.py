#!/usr/bin/env python3
"""Check the firmware module configuration against the pinned JaszczurHAL."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from native_build import compiler_for_source

REPO_ROOT = Path(__file__).resolve().parents[1]
HAL_ROOT = REPO_ROOT / "src" / "JaszczurHAL"
sys.path.insert(0, str(REPO_ROOT / "scripts"))
sys.path.insert(0, str(HAL_ROOT / "scripts"))
sys.path.append(str(HAL_ROOT))

import fiesta_modules  # noqa: E402
import project_config  # noqa: E402
from vscode.runtime import jh_vscode  # noqa: E402


class ModuleConfigurationTests(unittest.TestCase):
    def test_clocks_gps_constants_respect_wire_limits_in_c_and_cpp(self) -> None:
        header = (REPO_ROOT / "src/Clocks/config.h").read_text(encoding="utf-8")
        cases = [
            (None, None),
            ("GPS_MAX_AGE_MS", "0u"),
            ("GPS_MAX_AGE_MS", "4001u"),
            ("GPS_MAX_AGE_MS", "2000u"),
            ("GPS_UTC_MAX_AGE_MS", "0u"),
            ("GPS_UTC_MAX_AGE_MS", "3000u"),
            ("GPS_MIN_SPEED_KMH", "6u"),
            ("CAN_GPS_UPDATE_INTERVAL", "0u"),
            ("CAN_GPS_UPDATE_INTERVAL", "500u"),
            ("CAN_GPS_UPDATE_INTERVAL", "1001u"),
        ]
        with tempfile.TemporaryDirectory(prefix="clocks-gps-limits-") as temp:
            root = Path(temp)
            for standard in ["c11", "c++17"]:
                source = root / ("check.c" if standard == "c11" else "check.cpp")
                source.write_text('#include "config.h"\n', encoding="utf-8")
                for name, value in cases:
                    with self.subTest(language=standard, constant=name, value=value):
                        text = header
                        if name:
                            text, count = re.subn(
                                rf"(?m)^(#define {name} )\S+", rf"\g<1>{value}", text)
                            self.assertEqual(1, count)
                        (root / "config.h").write_text(text, encoding="utf-8")
                        command = [compiler_for_source(source), "-std=" + standard,
                                   "-fsyntax-only",
                                   "-I", str(HAL_ROOT / "src"),
                                   "-I", str(HAL_ROOT / "third_party/jhbase/include"),
                                   str(source)]
                        result = subprocess.run(command, capture_output=True,
                                                text=True, check=False)
                        if name:
                            self.assertNotEqual(0, result.returncode)
                            self.assertIn("static assertion", result.stderr.lower())
                        else:
                            self.assertEqual(0, result.returncode, result.stderr)

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
