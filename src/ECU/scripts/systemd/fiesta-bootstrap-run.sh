#!/usr/bin/env bash
# =============================================================================
# Fiesta daily bootstrap runner (invoked by fiesta-bootstrap.service)
#
# Flow:
#   1. Sync the Fiesta repo at $FIESTA_DIR (clone if missing, hard reset to
#      origin/$BRANCH otherwise — this runner is unattended and expects no
#      local changes on the Pi).
#   2. Remove previous ECU build artifacts (build_test/, .build/).
#   3. Run src/ECU/scripts/bootstrap.sh with SKIP_APT=1 (system packages are
#      set up once, manually, on the Pi; the user service cannot sudo).
#   4. Compose a status email and hand it to send-status.py.
#
# Config comes from fiesta-bootstrap.env (loaded by the systemd unit):
#   FIESTA_DIR, FIESTA_REPO_URL, BRANCH, SMTP_*, MAIL_FROM, MAIL_TO
# =============================================================================
set -u
umask 022

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FIESTA_DIR="${FIESTA_DIR:-$HOME/Documents/Fiesta}"
FIESTA_REPO_URL="${FIESTA_REPO_URL:-https://github.com/jaszczurtd/Fiesta.git}"
BRANCH="${BRANCH:-main}"
LOG_DIR="${FIESTA_LOG_DIR:-$HOME/.cache/fiesta-bootstrap}"

# systemd's EnvironmentFile= does not expand %h / %u / $HOME / ~. Catch the
# common mistake early — otherwise the literal specifier leaks into every
# downstream path and arduino-cli will fail with a confusing linker error.
for var in FIESTA_DIR FIESTA_REPO_URL LOG_DIR BRANCH; do
    val="${!var}"
    case "$val" in
        *%[a-zA-Z]*|\~*|\$*)
            echo "[ERROR] $var contains an unexpanded specifier or shell metachar: '$val'" >&2
            echo "[ERROR] EnvironmentFile= takes values literally — use an absolute path." >&2
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

mkdir -p "$LOG_DIR"
STAMP="$(date -u +'%Y%m%dT%H%M%SZ')"
LOG_FILE="$LOG_DIR/run-$STAMP.log"
BODY_FILE="$LOG_DIR/body-$STAMP.txt"
ln -sf "$LOG_FILE" "$LOG_DIR/last.log"

# Mirror all output into the log file.
exec > >(tee -a "$LOG_FILE") 2>&1

echo "=== Fiesta daily bootstrap run — $STAMP ==="
echo "Host:          $(hostname)"
echo "User:          $(id -un)"
echo "FIESTA_DIR:    $FIESTA_DIR"
echo "Branch:        $BRANCH"
echo

# -----------------------------------------------------------------------------
# 1. Repo sync
# -----------------------------------------------------------------------------
sync_rc=0
if [[ ! -d "$FIESTA_DIR/.git" ]]; then
    echo "[INFO] Cloning $FIESTA_REPO_URL into $FIESTA_DIR"
    mkdir -p "$(dirname "$FIESTA_DIR")"
    if ! git clone --branch "$BRANCH" "$FIESTA_REPO_URL" "$FIESTA_DIR"; then
        sync_rc=1
    fi
else
    echo "[INFO] Updating existing checkout at $FIESTA_DIR"
    if ! git -C "$FIESTA_DIR" fetch --prune origin; then sync_rc=1; fi
    if [[ $sync_rc -eq 0 ]]; then
        if ! git -C "$FIESTA_DIR" checkout "$BRANCH"; then sync_rc=1; fi
    fi
    if [[ $sync_rc -eq 0 ]]; then
        if ! git -C "$FIESTA_DIR" reset --hard "origin/$BRANCH"; then sync_rc=1; fi
    fi
fi

HEAD_SHA="unknown"
HEAD_SUBJ="unknown"
if [[ -d "$FIESTA_DIR/.git" ]]; then
    HEAD_SHA="$(git -C "$FIESTA_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"
    HEAD_SUBJ="$(git -C "$FIESTA_DIR" log -1 --format='%s' 2>/dev/null || echo unknown)"
fi
echo "[INFO] HEAD: $HEAD_SHA — $HEAD_SUBJ"

# -----------------------------------------------------------------------------
# 2. Clean ECU build artifacts
# -----------------------------------------------------------------------------
echo "[INFO] Cleaning previous ECU artifacts"
rm -rf "$FIESTA_DIR/src/ECU/build_test" "$FIESTA_DIR/src/ECU/.build"

# -----------------------------------------------------------------------------
# 3. Run bootstrap (SKIP_APT=1 — apt requires sudo which user service lacks)
# -----------------------------------------------------------------------------
bootstrap_rc=0
if [[ $sync_rc -ne 0 ]]; then
    echo "[ERROR] Skipping bootstrap because repo sync failed (rc=$sync_rc)"
    bootstrap_rc=$sync_rc
else
    echo "[INFO] Running bootstrap.sh with SKIP_APT=1"
    SKIP_APT=1 bash "$FIESTA_DIR/src/ECU/scripts/bootstrap.sh"
    bootstrap_rc=$?
fi
echo "[INFO] Bootstrap exit: $bootstrap_rc"

# -----------------------------------------------------------------------------
# 4. Compose status email
# -----------------------------------------------------------------------------
if [[ $bootstrap_rc -eq 0 ]]; then
    STATUS="PASS"
else
    STATUS="FAIL"
fi

SHORT_SHA="${HEAD_SHA:0:10}"
SUBJECT="[Fiesta] daily bootstrap $STATUS — $SHORT_SHA"

{
    echo "Fiesta daily bootstrap — $STATUS (rc=$bootstrap_rc)"
    echo
    echo "Host:        $(hostname)"
    echo "User:        $(id -un)"
    echo "Timestamp:   $STAMP (UTC)"
    echo "Repo:        $FIESTA_DIR"
    echo "Branch:      $BRANCH"
    echo "HEAD:        $HEAD_SHA"
    echo "Subject:     $HEAD_SUBJ"
    echo "Log file:    $LOG_FILE"
    echo
    echo "── last 80 lines of log ──────────────────────────────────────────"
    tail -n 80 "$LOG_FILE"
} > "$BODY_FILE"

if command -v python3 >/dev/null 2>&1; then
    python3 "$SCRIPT_DIR/send-status.py" \
        --subject "$SUBJECT" \
        --body-file "$BODY_FILE" \
        --attach "$LOG_FILE" \
        || echo "[WARN] Email send failed (see stderr above)"
else
    echo "[WARN] python3 not available — skipping email"
fi

exit "$bootstrap_rc"
