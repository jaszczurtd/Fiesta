# shellcheck shell=bash
# One bootstrap per checkout. runmefirst.sh, bootstrap.sh and the daily
# runner remove and rebuild the same module directories, so a second run
# stops at once instead of deleting them under the first one.

# Status of a run refused because another one holds the checkout.
FIESTA_BOOTSTRAP_BUSY_RC=75

# fiesta_bootstrap_lock_file <repo_root>
# Prints the lock file of a checkout (in its ignored top-level .build).
fiesta_bootstrap_lock_file() {
    printf '%s/.build/bootstrap.lock\n' "$(cd "$1" && pwd -P)"
}

# fiesta_bootstrap_lock_held <repo_root>
# Succeeds inside a locked run of this checkout.
fiesta_bootstrap_lock_held() {
    local root
    root="$(cd "$1" && pwd -P)" || return 1
    [[ "${FIESTA_BOOTSTRAP_LOCK_HELD:-}" == "$root" ]]
}

# fiesta_bootstrap_locked <repo_root> <command> [arguments...]
# Runs the command while holding the lock of <repo_root> and returns its
# status. A command started inside a locked run of the same checkout runs
# directly. When another run holds the lock, prints an error and returns
# FIESTA_BOOTSTRAP_BUSY_RC without running the command. The command and its
# children do not inherit the lock descriptor, so a process left running
# after the bootstrap cannot keep the checkout locked.
fiesta_bootstrap_locked() {
    local root
    root="$(cd "$1" && pwd -P)" || return 1
    shift
    if fiesta_bootstrap_lock_held "$root"; then
        "$@"
        return
    fi
    local lock
    lock="$(fiesta_bootstrap_lock_file "$root")"
    mkdir -p "${lock%/*}" || return 1
    (
        exec {lock_fd}>>"$lock" || exit 1
        if ! flock --nonblock "$lock_fd"; then
            echo "[ERROR] Another Fiesta bootstrap is running in $root" \
                "(lock $lock); wait for it to finish." >&2
            exit "$FIESTA_BOOTSTRAP_BUSY_RC"
        fi
        export FIESTA_BOOTSTRAP_LOCK_HELD="$root"
        "$@" {lock_fd}>&-
    )
}
