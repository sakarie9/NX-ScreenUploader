#!/bin/bash
# NX-ScreenUploader release packaging script

set -e

APP_TITLE="NX-ScreenUploader"
APP_TITLE_ID="420000000001BF52"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR=$(dirname "${SCRIPT_DIR}")
BUILD_DIR="${PROJECT_DIR}/build"
RELEASE_DIR="${PROJECT_DIR}/build/release"

if [[ -d "$RELEASE_DIR" ]]; then
    rm -r "$RELEASE_DIR"
fi

mkdir "$RELEASE_DIR"
pushd "$RELEASE_DIR"
mkdir -p config/${APP_TITLE}
mkdir -p atmosphere/contents/${APP_TITLE_ID}/flags
cp "${PROJECT_DIR}/config.ini.template" config/${APP_TITLE}/config.ini.template
cp "${PROJECT_DIR}/toolbox.json" atmosphere/contents/${APP_TITLE_ID}/toolbox.json
cp "${BUILD_DIR}/NX-ScreenUploader.nsp" atmosphere/contents/${APP_TITLE_ID}/exefs.nsp
touch atmosphere/contents/${APP_TITLE_ID}/flags/boot2.flag
zip -r "${BUILD_DIR}/${APP_TITLE}" ./*
popd

echo "✓ Release package created at ${BUILD_DIR}/${APP_TITLE}.zip"
