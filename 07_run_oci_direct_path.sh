#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

: "${DB_USER:?Set DB_USER to the Oracle schema name}"
: "${DB_ALIAS:?Set DB_ALIAS to an Oracle Net service name}"
DATA_FILE="${DATA_FILE:-${SCRIPT_DIR}/bulk_test_10m.dat}"
[[ -f "$DATA_FILE" ]] || { echo "Missing data file: $DATA_FILE" >&2; exit 2; }
[[ -x ./oci_direct_path_load ]] || {
  echo "Missing ./oci_direct_path_load; build it first with: make" >&2
  exit 2
}

exec ./oci_direct_path_load "$DB_USER" "$DB_ALIAS" "$DATA_FILE"
