#!/usr/bin/env bash
# =============================================================================
# Fiesta daily bootstrap runner (invoked by fiesta-bootstrap.service)
#
# Flow:
#   1. Sync the Fiesta repo at $FIESTA_DIR (clone if missing, hard reset to
#      origin/$BRANCH otherwise - this runner is unattended and expects no
#      local changes on the Pi).
#   2. Run runmefirst.sh to clean module builds and bootstrap with unattended
#      package provisioning and the pinned HAL submodule. The sync and the
#      bootstrap hold the checkout lock (a manual run makes this one fail
#      instead of losing its build directories) and stop after
#      FIESTA_BOOTSTRAP_TIMEOUT.
#   3. Compose a status email and hand it to send-status.py, also after a
#      timeout.
#
# Config comes from fiesta-bootstrap.env (loaded by the systemd unit):
#   FIESTA_DIR, FIESTA_REPO_URL, BRANCH, FIESTA_BOOTSTRAP_TIMEOUT, SMTP_*,
#   MAIL_FROM, MAIL_TO
# =============================================================================
# Bash parses the braced body whole before running it: the sync below may
# replace this file.
{
set -u
umask 022

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FIESTA_DIR="${FIESTA_DIR:-$HOME/Documents/Fiesta}"
FIESTA_REPO_URL="${FIESTA_REPO_URL:-https://github.com/jaszczurtd/Fiesta.git}"
BRANCH="${BRANCH:-main}"
LOG_DIR="${FIESTA_LOG_DIR:-$HOME/.cache/fiesta-bootstrap}"
# Below the unit's TimeoutStartSec, so a slow or hung run still reports.
BOOTSTRAP_TIMEOUT="${FIESTA_BOOTSTRAP_TIMEOUT:-80m}"
LOCK_SCRIPT="$SCRIPT_DIR/../../../common/scripts/fiesta-bootstrap-lock.sh"

# systemd's EnvironmentFile= does not expand %h / %u / $HOME / ~. Catch the
# common mistake early - otherwise the literal specifier leaks into every
# downstream path and the firmware build fails with a confusing linker error.
for var in FIESTA_DIR FIESTA_REPO_URL LOG_DIR BRANCH; do
    val="${!var}"
    case "$val" in
        *%[a-zA-Z]*|\~*|\$*)
            echo "[ERROR] $var contains an unexpanded specifier or shell metachar: '$val'" >&2
            echo "[ERROR] EnvironmentFile= takes values literally - use an absolute path." >&2
            exit 2
            ;;
    esac
