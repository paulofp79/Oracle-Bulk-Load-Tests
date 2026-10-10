#!/usr/bin/env bash
set -euo pipefail

# The local OCI C simulator is included for this benchmark. Set BSLOAD_BIN to a
# customer executable to use it instead.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DATA_FILE="${SCRIPT_DIR}/bulk_test_10m.dat"
TARGET_TABLE="BSLOAD_BULK_TEST_20261009"

[[ -f "$DATA_FILE" ]] || { echo "Missing $DATA_FILE; run 02_generate_data.py first." >&2; exit 2; }

echo "BSLOAD target: ${TARGET_TABLE}"
echo "Input file:    ${DATA_FILE}"

SECONDS=0
if [[ -n "${BSLOAD_BIN:-}" ]]; then
  # Customer-specific interface: adapt these arguments for that executable.
  "$BSLOAD_BIN" "$DATA_FILE" "$TARGET_TABLE"
else
  # Build on demand; make gives a clear message if OCI SDK headers are missing.
  make -C "$SCRIPT_DIR" bsload_sim
  "${SCRIPT_DIR}/bsload_sim" "$DATA_FILE"
fi
printf 'BSLOAD elapsed seconds: %s\n' "$SECONDS"
