#!/usr/bin/env bash
set -euo pipefail

LOG_FILE="${1:-build/qemu-smoke.log}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-45}"

mkdir -p "$(dirname "$LOG_FILE")"
: >"$LOG_FILE"

QEMU_CMD=(
  qemu-system-x86_64
  -cdrom ocean.iso
  -serial "file:$LOG_FILE"
  -display none
  -m 256M
  -smp 2
  -no-reboot
  -no-shutdown
)

"${QEMU_CMD[@]}" &
QEMU_PID=$!

cleanup() {
  if kill -0 "$QEMU_PID" 2>/dev/null; then
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

deadline=$((SECONDS + TIMEOUT_SECONDS))
while (( SECONDS < deadline )); do
  if grep -q "Kernel initialization complete" "$LOG_FILE" &&
     grep -q "Init started with PID" "$LOG_FILE"; then
    break
  fi
  sleep 1
done

if ! grep -q "Kernel initialization complete" "$LOG_FILE"; then
  echo "Smoke check failed: kernel init signature missing"
  exit 1
fi

if ! grep -q "Init started with PID" "$LOG_FILE"; then
  echo "Smoke check failed: init launch signature missing"
  exit 1
fi

if grep -Eq "Assertion failed|Unhandled page fault|panic|System halted" "$LOG_FILE"; then
  echo "Smoke check failed: fatal signature found in serial log"
  grep -nE "Assertion failed|Unhandled page fault|panic|System halted" "$LOG_FILE" || true
  exit 1
fi

echo "Smoke check passed: $LOG_FILE"
