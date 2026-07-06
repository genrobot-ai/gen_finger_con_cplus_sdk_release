#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/build/camera_cmd"

if [ ! -x "$BIN" ]; then
  mkdir -p "$DIR/build"
  cmake -S "$DIR" -B "$DIR/build"
  cmake --build "$DIR/build" --target camera_cmd -j"$(nproc)"
fi

exec "$BIN" "$@"
