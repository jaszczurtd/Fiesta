# Fiesta daily bootstrap — systemd user service

Runs the ECU bootstrap once a day on a Raspberry Pi (or any Linux host with
systemd), captures the result, and emails a status summary.

What the runner does, in order:

1. clone or hard-reset `$FIESTA_DIR` to `origin/$BRANCH`,
2. remove `src/ECU/build_test/` and `src/ECU/.build/` (pre-run clean),
3. run `src/ECU/scripts/bootstrap.sh` with `SKIP_APT=1`
   (system packages are set up once, manually — the user service cannot sudo),
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

# 4. Install user systemd units
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
- Port 465 → implicit TLS; anything else → STARTTLS.

## Notes

- The runner uses `git reset --hard origin/$BRANCH` on every run. Do **not**
  keep local changes in the Pi's checkout; they will be discarded.
- `SKIP_APT=1` is forced because the user-scope service cannot `sudo apt-get`
  without prompting. Re-run the full bootstrap manually to pick up new apt
  deps (e.g. after adding a package to `bootstrap.sh`).
- The runner caps attached log size at 512 KB (tail); the full file stays
  under `$FIESTA_LOG_DIR`.
- `TimeoutStartSec=30min` is a safety net; adjust if the Pi 5 needs more.
