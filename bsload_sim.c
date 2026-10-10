#define _POSIX_C_SOURCE 200809L

#include <oci.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define LINE_SIZE 256
#define PASSWORD_SIZE 256

static int check_oci(sword status, OCIError *error, const char *operation)
{
    OraText message[1024];
    sb4 code;

    if (status == OCI_SUCCESS)
    {
        return 1;
    }
    if (error != (OCIError *)0 &&
        OCIErrorGet((void *)error, (ub4)1, (OraText *)0, &code, message,
                    (ub4)sizeof(message), (ub4)OCI_HTYPE_ERROR) == OCI_SUCCESS)
    {
        (void)fprintf(stderr, "%s failed (ORA-%05d): %s\n", operation,
                      (int)code, (const char *)message);
    }
    else
    {
        (void)fprintf(stderr, "%s failed with OCI status %d\n", operation,
                      (int)status);
    }
    return status == OCI_SUCCESS_WITH_INFO;
}

static int read_password(char *password, size_t capacity)
{
    FILE *tty;
    struct termios original;
    struct termios hidden;
    size_t length;
    int fd;
    int ch;
    int overflow;
    int restored;

    tty = fopen("/dev/tty", "r+");
    if (tty == (FILE *)0 || tcgetattr(fileno(tty), &original) != 0)
    {
        if (tty != (FILE *)0) (void)fclose(tty);
        (void)fprintf(stderr, "Cannot read password from /dev/tty.\n");
        return 0;
    }
    fd = fileno(tty);
    hidden = original;
    hidden.c_lflag &= (tcflag_t)~ECHO;
    if (tcsetattr(fd, TCSAFLUSH, &hidden) != 0)
    {
        (void)fclose(tty);
        (void)fprintf(stderr, "Cannot disable terminal echo.\n");
        return 0;
    }
    (void)fprintf(tty, "Oracle password: ");
    (void)fflush(tty);
    length = 0U;
    overflow = 0;
    while ((ch = fgetc(tty)) != EOF && ch != '\n')
    {
        if (ch != '\r')
        {
            if (length + 1U < capacity) password[length++] = (char)ch;
            else overflow = 1;
        }
    }
    password[length] = '\0';
    restored = tcsetattr(fd, TCSAFLUSH, &original);
    (void)fprintf(tty, "\n");
    (void)fclose(tty);
    if (ch == EOF || restored != 0 || overflow != 0)
    {
        (void)memset(password, 0, capacity);
        (void)fprintf(stderr, "Could not read the full password.\n");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    OCIEnv *env = (OCIEnv *)0;
    OCIError *err = (OCIError *)0;
    OCISvcCtx *svc = (OCISvcCtx *)0;
    OCIStmt *stmt = (OCIStmt *)0;
    OCIBind *bind = (OCIBind *)0;
    ub2 payload_length;
    FILE *input = (FILE *)0;
    char password[PASSWORD_SIZE];
    char connect[1024];
    char line[LINE_SIZE];
    char *field[5];
    char *sep;
    size_t connect_len;
    size_t field_len;
    ub4 line_no;
    ub4 loaded;
    OCINumber id;
    OCINumber customer_id;
    OCINumber amount;
    OCIDate load_date;
    OraText payload[21];
    static OraText id_fmt[] = "9999999999";
    static OraText customer_fmt[] = "9999999";
    static OraText amount_fmt[] = "99999999D99";
    static OraText date_fmt[] = "YYYY-MM-DD";
    static OraText nls[] = "NLS_NUMERIC_CHARACTERS='.,'";
    static OraText insert_sql[] =
        "INSERT INTO BSLOAD_BULK_TEST_20261009 "
        "(ID,CUSTOMER_ID,AMOUNT,LOAD_DATE,PAYLOAD) VALUES (:1,:2,:3,:4,:5)";
    time_t started;
    int rc;
    int i;
    sword status;

    password[0] = '\0';
    loaded = (ub4)0;
    rc = EXIT_FAILURE;
    if (argc != 2 || getenv("DB_USER") == (char *)0 ||
        getenv("DB_ALIAS") == (char *)0)
    {
        (void)fprintf(stderr,
                      "Usage: DB_USER=name DB_ALIAS=service %s DATA_FILE\n",
                      argv[0]);
        return EXIT_FAILURE;
    }
    input = fopen(argv[1], "r");
    if (input == (FILE *)0)
    {
        (void)fprintf(stderr, "Cannot open data file: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    connect_len = strlen(getenv("DB_ALIAS"));
    if (connect_len + 3U > sizeof(connect))
    {
        (void)fprintf(stderr, "DB_ALIAS is too long.\n");
        goto cleanup;
    }
    if (getenv("DB_ALIAS")[0] == '(' ||
        strncmp(getenv("DB_ALIAS"), "//", 2U) == 0 ||
        strstr(getenv("DB_ALIAS"), "://") != (char *)0 ||
        strchr(getenv("DB_ALIAS"), '/') == (char *)0)
    {
        (void)snprintf(connect, sizeof(connect), "%s", getenv("DB_ALIAS"));
    }
    else if (strchr(getenv("DB_ALIAS"), '/') != (char *)0)
    {
        (void)snprintf(connect, sizeof(connect), "//%s", getenv("DB_ALIAS"));
    }
    if (!read_password(password, sizeof(password))) goto cleanup;
    started = time((time_t *)0);
    status = OCIEnvCreate(&env, (ub4)OCI_DEFAULT, (void *)0,
                          (void *(*)(void *, size_t))0,
                          (void *(*)(void *, void *, size_t))0,
                          (void (*)(void *, void *))0, (size_t)0,
                          (void **)0);
    if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO)
    {
        (void)fprintf(stderr, "OCIEnvCreate failed with status %d\n", (int)status);
        goto cleanup;
    }
    status = OCIHandleAlloc((void *)env, (void **)&err, (ub4)OCI_HTYPE_ERROR,
                            (size_t)0, (void **)0);
    if (!check_oci(status, err, "OCIHandleAlloc(error)")) goto cleanup;
    status = OCILogon2(env, err, &svc, (OraText *)getenv("DB_USER"),
                       (ub4)strlen(getenv("DB_USER")), (OraText *)password,
                       (ub4)strlen(password), (OraText *)connect,
                       (ub4)strlen(connect), (ub4)OCI_DEFAULT);
    (void)memset(password, 0, sizeof(password));
    if (!check_oci(status, err, "OCILogon2")) goto cleanup;

    status = OCIHandleAlloc((void *)env, (void **)&stmt, (ub4)OCI_HTYPE_STMT,
                            (size_t)0, (void **)0);
    if (!check_oci(status, err, "OCIHandleAlloc(statement)")) goto cleanup;
    status = OCIStmtPrepare(stmt, err,
                            (OraText *)"TRUNCATE TABLE BSLOAD_BULK_TEST_20261009",
                            (ub4)strlen("TRUNCATE TABLE BSLOAD_BULK_TEST_20261009"),
                            (ub4)OCI_NTV_SYNTAX, (ub4)OCI_DEFAULT);
    if (!check_oci(status, err, "OCIStmtPrepare(TRUNCATE)")) goto cleanup;
    status = OCIStmtExecute(svc, stmt, err, (ub4)1, (ub4)0,
                            (OCISnapshot *)0, (OCISnapshot *)0, (ub4)OCI_DEFAULT);
    if (!check_oci(status, err, "OCIStmtExecute(TRUNCATE)")) goto cleanup;
    status = OCIStmtPrepare(stmt, err, insert_sql,
                            (ub4)(sizeof(insert_sql) - 1U),
                            (ub4)OCI_NTV_SYNTAX, (ub4)OCI_DEFAULT);
    if (!check_oci(status, err, "OCIStmtPrepare(INSERT)")) goto cleanup;

#define BIND(position, value, size, type) \
    status = OCIBindByPos(stmt, &bind, err, (ub4)(position), (void *)(value), \
                          (sb4)(size), (ub2)(type), (void *)0, (ub2 *)0, \
                          (ub2 *)0, (ub4)0, (ub4 *)0, (ub4)OCI_DEFAULT); \
    if (!check_oci(status, err, "OCIBindByPos")) goto cleanup
    BIND(1, &id, sizeof(id), SQLT_VNU);
    BIND(2, &customer_id, sizeof(customer_id), SQLT_VNU);
    BIND(3, &amount, sizeof(amount), SQLT_VNU);
    BIND(4, &load_date, sizeof(load_date), SQLT_ODT);
    status = OCIBindByPos(stmt, &bind, err, (ub4)5, (void *)payload,
                          (sb4)sizeof(payload), (ub2)SQLT_CHR, (void *)0,
                          &payload_length, (ub2 *)0, (ub4)0, (ub4 *)0,
                          (ub4)OCI_DEFAULT);
    if (!check_oci(status, err, "OCIBindByPos(PAYLOAD)")) goto cleanup;
#undef BIND

    line_no = (ub4)0;
    while (fgets(line, sizeof(line), input) != (char *)0)
    {
        line_no++;
        if (strchr(line, '\n') == (char *)0 && feof(input) == 0)
        {
            (void)fprintf(stderr, "Line %u is too long.\n", (unsigned int)line_no);
            goto cleanup;
        }
        field[0] = line;
        for (i = 0; i < 4; i++)
        {
            sep = strchr(field[i], '|');
            if (sep == (char *)0) goto bad_line;
            *sep = '\0';
            field[i + 1] = sep + 1;
        }
        field[4][strcspn(field[4], "\r\n")] = '\0';
        if (strchr(field[4], '|') != (char *)0) goto bad_line;
        for (i = 0; i < 5; i++) if (*field[i] == '\0') goto bad_line;
        field_len = strlen(field[4]);
        if (field_len > 20U) goto bad_line;

        status = OCINumberFromText(err, (OraText *)field[0],
                                   (ub4)strlen(field[0]), id_fmt,
                                   (ub4)(sizeof(id_fmt) - 1U), nls,
                                   (ub4)(sizeof(nls) - 1U), &id);
        if (!check_oci(status, err, "Parse ID")) goto cleanup;
        status = OCINumberFromText(err, (OraText *)field[1],
                                   (ub4)strlen(field[1]), customer_fmt,
                                   (ub4)(sizeof(customer_fmt) - 1U), nls,
                                   (ub4)(sizeof(nls) - 1U), &customer_id);
        if (!check_oci(status, err, "Parse CUSTOMER_ID")) goto cleanup;
        status = OCINumberFromText(err, (OraText *)field[2],
                                   (ub4)strlen(field[2]), amount_fmt,
                                   (ub4)(sizeof(amount_fmt) - 1U), nls,
                                   (ub4)(sizeof(nls) - 1U), &amount);
        if (!check_oci(status, err, "Parse AMOUNT")) goto cleanup;
        status = OCIDateFromText(err, (OraText *)field[3],
                                 (ub4)strlen(field[3]), date_fmt,
                                 (ub4)(sizeof(date_fmt) - 1U),
                                 (OraText *)0, (ub4)0, &load_date);
        if (!check_oci(status, err, "Parse LOAD_DATE")) goto cleanup;
        (void)memset(payload, 0, sizeof(payload));
        (void)memcpy(payload, field[4], field_len);
        payload_length = (ub2)field_len;
        status = OCIStmtExecute(svc, stmt, err, (ub4)1, (ub4)0,
                                (OCISnapshot *)0, (OCISnapshot *)0,
                                (ub4)OCI_DEFAULT);
        if (!check_oci(status, err, "OCIStmtExecute(INSERT)")) goto cleanup;
        loaded++;
        if (loaded % (ub4)100000 == (ub4)0)
        {
            (void)fprintf(stdout, "Inserted %u rows\n", (unsigned int)loaded);
        }
        continue;
bad_line:
        (void)fprintf(stderr, "Malformed input at line %u.\n",
                      (unsigned int)line_no);
        goto cleanup;
    }
    if (ferror(input) != 0)
    {
        (void)fprintf(stderr, "Error reading input file.\n");
        goto cleanup;
    }
    status = OCITransCommit(svc, err, (ub4)OCI_DEFAULT);
    if (!check_oci(status, err, "OCITransCommit")) goto cleanup;
    (void)fprintf(stdout, "OCI C simulator loaded %u rows; elapsed seconds: %.0f\n",
                  (unsigned int)loaded,
                  difftime(time((time_t *)0), started));
    rc = EXIT_SUCCESS;

cleanup:
    (void)memset(password, 0, sizeof(password));
    if (input != (FILE *)0) (void)fclose(input);
    if (svc != (OCISvcCtx *)0)
    {
        if (rc != EXIT_SUCCESS)
            (void)OCITransRollback(svc, err, (ub4)OCI_DEFAULT);
        (void)OCILogoff(svc, err);
    }
    if (stmt != (OCIStmt *)0) (void)OCIHandleFree((void *)stmt, (ub4)OCI_HTYPE_STMT);
    if (err != (OCIError *)0) (void)OCIHandleFree((void *)err, (ub4)OCI_HTYPE_ERROR);
    if (env != (OCIEnv *)0) (void)OCIHandleFree((void *)env, (ub4)OCI_HTYPE_ENV);
    return rc;
}
