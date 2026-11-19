"""Exercise the daily runner without network, package installation or email."""
import fcntl
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("bash"), "daily runner requires Bash")
class DailyRunnerTests(unittest.TestCase):
    def run_fixture(self, sync_fails, bootstrap_rc=23, busy=False, timeout=None,
                    expected_rc=None, reason=None):
        with tempfile.TemporaryDirectory(prefix="fiesta-daily-") as temporary:
            base = Path(temporary)
            repo = base / "Fiesta"
            runner = repo / "src/ECU/scripts/systemd/fiesta-bootstrap-run.sh"
            runner.parent.mkdir(parents=True)
            shutil.copy2(ROOT / "src/ECU/scripts/systemd/fiesta-bootstrap-run.sh", runner)
            shutil.copy2(ROOT / "runmefirst.sh", repo)
            fragment = "src/common/scripts/fiesta-modules.sh"
            (repo / fragment).parent.mkdir(parents=True)
            shutil.copy2(ROOT / fragment, repo / fragment)
            lock_helper = "src/common/scripts/fiesta-bootstrap-lock.sh"
            shutil.copy2(ROOT / lock_helper, repo / lock_helper)
            (repo / ".git").mkdir()
            artifact = repo / "src/SerialConfigurator/build/artifact"
            artifact.parent.mkdir(parents=True)
            artifact.write_text("old cache")
            bootstrap = runner.parent.parent / "bootstrap.sh"
            # With a timeout the bootstrap hangs in a child it waits for.
            hang = 'sleep 60 >/dev/null 2>&1 &\necho $! > child.pid\nwait\n' if timeout else ""
            bootstrap.write_text(
                "#!/usr/bin/env bash\nset -eu\n"
                'test "$SKIP_APT" = 0\n'
                'test "$APT_NONINTERACTIVE" = 1\n'
                "test ! -e src/SerialConfigurator/build\n"
                f"printf ran > bootstrap-ran\n{hang}exit {bootstrap_rc}\n"
            )
            bootstrap.chmod(0o755)
            commands = base / "commands"
            commands.mkdir()
            fake_git = commands / "git"
            fake_git.write_text(
                "#!/usr/bin/env bash\n"
                'case " $* " in\n'
                '  *" fetch "*) echo fetch >> "$FIXTURE_GIT_LOG"; exit "${FIXTURE_SYNC_RC}" ;;\n'
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
                       FIXTURE_MAIL_ARGS=str(base / "mail-args"),
                       FIXTURE_GIT_LOG=str(base / "git-log"))
            if timeout:
                env["FIESTA_BOOTSTRAP_TIMEOUT"] = timeout
            lock = repo / ".build/bootstrap.lock"
            lock.parent.mkdir()
            with open(lock, "a") as handle:
                if busy:
                    # A manual bootstrap holds the checkout.
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                result = subprocess.run(["bash", str(runner)], cwd=base, env=env,
                                        text=True, capture_output=True, timeout=30)
            if expected_rc is None:
                expected_rc = 1 if sync_fails else bootstrap_rc
            self.assertEqual(result.returncode, expected_rc, result.stdout + result.stderr)
            ran = not sync_fails and not busy
            self.assertEqual((repo / "bootstrap-ran").exists(), ran)
            self.assertEqual(artifact.exists(), not ran)
            # A busy checkout is not even fetched.
            self.assertEqual((base / "git-log").exists(), not busy)
            if timeout:
                # The timeout stopped the bootstrap's child as well.
                child = int((repo / "child.pid").read_text())
                for _ in range(50):
                    if not Path(f"/proc/{child}").exists():
                        break
                    time.sleep(0.1)
                self.assertFalse(Path(f"/proc/{child}").exists())
            # Only the fake mail command ran; verify the reported status.
            mail_args = (base / "mail-args").read_text()
            status = "FAIL" if expected_rc else "PASS"
            if reason:
                status += f" ({reason})"
            self.assertIn(f"daily bootstrap {status} -", mail_args)
            self.assertIn("--attach", mail_args)
            lines = mail_args.splitlines()
            body = Path(lines[lines.index("--body-file") + 1]).read_text()
            if reason:
                self.assertIn("Reason:", body)

    def test_daily_run_cleans_all_modules_and_propagates_bootstrap_failure(self):
        self.run_fixture(False)

    def test_success_reports_pass(self):
        self.run_fixture(False, bootstrap_rc=0)

    def test_sync_failure_preserves_artifacts_and_skips_bootstrap(self):
        self.run_fixture(True)

    def test_busy_checkout_is_left_alone_and_reported(self):
        self.run_fixture(False, busy=True, expected_rc=75, reason="busy")

    @unittest.skipUnless(Path("/proc/self").exists() and shutil.which("timeout"),
                         "process checks need /proc and coreutils timeout")
    def test_timeout_stops_the_bootstrap_and_still_reports(self):
        self.run_fixture(False, bootstrap_rc=0, timeout="1s", expected_rc=124,
                         reason="timeout")

    def test_invalid_timeout_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="fiesta-daily-") as temporary:
            base = Path(temporary)
            # Stand-ins record a run that got past the check; nothing leaves the host.
            commands = base / "commands"
            commands.mkdir()
            for name in ("git", "python3"):
                stub = commands / name
                stub.write_text(f'#!/usr/bin/env bash\necho {name} >> "$FIXTURE_CALLS"\nexit 1\n')
                stub.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                       FIESTA_DIR=str(base / "Fiesta"), FIESTA_LOG_DIR=str(base / "logs"),
                       FIESTA_REPO_URL="https://example.invalid/fixture",
                       FIESTA_BOOTSTRAP_TIMEOUT="80 minutes",
                       FIXTURE_CALLS=str(base / "calls"))
            result = subprocess.run(
                ["bash", str(ROOT / "src/ECU/scripts/systemd/fiesta-bootstrap-run.sh")],
                env=env, text=True, capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 2)
            self.assertIn("FIESTA_BOOTSTRAP_TIMEOUT", result.stderr)
            self.assertFalse((base / "calls").exists())


if __name__ == "__main__":
    unittest.main()
