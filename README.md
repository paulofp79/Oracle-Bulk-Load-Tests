# Oracle bulk-load comparison

This bundle compares SQL*Loader direct path with a second bulk loader using the
same deterministic delimited file and equivalent empty heap tables. The second
loader can be the customer's BSLOAD program or the separate OCI C Direct Path
runner. The test tables are named `SQLLDR_BULK_TEST_20261009` and
`BSLOAD_BULK_TEST_20261009` in the schema used to run the SQL scripts.

## Run order

1. Connect to the test schema and run `01_create_tables.sql`.
2. `bulk_test_10m.dat` is included. To regenerate it, run:

   ```sh
   python3 02_generate_data.py
   ```

   It writes 10,000,000 rows (about 540 MB) in pipe-delimited ASCII/UTF-8 text.
   The row format is `ID|CUSTOMER_ID|AMOUNT|LOAD_DATE|PAYLOAD`.
3. Set `DB_USER` and `DB_ALIAS`, then run `./04_run_sqlldr.sh` (it is a Bash
   script). `DB_ALIAS` can be a TNS service alias or an Easy Connect value such
   as `scaqat05-scan1.us.oracle.com:1521/STDPDB1`. The script sets `TWO_TASK`
   for the SQL*Loader process and passes only the username, so SQL*Loader's
   username/password parser does not need to parse the connect identifier. It
   prompts for the password and writes `sqlldr.log`, `sqlldr_bad.dat`, and
   `sqlldr_discard.dat` as needed.
4. Build and run the included OCI C simulator:

   ```sh
   # Install the Oracle Client SDK/devel package into this client home first.
   export ORACLE_HOME=/usr/lib/oracle/23/client64
   export DB_USER=your_schema
   export DB_ALIAS=host:1521/service_name
   bash ./05_run_bsload.sh
   ```

   The script builds `bsload_sim.c` against OCI using `oci.h` and `libclntsh`
   under `ORACLE_HOME`, then runs it. This client home already has the OCI
   runtime library; install its matching SDK/devel package to add `oci.h`. The
   C program reads
   `bulk_test_10m.dat`, truncates `BSLOAD_BULK_TEST_20261009`, binds each row
   through OCI, and commits once after the load. It prompts for the password.
   This is a local OCI C simulator, not Oracle's or the customer's proprietary
   BSLOAD executable. If you later obtain that program, set `BSLOAD_BIN` to its
   full path and adapt the invocation in `05_run_bsload.sh` to its actual
   command-line interface.
   See [BSLOAD_SIM_TUTORIAL.md](BSLOAD_SIM_TUTORIAL.md) for a walkthrough of
   the C source and each OCI call.
5. After the SQL*Loader and simulator loads, run `06_compare.sql` in the same
   schema. For SQL*Loader versus the OCI C alternative, use `08_compare_oci.sql`.
   Both compare row counts, key range, totals, and a row-hash aggregate. Run the
   comparison after timing so it does not affect either load measurement.

Both target tables use `LOGGING`; neither script requests `NOLOGGING`. The
SQL*Loader script uses recoverable direct path. Do not change logging mode for
a Data Guard test without approval from the database administrator.

The included simulator uses standard OCI bind and statement execution for each
row, then commits once. Step 07 is a separate OCI Direct Path loader. SQL*Loader
direct path uses data saves. Record the API and commit differences alongside
elapsed time; the simulator does not reproduce proprietary BSLOAD behavior.

To repeat the test, empty both dedicated test tables first, then rerun each
loader. The SQL*Loader control file truncates its target before loading.

## OCI C Direct Path alternative

The OCI C example uses the direct path loading API described in Oracle's OCI
documentation. It loads the same file into `BSLOAD_BULK_TEST_20261009`, so run
it instead of `05_run_bsload.sh` when comparing direct path OCI against
SQL*Loader. It truncates that dedicated test table before loading, as the
SQL*Loader control file does for its target.

The build needs the Oracle Client SDK/devel package matching the installed
client, including `oci.h`, plus the OCI runtime library. Set `ORACLE_HOME` (or
`OCI_HOME`) to that client home and build with:

```sh
make
```

Then set `DB_USER` and `DB_ALIAS` as for SQL*Loader and run:

```sh
./07_run_oci_direct_path.sh
```

The program asks for the database password without echoing it or putting it in
the process arguments. `DATA_FILE` can override the default input file. Its
Easy Connect handling accepts `host:port/service` and adds the `//` prefix
required by OCI, while preserving TNS aliases and complete connect descriptors.
It submits batches of 5,000 rows, reports elapsed time and OCI's loaded-row
count, and uses normal logging. The direct-path load requires the usual direct
path privileges.
