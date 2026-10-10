#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

: "${DB_USER:?Set DB_USER to the Oracle schema name}"
: "${DB_ALIAS:?Set DB_ALIAS to the Oracle Net service name}"
[[ -f bulk_test_10m.dat ]] || { echo "Missing bulk_test_10m.dat; run 02_generate_data.py first." >&2; exit 2; }

# SQL*Loader expects Easy Connect identifiers in //host:port/service form.
# Prefix the common host:port/service form while preserving TNS aliases,
# already-prefixed Easy Connect identifiers, URLs, and full connect descriptors.
sqlldr_connect_identifier=$DB_ALIAS
case "$sqlldr_connect_identifier" in
  //*|*://*|\(*) ;;
  */*) sqlldr_connect_identifier="//$sqlldr_connect_identifier" ;;
esac

SECONDS=0
set +e
TWO_TASK="$sqlldr_connect_identifier" sqlldr "userid=${DB_USER}" \
  "control=${SCRIPT_DIR}/03_sqlldr.ctl" \
  "log=${SCRIPT_DIR}/sqlldr.log"
rc=$?
set -e
printf 'SQL*Loader exit code: %s; elapsed seconds: %s\n' "$rc" "$SECONDS"
exit "$rc"
