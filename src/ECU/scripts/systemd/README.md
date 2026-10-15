# Daily build with a systemd user service

[Polski](README.pl.md) · [Build guide](../../../../doc/BUILDING.md)

The timer runs Fiesta setup, tests and builds at 13:00 local time on a Linux
host with systemd, such as a Raspberry Pi. `Persistent=true` allows a missed
run after the user service manager starts again.

Use a dedicated checkout: each run clones or **resets `$FIESTA_DIR` with
`git reset --hard origin/$BRANCH`**, discarding tracked local changes.
`runmefirst.sh` then clears module build directories, initializes the recorded
HAL revision, installs missing packages through non-interactive sudo and runs
tests/builds. Local HAL changes stop submodule initialization. HAL build
directories and local VS Code settings are preserved.

The runner emails PASS/FAIL, HEAD SHA, commit subject and the last 80 log lines.
The attachment is limited to the last 512 KB; the complete log stays in
`$FIESTA_LOG_DIR` (default `$HOME/.cache/fiesta-bootstrap/`), with `last.log`
pointing to the latest run.

## Files and service account

| File | Role |
|---|---|
| `fiesta-bootstrap-run.sh` | Updates the checkout, runs setup and sends the result. |
| `send-status.py` | Sends SMTP mail with Python's standard library. |
| `fiesta-bootstrap.service` | User oneshot service. |
| `fiesta-bootstrap.timer` | Daily schedule. |
| `fiesta-bootstrap.env.example` | Paths and SMTP configuration template. |

Only `.service` and `.timer` go in `~/.config/systemd/user/`. Keep the scripts
in the repository so updates reach them together. The service's `ExecStart`
uses `%h/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap-run.sh`;
change the unit if your checkout lives elsewhere.

Run `systemctl --user` as the service account. Prefixing it with `sudo`
switches to root's user session and can fail with
`Failed to connect to bus: No medium found`.

## Install

Run as the account that will own the service. The first setup uses interactive
sudo for apt and may take time:

```bash
mkdir -p ~/Documents
git clone --recurse-submodules https://github.com/jaszczurtd/Fiesta.git ~/Documents/Fiesta
bash ~/Documents/Fiesta/runmefirst.sh

mkdir -p ~/.config
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.env.example \
   ~/.config/fiesta-bootstrap.env
chmod 600 ~/.config/fiesta-bootstrap.env
```

Edit `~/.config/fiesta-bootstrap.env` with your editor, filling in SMTP and
recipient settings before enabling the timer. Paths in that file must be
absolute; see below.

Unattended package repair requires persistent sudo permission for the required
apt commands without a password prompt. `sudo -n true` checks only the current
sudo context; a cached password or permission for `true` alone does not prove
that a later apt invocation will work. The runner checks this context and
uses `sudo -n` for installation, but never changes sudo policy. The package
list is in [`ensure-system-dependencies.sh`](../ensure-system-dependencies.sh).

Install the units and enable the schedule:

```bash
mkdir -p ~/.config/systemd/user
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.service \
   ~/.config/systemd/user/
cp ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap.timer \
   ~/.config/systemd/user/
chmod +x ~/Documents/Fiesta/src/ECU/scripts/systemd/fiesta-bootstrap-run.sh
systemctl --user daemon-reload
systemctl --user enable --now fiesta-bootstrap.timer
systemctl --user status fiesta-bootstrap.timer
```

To keep the user service manager running after logout, enable lingering for
this same account. This command may require administrator authorization:

```bash
sudo loginctl enable-linger "$USER"
```

## Update and inspect

Publish changes before updating the build machine. If only repository scripts
changed, no unit reinstall is needed:

```bash
git -C ~/Documents/Fiesta pull --ff-only
systemctl --user start fiesta-bootstrap.service
```

Starting the service runs the reset/setup/build/email sequence immediately.
Inspect its progress and schedule with:

```bash
journalctl --user -u fiesta-bootstrap.service -f
systemctl --user list-timers --all
less ~/.cache/fiesta-bootstrap/last.log
```

Use your configured log path if it differs from the default.
`TimeoutStartSec=30min` bounds the service run; adjust the unit if the machine
needs longer, then reload the user manager.

## SMTP configuration

Use credentials accepted by the provider for automated SMTP clients, such as
an application password when supported. Put the secret in `SMTP_PASS`.
For a relay allowing unauthenticated sending from this host, leave `SMTP_USER`
and `SMTP_PASS` empty. The mailer uses implicit TLS on port 465 and STARTTLS
on other ports.

## Environment paths

Values in `fiesta-bootstrap.env` are literal. They do not expand `%h`, `%u`,
`$HOME` or `~`. This differs from the unit's `EnvironmentFile=` and `ExecStart=`
paths, where systemd specifiers such as `%h` are used.

Leave `FIESTA_DIR` unset to use the runner's `$HOME/Documents/Fiesta` default,
or supply an absolute path such as `FIESTA_DIR=/home/pi/Documents/Fiesta`.
Replace `pi` with the actual account's path. The runner rejects unexpanded
`%...`, `~` and `$...` tokens in `FIESTA_DIR`, `FIESTA_REPO_URL`,
`FIESTA_LOG_DIR` and `BRANCH`, and rejects a relative `FIESTA_DIR` before any
checkout or build. If an older runner created a literal `%h` directory,
inspect its contents before removing that mistaken checkout.
