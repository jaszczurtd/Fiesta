"""Exercise the daily runner without network, package installation or email."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("bash"), "daily runner requires Bash")
class DailyRunnerTests(unittest.TestCase):
    def run_fixture(self, sync_fails, bootstrap_rc=23):
        with tempfile.TemporaryDirectory(prefix="fiesta-daily-") as temporary:
            base = Path(temporary)
            repo = base / "Fiesta"
            runner = repo / "src/ECU/scripts/systemd/fiesta-bootstrap-run.sh"
            runner.parent.mkdir(parents=True)
            shutil.copy2(ROOT / "src/ECU/scripts/systemd/fiesta-bootstrap-run.sh", runner)
            shutil.copy2(ROOT / "runmefirst.sh", repo)
            (repo / ".git").mkdir()
            artifact = repo / "src/SerialConfigurator/build/artifact"
            artifact.parent.mkdir(parents=True)
            artifact.write_text("old cache")
            bootstrap = runner.parent.parent / "bootstrap.sh"
            bootstrap.write_text(
                "#!/usr/bin/env bash\nset -eu\n"
                'test "$SKIP_APT" = 0\n'
                'test "$APT_NONINTERACTIVE" = 1\n'
                "test ! -e src/SerialConfigurator/build\n"
                f"printf ran > bootstrap-ran\nexit {bootstrap_rc}\n"
            )
            bootstrap.chmod(0o755)
            commands = base / "commands"
            commands.mkdir()
            fake_git = commands / "git"
            fake_git.write_text(
                "#!/usr/bin/env bash\n"
                'case " $* " in\n'
                '  *" fetch "*) exit "${FIXTURE_SYNC_RC}" ;;\n'
                '  *" rev-parse "*) echo 1234567890abcdef ;;\n'
                '  *" log "*) echo fixture ;;\n'
                "esac\n"
            )
            fake_git.chmod(0o755)
            fake_mail = commands / "python3"
            fake_mail.write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$@" > "$FIXTURE_MAIL_ARGS"\n')
            fake_mail.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                       FIESTA_DIR=str(repo), FIESTA_LOG_DIR=str(base / "logs"),
                       FIESTA_REPO_URL="https://example.invalid/fixture", BRANCH="main",
                       FIXTURE_SYNC_RC="1" if sync_fails else "0",
                       FIXTURE_MAIL_ARGS=str(base / "mail-args"))
            result = subprocess.run(["bash", str(runner)], cwd=base, env=env,
                                    text=True, capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 1 if sync_fails else bootstrap_rc, result.stdout + result.stderr)
            self.assertEqual((repo / "bootstrap-ran").exists(), not sync_fails)
            self.assertEqual(artifact.exists(), sync_fails)
            # Only the fake mail command ran; verify the reported status.
            mail_args = (base / "mail-args").read_text()
            status = "FAIL" if sync_fails or bootstrap_rc else "PASS"
            self.assertIn(f"daily bootstrap {status}", mail_args)
            self.assertIn("--attach", mail_args)

    def test_daily_run_cleans_all_modules_and_propagates_bootstrap_failure(self):
        self.run_fixture(False)

    def test_success_reports_pass(self):
        self.run_fixture(False, bootstrap_rc=0)

    def test_sync_failure_preserves_artifacts_and_skips_bootstrap(self):
        self.run_fixture(True)


if __name__ == "__main__":
    unittest.main()
