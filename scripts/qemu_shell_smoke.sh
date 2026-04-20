#!/usr/bin/env bash
set -euo pipefail

LOG_FILE="${1:-build/qemu-shell.log}"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-35}"
BOOT_DELAY_SECONDS="${BOOT_DELAY_SECONDS:-8}"

mkdir -p "$(dirname "$LOG_FILE")"
: >"$LOG_FILE"

tmpdir="$(mktemp -d)"
fifo="$tmpdir/serial.in"
mkfifo "$fifo"

cleanup() {
  if [[ -n "${qemu_pid:-}" ]] && kill -0 "$qemu_pid" 2>/dev/null; then
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
  fi
  if [[ -n "${feeder_pid:-}" ]] && kill -0 "$feeder_pid" 2>/dev/null; then
    kill "$feeder_pid" 2>/dev/null || true
    wait "$feeder_pid" 2>/dev/null || true
  fi
  rm -rf "$tmpdir"
}
trap cleanup EXIT

(
  sleep "$BOOT_DELAY_SECONDS"
  printf 'help\n'
  sleep 1
  printf 'which ls\n'
  sleep 1
  printf 'ls --help\n'
  sleep 1
  printf 'cat --help\n'
  sleep 1
  printf 'exit\n'
) >"$fifo" &
feeder_pid=$!

qemu-system-x86_64 \
  -cdrom ocean.iso \
  -serial stdio \
  -display none \
  -m 256M \
  -smp 2 \
  -no-reboot \
  -no-shutdown \
  <"$fifo" >"$LOG_FILE" 2>&1 &
qemu_pid=$!

deadline=$((SECONDS + TIMEOUT_SECONDS))
while (( SECONDS < deadline )); do
  if grep -q "Shell exited cleanly" "$LOG_FILE"; then
    break
  fi
  sleep 1
done

if ! grep -q "Ocean Shell v" "$LOG_FILE"; then
  echo "Shell smoke failed: shell banner missing"
  exit 1
fi

if ! grep -q "Built-in commands:" "$LOG_FILE"; then
  echo "Shell smoke failed: help output missing"
  exit 1
fi

if ! grep -q "ls: /boot/ls.elf" "$LOG_FILE"; then
  echo "Shell smoke failed: which output missing"
  exit 1
fi

if ! grep -Fq "usage: ls [--help] [/boot]" "$LOG_FILE"; then
  echo "Shell smoke failed: argv handling for ls missing"
  exit 1
fi

if ! grep -Fq "usage: cat [--help] [FILE...|-]" "$LOG_FILE"; then
  echo "Shell smoke failed: argv handling for cat missing"
  exit 1
fi

if ! grep -q "Shell exited cleanly" "$LOG_FILE"; then
  echo "Shell smoke failed: shell did not exit cleanly before timeout"
  exit 1
fi

if grep -Eq "Triple fault|Unhandled page fault|panic|General protection|Page fault:" "$LOG_FILE"; then
  echo "Shell smoke failed: fatal signature found in serial log"
  grep -nE "Triple fault|Unhandled page fault|panic|General protection|Page fault:" "$LOG_FILE" || true
  exit 1
fi

echo "Shell smoke passed: $LOG_FILE"
