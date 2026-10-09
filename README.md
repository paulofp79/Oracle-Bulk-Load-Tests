# Oracle bulk-load comparison

This bundle compares SQL*Loader direct path with the customer's BSLOAD program
using the same deterministic delimited file and equivalent empty heap tables.
The test tables are named `SQLLDR_BULK_TEST_20261009` and
`BSLOAD_BULK_TEST_20261009` in the schema used to run the SQL scripts.

## Run order

1. Connect to the test schema and run `01_create_tables.sql`.
2. `bulk_test_10m.dat` is included. To regenerate it, run:

   ```sh
   python3 02_generate_data.py
   ```

   It writes 10,000,000 rows (about 540 MB) in pipe-delimited ASCII/UTF-8 text.
   The row format is `ID|CUSTOMER_ID|AMOUNT|LOAD_DATE|PAYLOAD`.
3. Set `DB_USER` and `DB_ALIAS`, then run `04_run_sqlldr.sh`. It prompts for
   the password and writes `sqlldr.log`, `sqlldr_bad.dat`, and
   `sqlldr_discard.dat` as needed.
4. Before running `05_run_bsload.sh`, set `BSLOAD_BIN` and replace its marked
   invocation line with the actual syntax supported by the customer's BSLOAD.
   The BSLOAD program must load `bulk_test_10m.dat` into
   `BSLOAD_BULK_TEST_20261009`. This adapter is intentionally marked because
   BSLOAD's command-line interface was not supplied.
5. After both loads, run `06_compare.sql` in the same schema to compare row
   counts, key range, totals, and a row-hash aggregate. Run the comparison
   after timing so it does not affect either load measurement.

Both target tables use `LOGGING`; neither script requests `NOLOGGING`. The
SQL*Loader script uses recoverable direct path. Do not change logging mode for
a Data Guard test without approval from the database administrator.

The loaders may use different checkpoint/commit behavior: BSLOAD was described
as committing once, while SQL*Loader direct path uses data saves. Record that
alongside elapsed time when comparing results.

To repeat the test, empty both dedicated test tables first, then rerun each
loader. The SQL*Loader control file truncates its target before loading.
