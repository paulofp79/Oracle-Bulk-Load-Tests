#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

: "${DB_USER:?Set DB_USER to the Oracle schema name}"
: "${DB_ALIAS:?Set DB_ALIAS to the Oracle Net service name}"
[[ -f bulk_test_10m.dat ]] || { echo "Missing bulk_test_10m.dat; run 02_generate_data.py first." >&2; exit 2; }

SECONDS=0
set +e
sqlldr "userid=${DB_USER}@${DB_ALIAS}" \
  "control=${SCRIPT_DIR}/03_sqlldr.ctl" \
  "log=${SCRIPT_DIR}/sqlldr.log"
rc=$?
set -e
printf 'SQL*Loader exit code: %s; elapsed seconds: %s\n' "$rc" "$SECONDS"
exit "$rc"
