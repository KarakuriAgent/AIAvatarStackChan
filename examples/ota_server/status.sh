#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ENV_FILE="$SCRIPT_DIR/.env"
PID_FILE="$SCRIPT_DIR/ota_server.pid"
LOG_FILE="$SCRIPT_DIR/ota_server.log"

if [[ -f "$ENV_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a
fi

if [[ -f "$PID_FILE" ]]; then
  PID=$(cat "$PID_FILE")
  if [[ "$PID" =~ ^[0-9]+$ ]] && kill -0 "$PID" 2>/dev/null; then
    echo "OTA server: running"
    echo "PID: $PID"
    echo "URL: http://${OTA_HOST:-127.0.0.1}:${OTA_PORT:-8080}"
    echo "Root: ${OTA_ROOT:-dist}"
    echo "Log: $LOG_FILE"
    exit 0
  fi
  echo "OTA server: stopped (stale pid file: $PID)"
  exit 1
fi

echo "OTA server: stopped"
echo "URL: http://${OTA_HOST:-127.0.0.1}:${OTA_PORT:-8080}"
echo "Root: ${OTA_ROOT:-dist}"
