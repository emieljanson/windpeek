#!/usr/bin/env bash
set -euo pipefail

HOST="${WINDPEEK_HOST:-windpeek.local}"
FIRMWARE="${1:-build/windpeek.bin}"
BASE_URL="http://${HOST}"

if [[ ! -f "$FIRMWARE" ]]; then
  echo "Firmware not found: $FIRMWARE" >&2
  echo "Build it first with: ./build.py --board seeedstudio_reterminal_e1002 --step firmware" >&2
  exit 1
fi

echo "Checking ${BASE_URL}..."
STATUS="$(curl --fail --silent --show-error --connect-timeout 5 "${BASE_URL}/api/ota/status")"
ENABLED="$(printf '%s' "$STATUS" | python3 -c 'import json,sys; print(str(json.load(sys.stdin).get("local_upload_enabled", False)).lower())')"
if [[ "$ENABLED" != "true" ]]; then
  echo "Firmware upload is locked." >&2
  echo "Press the WAKE button once, then run this command again within 10 minutes." >&2
  exit 2
fi

echo "Uploading $(basename "$FIRMWARE") to ${HOST}..."
curl --fail --show-error \
  --connect-timeout 5 \
  --max-time 240 \
  -H 'Content-Type: application/octet-stream' \
  --data-binary "@${FIRMWARE}" \
  "${BASE_URL}/api/ota/upload"
echo
echo "Firmware accepted. Waiting for Windpeek to restart..."

for _ in $(seq 1 60); do
  sleep 2
  if NEW_STATUS="$(curl --fail --silent --connect-timeout 2 "${BASE_URL}/api/ota/status" 2>/dev/null)"; then
    VERSION="$(printf '%s' "$NEW_STATUS" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("current_version", "unknown"))')"
    echo "Windpeek is online with firmware ${VERSION}."
    exit 0
  fi
done

echo "Upload succeeded, but Windpeek did not return within 120 seconds." >&2
exit 3
