#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
ENV_FILE="$SCRIPT_DIR/.env"

usage() {
  cat <<'EOF'
Usage: examples/ota_server/stage_tools.sh [--source-dir DIR] [--version VERSION] [--base-url URL] [--release-date YYYY-MM-DD]

Packages the tool SD-root directory as tools.zip, writes it into the OTA root's
tools directory, and generates tools/manifest.json for the existing OTA server.

Defaults:
  SOURCE DIR   TOOL_SOURCE_DIR from examples/ota_server/.env, or the first
               existing directory containing tools.json:
               private/tool_assets/sdroot
               sdcard
  VERSION      current local time as YYYYMMDD-HHMMSS
  RELEASE_DATE current local date as YYYY-MM-DD
  OTA ROOT     OTA_ROOT from examples/ota_server/.env, or dist
  BASE URL     OTA_PUBLIC_BASE_URL from examples/ota_server/.env, or https://ota.example.com/
EOF
}

resolve_repo_path() {
  local path="$1"
  if [[ "$path" = /* ]]; then
    echo "$path"
    return
  fi
  if [[ -e "$path" ]]; then
    echo "$(cd "$(dirname "$path")" && pwd)/$(basename "$path")"
    return
  fi
  echo "$REPO_ROOT/$path"
}

select_default_source_dir() {
  local candidate
  for candidate in \
    "$REPO_ROOT/private/tool_assets/sdroot" \
    "$REPO_ROOT/sdcard"; do
    if [[ -f "$candidate/tools.json" ]]; then
      echo "$candidate"
      return
    fi
  done
}

resolve_dist_dir() {
  local root_path="${OTA_ROOT:-dist}"
  if [[ "$root_path" = /* ]]; then
    echo "$root_path"
    return
  fi
  echo "$SCRIPT_DIR/$root_path"
}

SOURCE_DIR=""
VERSION="${TOOL_VERSION:-}"
RELEASE_DATE="${TOOL_RELEASE_DATE:-}"
BASE_URL="${OTA_PUBLIC_BASE_URL:-}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-dir)
      SOURCE_DIR="${2:-}"
      shift 2
      ;;
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

SOURCE_DIR="${SOURCE_DIR:-${TOOL_SOURCE_DIR:-}}"
VERSION="${VERSION:-${TOOL_VERSION:-}}"
RELEASE_DATE="${RELEASE_DATE:-${TOOL_RELEASE_DATE:-}}"
BASE_URL="${BASE_URL:-${OTA_PUBLIC_BASE_URL:-https://ota.example.com/}}"
DIST_DIR=$(resolve_dist_dir)

if [[ -z "$SOURCE_DIR" ]]; then
  SOURCE_DIR=$(select_default_source_dir)
else
  SOURCE_DIR=$(resolve_repo_path "$SOURCE_DIR")
fi
if [[ ! -d "$SOURCE_DIR" ]]; then
  echo "Tool source directory not found."
  echo "Set TOOL_SOURCE_DIR in $ENV_FILE or pass --source-dir DIR."
  exit 1
fi
if [[ ! -f "$SOURCE_DIR/tools.json" ]]; then
  echo "tools.json not found in source directory: $SOURCE_DIR"
  exit 1
fi
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

TOOLS_DIR="$DIST_DIR/tools"
mkdir -p "$TOOLS_DIR"

python3 "$SCRIPT_DIR/package_tools.py" \
  --source-dir "$SOURCE_DIR" \
  --output-zip "$TOOLS_DIR/tools.zip" \
  --manifest "$TOOLS_DIR/manifest.json" \
  --version "$VERSION" \
  --release-date "$RELEASE_DATE" \
  --base-url "$BASE_URL"

echo "Tool package staged:"
echo "  source: $SOURCE_DIR"
echo "  $TOOLS_DIR/tools.zip"
echo "  $TOOLS_DIR/manifest.json"
echo "Device tool_manifest_url:"
echo "  ${BASE_URL%/}/tools/manifest.json"
