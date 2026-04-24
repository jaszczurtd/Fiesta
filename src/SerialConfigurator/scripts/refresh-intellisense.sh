#!/usr/bin/env bash
# =============================================================================
# Refresh IntelliSense for SerialConfigurator (CMake)
# Runs CMake configure with EXPORT_COMPILE_COMMANDS and writes
# c_cpp_properties.json pointing at the generated compile_commands.json
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"
VSCODE_DIR="${PROJECT_DIR}/.vscode"
CPP_PROPS="${VSCODE_DIR}/c_cpp_properties.json"

echo "[INFO] Configuring CMake (with compile_commands.json)..."
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "[INFO] Writing ${CPP_PROPS}..."
cat > "$CPP_PROPS" <<'EOF'
{
    "configurations": [
        {
            "name": "Linux",
            "compileCommands": "${workspaceFolder}/build/compile_commands.json",
            "intelliSenseMode": "linux-gcc-x64"
        }
    ],
    "version": 4
}
EOF

echo "[OK] IntelliSense refreshed."
