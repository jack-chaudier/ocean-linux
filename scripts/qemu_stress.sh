#!/usr/bin/env bash
set -euo pipefail

ITERATIONS="${ITERATIONS:-5}"
BASE_LOG_DIR="${1:-build/stress}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-35}"

mkdir -p "$BASE_LOG_DIR"

for i in $(seq 1 "$ITERATIONS"); do
  log_file="$BASE_LOG_DIR/run-${i}.log"
  echo "[stress] iteration $i/$ITERATIONS"
  TIMEOUT_SECONDS="$TIMEOUT_SECONDS" ./scripts/qemu_smoke.sh "$log_file"
done

echo "Stress check passed: $ITERATIONS iterations"
