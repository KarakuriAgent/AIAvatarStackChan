#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
ENV_FILE="$SCRIPT_DIR/.env"
DIST_DIR="$SCRIPT_DIR/dist"
FIRMWARE_INFO="$REPO_ROOT/src/FirmwareInfoGenerated.h"
PIO_BIN="$REPO_ROOT/.venv-platformio/bin/pio"
EMBED_SCRIPT="$REPO_ROOT/examples/tools/embed_assets.py"
PRIVATE_ASSETS="$REPO_ROOT/private/firmware_assets/sdroot"
BUILTIN_ASSETS_OUT="$REPO_ROOT/examples/stackchan/basic/src/BuiltinFirmwareAssets"
PROJECT_DIR="$REPO_ROOT/examples/stackchan/basic"
FIRMWARE_BIN="$PROJECT_DIR/.pio/build/cores3/firmware.bin"

usage() {
  cat <<'EOF'
Usage: examples/ota_server/build_ota.sh [--version VERSION] [--base-url URL] [--release-date YYYY-MM-DD]

Builds stackchan/basic, stages firmware.bin into examples/ota_server/dist,
and generates manifest.json for OTA delivery.

Defaults:
  VERSION       current local time as YYYYMMDD-HHMMSS
  RELEASE_DATE  current local date as YYYY-MM-DD
  BASE URL      OTA_PUBLIC_BASE_URL from examples/ota_server/.env, or https://ota.example.com/
EOF
}

VERSION="${OTA_VERSION:-}"
RELEASE_DATE="${OTA_RELEASE_DATE:-}"
BASE_URL="${OTA_PUBLIC_BASE_URL:-}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="${2:-}"
      shift 2
      ;;
    --base-url)
      BASE_URL="${2:-}"
      shift 2
      ;;
    --release-date)
      RELEASE_DATE="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ -f "$ENV_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a
fi

VERSION="${VERSION:-${OTA_VERSION:-}}"
RELEASE_DATE="${RELEASE_DATE:-${OTA_RELEASE_DATE:-}}"
BASE_URL="${BASE_URL:-${OTA_PUBLIC_BASE_URL:-https://ota.example.com/}}"

if [[ -z "$VERSION" ]]; then
  VERSION=$(date '+%Y%m%d-%H%M%S')
fi
if [[ -z "$RELEASE_DATE" ]]; then
  RELEASE_DATE=$(date '+%Y-%m-%d')
fi
if [[ ! "$BASE_URL" =~ ^https:// ]]; then
  echo "OTA public base URL must start with https://: $BASE_URL"
  exit 1
fi

if [[ ! -x "$PIO_BIN" ]]; then
  if command -v pio >/dev/null 2>&1; then
    PIO_BIN=$(command -v pio)
  else
    echo "PlatformIO not found. Expected $PIO_BIN or pio in PATH."
    exit 1
  fi
fi

mkdir -p "$DIST_DIR"

cat > "$FIRMWARE_INFO" <<EOF
#pragma once

#define AIAVATAR_FIRMWARE_VERSION "$VERSION"
#define AIAVATAR_FIRMWARE_RELEASE_DATE "$RELEASE_DATE"
EOF

echo "Firmware version: $VERSION"
echo "Release date: $RELEASE_DATE"
echo "Public base URL: $BASE_URL"
echo "Generated: $FIRMWARE_INFO"

if [[ -d "$PRIVATE_ASSETS" ]]; then
  python3 "$EMBED_SCRIPT" \
    --input "$PRIVATE_ASSETS" \
    --output "$BUILTIN_ASSETS_OUT" \
    --array-name kBuiltinFirmwareAssets \
    --path-prefix /
fi

"$PIO_BIN" run -d "$PROJECT_DIR"

if [[ ! -f "$FIRMWARE_BIN" ]]; then
  echo "Firmware build output not found: $FIRMWARE_BIN"
  exit 1
fi

cp "$FIRMWARE_BIN" "$DIST_DIR/firmware.bin"
python3 "$SCRIPT_DIR/generate_manifest.py" \
  --firmware "$DIST_DIR/firmware.bin" \
  --version "$VERSION" \
  --release-date "$RELEASE_DATE" \
  --base-url "$BASE_URL" \
  --output "$DIST_DIR/manifest.json"

echo "OTA files staged:"
echo "  $DIST_DIR/firmware.bin"
echo "  $DIST_DIR/manifest.json"
