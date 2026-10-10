# Teaching Guide: `bsload_sim.c`

This guide explains the local C program in `bsload_sim.c`. It is an OCI
learning example for this repository, not Oracle's or a customer's proprietary
BSLOAD program. It uses ordinary OCI SQL execution with bind variables. It
does **not** use OCI Direct Path; that separate example is
`oci_direct_path_load.c`.

## What the program does

Given a pipe-delimited file with rows shaped like:

```text
ID|CUSTOMER_ID|AMOUNT|LOAD_DATE|PAYLOAD
```

the program:

1. Reads `DB_USER` and `DB_ALIAS` from the environment and the input filename
   from `argv[1]`.
2. Prompts for the password without echoing it.
3. Creates an OCI environment and logs in.
4. Truncates the dedicated table `BSLOAD_BULK_TEST_20261009`.
5. Prepares one `INSERT` statement, binds five values, then parses and executes
   that statement once per input row.
6. Commits once after the full file has loaded.
7. On an error, prints the OCI error and rolls back uncommitted inserts.

The `TRUNCATE` is DDL and commits implicitly in Oracle. A later rollback cannot
restore rows that were in the table before the run. Use this only with the
dedicated benchmark table.

## Headers and constants

The first lines include OCI declarations (`oci.h`) and standard C interfaces
for files, strings, terminal control, and timing. `_POSIX_C_SOURCE` enables the
POSIX declarations used for terminal input. `LINE_SIZE` bounds each file line;
`PASSWORD_SIZE` bounds the hidden password buffer.

Oracle OCI defines types such as `OCIEnv`, `OCIError`, `OCISvcCtx`, `OCIStmt`,
`OCIBind`, `OraText`, `ub2`, and `ub4`. The `ub` types are unsigned Oracle
integer types used in OCI function signatures. `OCINumber` and `OCIDate` are
Oracle's C representations for NUMBER and DATE values.

## `check_oci`: interpreting OCI status codes

OCI calls return a `sword` status. `OCI_SUCCESS` means success;
`OCI_SUCCESS_WITH_INFO` means the call succeeded with a warning. Most other
statuses indicate failure.

`check_oci` accepts plain success. For other statuses it calls `OCIErrorGet`
to retrieve the Oracle error number and message from the error handle, prints
them, and returns false except for `OCI_SUCCESS_WITH_INFO`. This helper keeps
the main path readable while retaining the operation name in diagnostics.

The OCI error handle must be valid before using `OCIErrorGet`; the helper also
handles the case where allocation of that handle itself failed.

## `read_password`: hidden terminal input

This function opens `/dev/tty` so password input still works when ordinary
input or output is redirected. It saves terminal settings with `tcgetattr`,
turns off `ECHO` with `tcsetattr`, reads into a bounded buffer, then restores
the original settings. The password is cleared after use and on read failure.

The password is never placed in the command line. `OCILogon2` receives it
directly from memory. The program requires an interactive terminal for this
prompt.

## `main`: arguments and connection identifier

The invocation is:

```sh
DB_USER=pp DB_ALIAS=host:1521/service ./bsload_sim bulk_test_10m.dat
```

`argc` must be two: the program name plus the input file. Both environment
variables must exist. It opens the file before starting OCI, so a missing file
does not cause a database connection.

The connect string logic accepts:

- A TNS alias such as `STDPDB1` unchanged.
- An Easy Connect form such as `host:1521/service` by adding the `//` prefix.
- An already prefixed Easy Connect string, URL, or full `(DESCRIPTION=...)`
  descriptor unchanged.

The fixed buffer is checked before constructing the string. OCI receives the
connect string through `OCILogon2` as a counted byte string, not as a shell
command.

## Creating OCI handles and logging in

`OCIEnvCreate` initializes the OCI environment. That environment is the parent
for OCI handles created later. `OCIHandleAlloc` creates an `OCIError` handle;
that error handle is passed to most OCI operations so Oracle can return detailed
diagnostics.

`OCILogon2` creates the service context (`svc`) used by statements and
transactions. `OCI_DEFAULT` selects the ordinary direct logon mode. Once the
call returns, the program clears its local password buffer.

