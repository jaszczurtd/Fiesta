"""Check bootstrap cleanup without installing packages or building firmware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("bash"), "runmefirst requires Bash")
class RunmefirstTests(unittest.TestCase):
    def test_cleanup_is_scoped_and_precedes_bootstrap_from_another_directory(self):
        with tempfile.TemporaryDirectory(prefix="fiesta-cleanup-") as temporary:
            base = Path(temporary)
            repo = base / "Fiesta checkout"
            repo.mkdir()
            shutil.copy2(ROOT / "runmefirst.sh", repo)
            removed = [
                f"src/{module}/{directory}"
                for module in ("ECU", "Clocks", "OilAndSpeed", "Adjustometer")
                for directory in ("build_test", ".build")
            ] + ["src/Fiesta_clock/.build", "src/SerialConfigurator/build"]
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
