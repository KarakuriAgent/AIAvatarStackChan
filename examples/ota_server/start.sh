#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ENV_FILE="$SCRIPT_DIR/.env"
PID_FILE="$SCRIPT_DIR/ota_server.pid"
LOG_FILE="$SCRIPT_DIR/ota_server.log"

cd "$SCRIPT_DIR"

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing $ENV_FILE"
  echo "Create it from .env.example and set OTA_API_KEY."
  exit 1
fi

set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

if [[ "${OTA_API_KEY:-}" == "" || "${OTA_API_KEY:-}" == "change-me" ]]; then
  echo "OTA_API_KEY is not set or still uses the example value."
  exit 1
fi

if [[ -f "$PID_FILE" ]]; then
  PID=$(cat "$PID_FILE")
  if [[ "$PID" =~ ^[0-9]+$ ]] && kill -0 "$PID" 2>/dev/null; then
    echo "OTA server is already running: pid=$PID"
    exit 0
  fi
  rm -f "$PID_FILE"
fi

mkdir -p "${OTA_ROOT:-dist}"
nohup python3 "$SCRIPT_DIR/server.py" >"$LOG_FILE" 2>&1 &
PID=$!
echo "$PID" > "$PID_FILE"
sleep 0.3

if kill -0 "$PID" 2>/dev/null; then
  echo "OTA server started: pid=$PID"
  echo "Log: $LOG_FILE"
  echo "URL: http://${OTA_HOST:-127.0.0.1}:${OTA_PORT:-8080}"
else
  echo "OTA server failed to start. Log: $LOG_FILE"
  rm -f "$PID_FILE"
  exit 1
fi
