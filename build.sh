#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"

cd "$ROOT/frontend"
npm install
npm run build

rm -rf "$ROOT/firmware/data"
mkdir -p "$ROOT/firmware/data"
cp -R dist/. "$ROOT/firmware/data/"

cd "$ROOT/firmware"
pio run
pio run -t buildfs

if [ "${1:-}" = "upload" ]; then
  pio run -t upload
  pio run -t uploadfs
  pio device monitor
else
  echo
  echo "Build finished. Plug in the ESP32 and run: ./build.sh upload"
fi
