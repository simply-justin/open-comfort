#!/usr/bin/env bash
# Builds test_app/ and runs every Unity TEST_CASE in the ESP32-S3 QEMU emulator.
# Needs an exported ESP-IDF environment, `idf_tools.py install qemu-xtensa` and libslirp0.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root/test_app"
idf.py build

cd build
python -m esptool --chip esp32s3 merge_bin --fill-flash-size 4MB -o flash.bin @flash_args >/dev/null

log="$(mktemp)"
qemu-system-xtensa -nographic -machine esp32s3 -m 32M \
  -drive file=flash.bin,if=mtd,format=raw -serial mon:stdio >"$log" 2>&1 &
qemu=$!
trap 'kill "$qemu" 2>/dev/null || true; rm -f "$log"' EXIT

for _ in $(seq 1 300); do
  grep -q "OPENCOMFORT_TESTS_DONE" "$log" && break
  kill -0 "$qemu" 2>/dev/null || break
  sleep 1
done

grep -E ":(PASS|FAIL|IGNORE)|Tests .* Failures|Guru Meditation|abort\(\)" "$log" || cat "$log"
grep -q "OPENCOMFORT_TESTS_DONE failures=0" "$log"
