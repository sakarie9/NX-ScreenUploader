#!/bin/bash
# NX-ScreenUploader build script with profiling enabled

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR=$(dirname "$SCRIPT_DIR")
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"

# shellcheck source=devkit-env.sh
source "${SCRIPT_DIR}/devkit-env.sh"

# CLEAN is consumed by prepare_build_dir() in devkit-env.sh.
# shellcheck disable=SC2034
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        *) echo "error: unknown option: $arg" >&2; exit 1 ;;
    esac
done

resolve_devkitpro
prepare_build_dir "$BUILD_DIR"

echo "==> devkitPro: $DEVKITPRO"

# Build
cd "$BUILD_DIR"
cmake "$PROJECT_DIR" -DCMAKE_TOOLCHAIN_FILE="$PROJECT_DIR/devkita64-libnx.cmake" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DENABLE_PROFILING=ON
make -j"$(nproc 2>/dev/null || echo 2)"

echo "✓ Build completed!"
