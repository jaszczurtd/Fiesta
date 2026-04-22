# Fiesta daily bootstrap - systemd user service

Runs the ECU bootstrap once a day on a Raspberry Pi (or any Linux host with
systemd), captures the result, and emails a status summary.

What the runner does, in order:

1. clone or hard-reset `$FIESTA_DIR` to `origin/$BRANCH`,
2. remove `src/ECU/build_test/` and `src/ECU/.build/` (pre-run clean),
3. run `src/ECU/scripts/bootstrap.sh` with `SKIP_APT=1`
   (system packages are set up once, manually - the user service cannot sudo),
4. send an email with PASS/FAIL, HEAD SHA, commit subject, and the last
   80 lines of the log (full log attached).

Artifacts under `src/ECU/build_test/` and `src/ECU/.build/` are left in place
after each run. Logs go to `$HOME/.cache/fiesta-bootstrap/run-*.log`
(symlinked as `last.log`).

## Files

| File | Purpose |
| ---- | ------- |
| `fiesta-bootstrap-run.sh`      | Main runner (git sync + clean + bootstrap + email) |
| `send-status.py`               | SMTP mailer (stdlib `smtplib`, no extra deps) |
| `fiesta-bootstrap.service`     | systemd oneshot unit |
| `fiesta-bootstrap.timer`       | systemd timer (13:00 daily, `Persistent=true`) |
| `fiesta-bootstrap.env.example` | SMTP / path config template |

## File placement (important)

Only **two** files from this directory belong in `~/.config/systemd/user/`:

- `fiesta-bootstrap.service`
- `fiesta-bootstrap.timer`

Everything else (`fiesta-bootstrap-run.sh`, `send-status.py`,
`fiesta-bootstrap.env.example`) **stays in the repo**. The service's
`ExecStart=` points at the runner via `%h/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap-run.sh`,
and the runner invokes `send-status.py` from its own directory. Do not
copy those scripts into `~/.config/systemd/user/` - it will not help, and
it creates drift the next time you `git pull`.

## `systemctl --user` never takes `sudo`

`--user` is the current user's scope. Prefixing it with `sudo` switches
to `root`, which has no user-bus session, and you get:

```
Failed to connect to bus: No medium found
```

Always run user-scope commands as yourself:

```bash
systemctl --user daemon-reload            # yes
sudo systemctl --user daemon-reload       # NO
```

## One-time install on the Pi

```bash
# 1. Clone the repo (the unit path assumes ~/Documents/Fiesta)
mkdir -p ~/Documents
git clone https://github.com/jaszczurtd/Fiesta.git ~/Documents/Fiesta

# 2. Run bootstrap manually once so apt deps and the arduino-cli env are set up
bash ~/Documents/Fiesta/src/ECU/scripts/bootstrap.sh

# 3. Create and protect the env file
mkdir -p ~/.config
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.env.example \
   ~/.config/fiesta-bootstrap.env
chmod 600 ~/.config/fiesta-bootstrap.env
$EDITOR ~/.config/fiesta-bootstrap.env     # fill in SMTP_*, MAIL_TO, etc.

# 4. Install user systemd units - ONLY .service and .timer go here.
#    Do NOT copy fiesta-bootstrap-run.sh or send-status.py into this dir.
mkdir -p ~/.config/systemd/user
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.service \
   ~/.config/systemd/user/
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.timer \
   ~/.config/systemd/user/
chmod +x ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap-run.sh

# 5. Enable the timer
systemctl --user daemon-reload
systemctl --user enable --now fiesta-bootstrap.timer

# 6. Let the service run even when no one is logged in
loginctl enable-linger "$USER"
```

## Smoke test / inspection

```bash
# Fire the service now (does not wait for 13:00)
systemctl --user start fiesta-bootstrap.service

# Watch live logs
journalctl --user -u fiesta-bootstrap.service -f

# Confirm the timer is armed
systemctl --user list-timers | grep fiesta-bootstrap

# Latest run log on disk
less ~/.cache/fiesta-bootstrap/last.log
```

## Email setup notes

- Gmail: create an **App Password** (requires 2FA on the bot account) and
  put it in `SMTP_PASS`. Regular account passwords will not work.
- Corporate SMTP relays that require only the sender host: leave `SMTP_USER`
  / `SMTP_PASS` empty and the script will skip authentication.
- Port 465 -> implicit TLS; anything else -> STARTTLS.

## Gotcha: `EnvironmentFile` does not expand `%h` or `$HOME`

`systemd`'s `EnvironmentFile=` directive reads the file as literal
`KEY=VALUE` pairs - it does **not** expand:

- systemd specifiers (`%h`, `%u`, ...),
- shell variables (`$HOME`),
- tildes (`~`).

If you put `FIESTA_DIR=%h/Documents/Fiesta` in `fiesta-bootstrap.env`, the
runner will treat `%h/Documents/Fiesta` as a relative path (rooted at the
service's `WorkingDirectory`, i.e. `$HOME`), end up creating a literal
`$HOME/%h/Documents/Fiesta/` directory, and arduino-cli will fail with a
confusing `ld: cannot open map file …` because the `%h` token leaks into
the linker recipe.

The runner now fail-fasts with a clear error when it sees a `%…` / `~` /
`$…` in `FIESTA_DIR`, `FIESTA_REPO_URL`, `FIESTA_LOG_DIR`, or `BRANCH`, and
when `FIESTA_DIR` is not absolute.

**Fix**: in `~/.config/fiesta-bootstrap.env`, either

- leave `FIESTA_DIR` **unset** (the runner defaults to `$HOME/Documents/Fiesta`), or
- set it to an **absolute path**, e.g. `FIESTA_DIR=/home/pi/Documents/Fiesta`.

If an earlier run already created a literal `%h` directory, clean it up
once:

```bash
rm -rf "$HOME/%h"
```

## Notes

- The runner uses `git reset --hard origin/$BRANCH` on every run. Do **not**
  keep local changes in the Pi's checkout; they will be discarded.
- `SKIP_APT=1` is forced because the user-scope service cannot `sudo apt-get`
  without prompting. Re-run the full bootstrap manually to pick up new apt
  deps (e.g. after adding a package to `bootstrap.sh`).
- The runner caps attached log size at 512 KB (tail); the full file stays
  under `$FIESTA_LOG_DIR`.
- `TimeoutStartSec=30min` is a safety net; adjust if the Pi 5 needs more.
