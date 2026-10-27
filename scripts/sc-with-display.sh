#!/usr/bin/env bash
set -euo pipefail
if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    exec "$@"
fi
if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "GUI tests require a display or xvfb-run (install xvfb and xauth)." >&2
    exit 1
fi
exec env GDK_BACKEND=x11 xvfb-run -a "$@"
