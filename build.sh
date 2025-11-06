#!/bin/bash
# NX-ScreenUploader build script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Ensure build directory exists
mkdir -p "$BUILD_DIR"

# Build
cd "$BUILD_DIR"
cmake .. -DCMAKE_TOOLCHAIN_FILE=../devkita64-libnx.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make

echo "✓ Build completed!"
