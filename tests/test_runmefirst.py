"""Check bootstrap cleanup without installing packages or building firmware."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import fiesta_modules  # noqa: E402

MODULE_FRAGMENT = "src/common/scripts/fiesta-modules.sh"


@unittest.skipUnless(shutil.which("bash"), "runmefirst requires Bash")
class RunmefirstTests(unittest.TestCase):
    @unittest.skipUnless(Path("/bin/sh").is_file(), "GUI runner requires POSIX")
    def test_sc_display_runner_propagates_errors_and_requires_headless_display(self):
        runner = ROOT / "scripts/sc-with-display.sh"
        with tempfile.TemporaryDirectory(prefix="fiesta-gui-runner-") as temporary:
            bindir = Path(temporary)
            env = dict(os.environ, PATH=str(bindir), DISPLAY=":9")
            env.pop("WAYLAND_DISPLAY", None)
            command = [shutil.which("bash"), str(runner), "/bin/sh", "-c", "exit 23"]
            # A real display preserves the command and its failure status.
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stderr)
            env.pop("DISPLAY")
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("require a display", result.stderr)
            # Exercise command forwarding under the headless runner as well.
            (bindir / "env").symlink_to(shutil.which("env"))
            xvfb = bindir / "xvfb-run"
            xvfb.write_text('#!/bin/sh\n[ "$1" = -a ] || exit 2\nshift\nexec "$@"\n')
            xvfb.chmod(0o755)
            command[-1] = '[ "$GDK_BACKEND" = x11 ] || exit 2; exit 23'
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stderr)
            # The documented desktop helper must use the same display path.
            project = bindir / "Fiesta checkout/src/SerialConfigurator"
            (project / "scripts").mkdir(parents=True)
            shutil.copy2(ROOT / "src/SerialConfigurator/scripts/desktop-build.sh", project / "scripts")
            (project.parents[1] / "scripts").mkdir()
            shutil.copy2(runner, project.parents[1] / "scripts")
            (bindir / "dirname").symlink_to(shutil.which("dirname"))
            (bindir / "bash").symlink_to(shutil.which("bash"))
            for name, status in (("cmake", 0), ("ctest", 23)):
                stub = bindir / name
                stub.write_text(f"#!/bin/sh\nexit {status}\n")
                stub.chmod(0o755)
            result = subprocess.run([shutil.which("bash"), str(project / "scripts/desktop-build.sh"), "test"],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stderr)

    @unittest.skipUnless(Path("/bin/sh").is_file(), "Locale setup requires POSIX")
    def test_missing_gui_locale_is_rejected_or_generated_by_setup(self):
        helper = ROOT / "src/ECU/scripts/ensure-system-dependencies.sh"
        with tempfile.TemporaryDirectory(prefix="fiesta-locales-") as temporary:
            base = Path(temporary)
            python = base / "python3"
            # pl_PL.UTF-8 is missing until locale-gen ran, or for good.
            python.write_text('#!/bin/sh\n[ -e "${0%/*}/root-call" ] && '
                              '[ ! -e "${0%/*}/broken" ] || echo pl_PL.UTF-8\n')
            python.chmod(0o755)
            # Raspberry Pi OS: Debian's locale-gen builds only enabled entries.
            config = base / "locale.gen"
            config.write_text("# en_US.UTF-8 UTF-8\npl_PL.UTF-8 UTF-8\n")
            env = dict(os.environ, PATH=str(base), FIESTA_LOCALE_GEN_CONFIG=str(config))
            marker = base / "root-call"
            script = ('. "$1"\nmarker=$2\n'
                      'run_dependency_command_as_root() { printf "%s\\n" "$*" >> "$marker"; }\n'
                      'ensure_test_locales "$3" 1\n')
            command = [shutil.which("bash"), "-c", script, "locale-test", str(helper), str(marker)]
            result = subprocess.run([*command, "check"], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("Missing GUI test locales", result.stderr)
            self.assertFalse(marker.exists())
            result = subprocess.run([*command, "install"], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(marker.read_text().splitlines(), [
                f"1 python3 - {config} en_US.UTF-8 pl_PL.UTF-8",
                "1 locale-gen en_US.UTF-8 pl_PL.UTF-8"])
            # A locale-gen that exits 0 without the locale fails the setup.
            marker.unlink()
            (base / "broken").touch()
            result = subprocess.run([*command, "install"], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("locale-gen did not provide", result.stderr)

    def test_cleanup_is_scoped_and_precedes_bootstrap_from_another_directory(self):
        with tempfile.TemporaryDirectory(prefix="fiesta-cleanup-") as temporary:
            base = Path(temporary)
            repo = base / "Fiesta checkout"
            repo.mkdir()
            shutil.copy2(ROOT / "runmefirst.sh", repo)
            (repo / MODULE_FRAGMENT).parent.mkdir(parents=True)
            shutil.copy2(ROOT / MODULE_FRAGMENT, repo / MODULE_FRAGMENT)
            registry = fiesta_modules.load(ROOT)
            removed = [f"src/{module.name}/{module.host_tests}"
                       for module in registry.host_tests()]
            removed += [f"src/{module.name}/.build" for module in registry.firmware()]
            # The cases below rely on these directories being in the registry.
            for relative in ("src/ECU/build_test", "src/SerialConfigurator/build",
                             "src/Fiesta_clock/.build"):
                self.assertIn(relative, removed)
            kept = ["src/JaszczurHAL/.build/cache", "src/ECU/start.c",
                    "src/ECU/.vscode/jaszczurhal.local.json", "build/user-file"]
            for relative in removed:
                directory = repo / relative
                directory.mkdir(parents=True)
                (directory / "artifact").write_text("generated")
            for relative in kept:
                path = repo / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("keep")
            bootstrap = repo / "src/ECU/scripts/bootstrap.sh"
            bootstrap.parent.mkdir(parents=True)
            bootstrap.write_text(
                "#!/usr/bin/env bash\nset -eu\n"
                "test ! -e src/ECU/build_test\n"
                "test ! -e src/SerialConfigurator/build\n"
                "printf started > bootstrap-ran\nexit 23\n"
            )
            bootstrap.chmod(0o755)
            # A symlink build directory must not cause deletion of its target.
            outside = base / "outside"
            outside.mkdir()
            (outside / "keep").write_text("keep")
            linked = repo / "src/Fiesta_clock/.build"
            shutil.rmtree(linked)
            linked.symlink_to(outside, target_is_directory=True)
            for _ in range(2):
                result = subprocess.run(["bash", str(repo / "runmefirst.sh")],
                                        cwd=base, capture_output=True, text=True)
                self.assertEqual(result.returncode, 23, result.stderr)
                self.assertTrue((repo / "bootstrap-ran").is_file())
                for relative in removed:
                    self.assertFalse((repo / relative).exists(), relative)
                for relative in kept:
                    self.assertEqual((repo / relative).read_text(), "keep")
                self.assertEqual((outside / "keep").read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
