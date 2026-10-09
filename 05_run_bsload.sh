#!/usr/bin/env bash
set -euo pipefail

# BSLOAD is customer-specific. Set the executable and adapt the invocation
# block below to the exact command-line interface of your BSLOAD binary.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DATA_FILE="${SCRIPT_DIR}/bulk_test_10m.dat"
TARGET_TABLE="BSLOAD_BULK_TEST_20261009"

: "${BSLOAD_BIN:?Set BSLOAD_BIN to the full path of the customer's BSLOAD executable}"
[[ -f "$DATA_FILE" ]] || { echo "Missing $DATA_FILE; run 02_generate_data.py first." >&2; exit 2; }

echo "BSLOAD target: ${TARGET_TABLE}"
echo "Input file:    ${DATA_FILE}"
echo "Adapt the invocation below to the customer's BSLOAD syntax before running."

# TODO: Replace this line with the customer's actual syntax. The two example
# arguments below are placeholders and are not assumed to match BSLOAD.
SECONDS=0
"$BSLOAD_BIN" "$DATA_FILE" "$TARGET_TABLE"
printf 'BSLOAD elapsed seconds: %s\n' "$SECONDS"
