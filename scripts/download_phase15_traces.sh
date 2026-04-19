#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

TRACE_DIR="traces"
BASE_URL="https://dpc3.compas.cs.stonybrook.edu/champsim-traces/speccpu"

mkdir -p "$TRACE_DIR"

traces=(
  "471.omnetpp-188B.champsimtrace.xz"
  "459.GemsFDTD-765B.champsimtrace.xz"
  "437.leslie3d-134B.champsimtrace.xz"
)

for trace in "${traces[@]}"; do
  dst="$TRACE_DIR/$trace"
  if [[ -f "$dst" ]]; then
    echo "[skip] $trace already exists"
    continue
  fi

  echo "[download] $trace"
  curl -L -C - --fail --output "$dst" "$BASE_URL/$trace"
done

echo "phase15 traces ready"
