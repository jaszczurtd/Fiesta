"""Exercise pinned HAL initialization in isolated Git repositories."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("bash"), "HAL bootstrap requires Bash")
class HalSubmoduleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="fiesta-submodule-")
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.env = dict(os.environ, GIT_ALLOW_PROTOCOL="file",
                        GIT_AUTHOR_NAME="Fixture", GIT_AUTHOR_EMAIL="fixture@example.invalid",
                        GIT_COMMITTER_NAME="Fixture", GIT_COMMITTER_EMAIL="fixture@example.invalid")
        self.upstream = self.base / "upstream"
        self.repo = self.base / "Fiesta"
        for directory in (self.upstream, self.repo):
            directory.mkdir()
            self.git(directory, "init", "-q")
        header = self.upstream / "src/JaszczurHAL.h"
        header.parent.mkdir()
        header.write_text("/* first revision */\n")
        self.git(self.upstream, "add", ".")
        self.first = self.commit_fixture("first")
        header.write_text("/* second revision */\n")
        self.git(self.upstream, "add", ".")
        self.second = self.commit_fixture("second", self.first)
        (self.repo / "scripts").mkdir()
        shutil.copy2(ROOT / "scripts/init_hal_submodule.sh", self.repo / "scripts")
        self.git(self.repo, "submodule", "add", str(self.upstream), "src/JaszczurHAL")
        self.hal = self.repo / "src/JaszczurHAL"
        self.git(self.hal, "checkout", "--detach", self.first)
        self.git(self.repo, "add", "src/JaszczurHAL")

    def git(self, directory, *args, input=None):
        return subprocess.run(["git", "-C", str(directory), *args], env=self.env,
                              input=input, text=True, capture_output=True, check=True).stdout.strip()

    def commit_fixture(self, message, parent=None):
        tree = self.git(self.upstream, "write-tree")
        args = ["commit-tree", tree]
        if parent:
            args += ["-p", parent]
        commit = self.git(self.upstream, *args, input=message + "\n")
        self.git(self.upstream, "update-ref", "HEAD", commit)
        return commit

    def initialize(self):
        return subprocess.run(["bash", str(self.repo / "scripts/init_hal_submodule.sh")],
                              env=self.env, text=True, capture_output=True)

    def test_missing_checkout_uses_pin_not_upstream_head(self):
        self.git(self.repo, "submodule", "deinit", "-f", "src/JaszczurHAL")
        result = self.initialize()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.hal / ".git").is_file())
        self.assertEqual(self.git(self.hal, "rev-parse", "HEAD"), self.first)
        self.assertEqual(self.git(self.upstream, "rev-parse", "HEAD"), self.second)
        self.assertEqual(self.initialize().returncode, 0)

    def test_clean_checkout_updates_to_new_pin(self):
        self.git(self.repo, "update-index", "--cacheinfo", "160000", self.second, "src/JaszczurHAL")
        result = self.initialize()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.git(self.hal, "rev-parse", "HEAD"), self.second)

    def test_local_edits_are_preserved_when_pin_changes(self):
        self.git(self.repo, "update-index", "--cacheinfo", "160000", self.second, "src/JaszczurHAL")
        header = self.hal / "src/JaszczurHAL.h"
        header.write_text("local work\n")
        result = self.initialize()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("local changes", result.stderr)
        self.assertEqual(header.read_text(), "local work\n")
        self.assertEqual(self.git(self.hal, "rev-parse", "HEAD"), self.first)

    def test_untracked_work_is_preserved(self):
        local = self.hal / "new-driver.c"
        local.write_text("local work\n")
        result = self.initialize()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(local.read_text(), "local work\n")


if __name__ == "__main__":
    unittest.main()