Handles are initialized to null at the start of `main`. This lets the cleanup
block safely release only those resources that were successfully created.

## Truncating the benchmark table

The program allocates an `OCIStmt` handle, prepares:

```sql
TRUNCATE TABLE BSLOAD_BULK_TEST_20261009
```

with `OCIStmtPrepare`, and executes it with `OCIStmtExecute`. The table name is
fixed in the source; it is not accepted from user input. That avoids turning a
command-line value into DDL text, while also restricting the program to the
dedicated table.

After truncation, the same statement handle is prepared with the parameterized
`INSERT` statement. The data values are parameters; the table and column names
are fixed SQL text.

## Binding values with `OCIBindByPos`

The insert has five positional placeholders: `:1` through `:5`. `OCIBindByPos`
associates a C address and OCI datatype with each placeholder:

| Bind | C storage | OCI datatype | Table column |
| --- | --- | --- | --- |
| `:1` | `OCINumber id` | `SQLT_VNU` | `ID` |
| `:2` | `OCINumber customer_id` | `SQLT_VNU` | `CUSTOMER_ID` |
| `:3` | `OCINumber amount` | `SQLT_VNU` | `AMOUNT` |
| `:4` | `OCIDate load_date` | `SQLT_ODT` | `LOAD_DATE` |
| `:5` | `OraText payload[21]` | `SQLT_CHR` | `PAYLOAD` |

The first four binds use a small `BIND` macro to avoid repeating the same OCI
call shape. The payload bind passes `&payload_length` as the actual character
length (`alenp`). The buffer has 21 bytes so it can hold up to 20 payload bytes
plus a C terminator, but the database receives only the payload bytes. Passing
the buffer size as the value length would include the extra byte and can raise
ORA-12899 for a 20-byte column.

The indicator, return-code, and array-length arguments are null because this
example sends one non-null row per execution. It is not an array bind or a
high-throughput implementation.

## Parsing the input file

The loop uses `fgets` to read one bounded line at a time. It detects lines that
exceed the buffer, splits the row at four `|` delimiters, strips the line
ending from the final field, and rejects missing, empty, extra, or oversized
fields. `line_no` is included in malformed-row messages.

`OCINumberFromText` converts the three text values into `OCINumber` values.
Each conversion uses a fixed format mask and an explicit numeric NLS setting
(`NLS_NUMERIC_CHARACTERS='.,'`) so decimal parsing does not depend on the
machine's locale. `OCIDateFromText` converts the ISO date using `YYYY-MM-DD`.
The payload bytes are copied into the bound character buffer, and
`payload_length` is set to the row's exact length.

## Executing, committing, and timing

For each parsed row, the program calls `OCIStmtExecute` with an iteration count
of one. OCI sends the insert to Oracle, but the row remains part of the current
transaction until `OCITransCommit` runs after the file reaches EOF. Every
100,000 successful inserts, the program prints a progress line.

Elapsed time uses `time()`/`difftime()` and includes login, truncation, file
parsing, inserts, and commit. It is a simple elapsed-time measure, not a
microbenchmark timer.

## Failure handling and cleanup

Parsing, file I/O, handle allocation, login, prepare, bind, execute, or commit
failures jump to `cleanup`. If the connection exists and the overall result is
failure, cleanup calls `OCITransRollback` for the current transaction. Since
Oracle commits `TRUNCATE` implicitly, a failure after truncation leaves the
table empty; the inserts in this program are rolled back.

Cleanup closes the input file, logs off the service context, frees the
statement, error, and environment handles, clears the password buffer, and
returns an exit status suitable for the shell script.

## Build and run

The Makefile builds `bsload_sim.c` using the Oracle Client SDK headers and
`libclntsh`:

```sh
make bsload_sim
export DB_USER=your_schema
export DB_ALIAS=host:1521/service_name
bash ./05_run_bsload.sh
```

`05_run_bsload.sh` builds the executable on demand when `BSLOAD_BIN` is unset.
The executable and source are separate: the source is portable C around OCI,
while the generated binary is specific to this operating system, architecture,
and Oracle Client installation.
