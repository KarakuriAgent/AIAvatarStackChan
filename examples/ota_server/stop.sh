#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PID_FILE="$SCRIPT_DIR/ota_server.pid"

if [[ ! -f "$PID_FILE" ]]; then
  echo "OTA server is not running: no pid file"
  exit 0
fi

PID=$(cat "$PID_FILE")
if [[ ! "$PID" =~ ^[0-9]+$ ]]; then
  echo "Invalid pid file; removing it"
  rm -f "$PID_FILE"
  exit 1
fi

if ! kill -0 "$PID" 2>/dev/null; then
  echo "OTA server is not running: stale pid=$PID"
  rm -f "$PID_FILE"
  exit 0
fi

kill "$PID"
for _ in {1..30}; do
  if ! kill -0 "$PID" 2>/dev/null; then
    rm -f "$PID_FILE"
    echo "OTA server stopped"
    exit 0
  fi
  sleep 0.1
done

kill -9 "$PID" 2>/dev/null || true
rm -f "$PID_FILE"
echo "OTA server force-stopped"