done
case "$FIESTA_DIR" in
    /*) : ;;
    *)
        echo "[ERROR] FIESTA_DIR must be an absolute path, got: '$FIESTA_DIR'" >&2
        exit 2
        ;;
esac
if [[ ! "$BOOTSTRAP_TIMEOUT" =~ ^[0-9]+[smh]?$ ]]; then
    echo "[ERROR] FIESTA_BOOTSTRAP_TIMEOUT must be a number with an optional" \
        "s, m or h suffix, got: '$BOOTSTRAP_TIMEOUT'" >&2
    exit 2
fi

mkdir -p "$LOG_DIR"
STAMP="$(date -u +'%Y%m%dT%H%M%SZ')"
LOG_FILE="$LOG_DIR/run-$STAMP.log"
BODY_FILE="$LOG_DIR/body-$STAMP.txt"
ln -sf "$LOG_FILE" "$LOG_DIR/last.log"

# Mirror all output into the log file.
exec > >(tee -a "$LOG_FILE") 2>&1

echo "=== Fiesta daily bootstrap run - $STAMP ==="
echo "Host:          $(hostname)"
echo "User:          $(id -un)"
echo "FIESTA_DIR:    $FIESTA_DIR"
echo "Branch:        $BRANCH"
echo

# -----------------------------------------------------------------------------
# 1. Repo sync and 2. clean + bootstrap with non-interactive provisioning
# -----------------------------------------------------------------------------
# Updates the checkout and bootstraps it; runs under the checkout lock.
sync_and_bootstrap() {
    echo "[INFO] Updating existing checkout at $FIESTA_DIR"
    if ! git -C "$FIESTA_DIR" fetch --prune origin ||
        ! git -C "$FIESTA_DIR" checkout "$BRANCH" ||
        ! git -C "$FIESTA_DIR" reset --hard "origin/$BRANCH"; then
        echo "[ERROR] Skipping bootstrap because repo sync failed"
        return 1
    fi
    echo "[INFO] Running runmefirst.sh with unattended apt provisioning" \
        "(limit $BOOTSTRAP_TIMEOUT)"
    # timeout stops the whole process group; KILL follows 2 min after TERM.
    SKIP_APT=0 APT_NONINTERACTIVE=1 \
        timeout --kill-after=2m "$BOOTSTRAP_TIMEOUT" bash "$FIESTA_DIR/runmefirst.sh"
}

bootstrap_rc=0
if [[ ! -d "$FIESTA_DIR/.git" ]]; then
    echo "[INFO] Cloning $FIESTA_REPO_URL into $FIESTA_DIR"
    mkdir -p "$(dirname "$FIESTA_DIR")"
    if ! git clone --branch "$BRANCH" "$FIESTA_REPO_URL" "$FIESTA_DIR"; then
        echo "[ERROR] Skipping bootstrap because the clone failed"
        bootstrap_rc=1
    fi
fi
if [[ $bootstrap_rc -eq 0 ]]; then
    # shellcheck source=../../../common/scripts/fiesta-bootstrap-lock.sh
    source "$LOCK_SCRIPT" || bootstrap_rc=1
fi
if [[ $bootstrap_rc -eq 0 ]]; then
    fiesta_bootstrap_locked "$FIESTA_DIR" sync_and_bootstrap || bootstrap_rc=$?
fi
echo "[INFO] Bootstrap exit: $bootstrap_rc"

HEAD_SHA="unknown"
HEAD_SUBJ="unknown"
if [[ -d "$FIESTA_DIR/.git" ]]; then
    HEAD_SHA="$(git -C "$FIESTA_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"
    HEAD_SUBJ="$(git -C "$FIESTA_DIR" log -1 --format='%s' 2>/dev/null || echo unknown)"
fi
echo "[INFO] HEAD: $HEAD_SHA - $HEAD_SUBJ"

# -----------------------------------------------------------------------------
# 3. Compose status email
# -----------------------------------------------------------------------------
REASON=""
case "$bootstrap_rc" in
    124) REASON="timeout" ;;
    137) REASON="killed" ;;
    "${FIESTA_BOOTSTRAP_BUSY_RC:-75}") REASON="busy" ;;
esac
if [[ $bootstrap_rc -eq 0 ]]; then
    STATUS="PASS"
else
    STATUS="FAIL${REASON:+ ($REASON)}"
fi

SHORT_SHA="${HEAD_SHA:0:10}"
SUBJECT="[Fiesta] daily bootstrap $STATUS - $SHORT_SHA"

{
    echo "Fiesta daily bootstrap - $STATUS (rc=$bootstrap_rc)"
    echo
    echo "Host:        $(hostname)"
    echo "User:        $(id -un)"
    echo "Timestamp:   $STAMP (UTC)"
    echo "Repo:        $FIESTA_DIR"
    echo "Branch:      $BRANCH"
    echo "HEAD:        $HEAD_SHA"
    echo "Subject:     $HEAD_SUBJ"
    echo "Log file:    $LOG_FILE"
    case "$REASON" in
        timeout) echo "Reason:      stopped after $BOOTSTRAP_TIMEOUT (FIESTA_BOOTSTRAP_TIMEOUT)" ;;
        killed) echo "Reason:      killed (SIGKILL after the timeout, or the OOM killer)" ;;
        busy) echo "Reason:      another bootstrap was running in $FIESTA_DIR" ;;
    esac
    echo
    echo "── last 80 lines of log ──────────────────────────────────────────"
    tail -n 80 "$LOG_FILE" \
        | perl -pe 's/\e\[[0-9;]*m//g'
} > "$BODY_FILE"

if command -v python3 >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/send-status.py" \
        --subject "$SUBJECT" \
        --body-file "$BODY_FILE" \
        --attach "$LOG_FILE" \
        || echo "[WARN] Email send failed (see stderr above)"
else
    echo "[WARN] python3 not available - skipping email"
fi

exit "$bootstrap_rc"
}
