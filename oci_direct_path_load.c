#define _POSIX_C_SOURCE 200809L

#include <oci.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define OCI_DP_COLUMN_COUNT 5
#define OCI_DP_BATCH_ROWS 5000
#define OCI_DP_STREAM_BYTES (8U * 1024U * 1024U)
#define OCI_DP_LINE_BYTES 256
#define OCI_DP_PASSWORD_BYTES 256

static OraText oci_dp_table_name[] = "BSLOAD_BULK_TEST_20261009";
static OraText oci_dp_column_names[OCI_DP_COLUMN_COUNT][16] = {
    "ID", "CUSTOMER_ID", "AMOUNT", "LOAD_DATE", "PAYLOAD"
};
static ub2 oci_dp_column_types[OCI_DP_COLUMN_COUNT] = {
    SQLT_VNU, SQLT_VNU, SQLT_VNU, SQLT_CHR, SQLT_CHR
};
static ub4 oci_dp_column_sizes[OCI_DP_COLUMN_COUNT] = {
    sizeof(OCINumber), sizeof(OCINumber), sizeof(OCINumber),
    10U, 20U
};

typedef struct oci_dp_row
{
    OCINumber id;
    OCINumber customer_id;
    OCINumber amount;
    OraText load_date[10];
    ub4 load_date_length;
    OraText payload[20];
    ub4 payload_length;
} oci_dp_row;

static void oci_dp_report_error(OCIError *error_handle, const char *operation)
{
    OraText message[1024];
    sb4 error_code;
    sword status;

    error_code = 0;
    message[0] = (OraText)'\0';
    if (error_handle == (OCIError *)0)
    {
        (void)fprintf(stderr, "%s failed (no OCI error handle; status unavailable)\n",
                      operation);
        return;
    }

    status = OCIErrorGet((void *)error_handle, (ub4)1, (OraText *)0,
                         &error_code, message, (ub4)sizeof(message),
                         (ub4)OCI_HTYPE_ERROR);
    if (status == OCI_SUCCESS)
    {
        (void)fprintf(stderr, "%s failed (ORA-%05d): %s\n", operation,
                      (int)error_code, (const char *)message);
    }
    else
    {
        (void)fprintf(stderr, "%s failed with OCI status %d\n", operation,
                      (int)status);
    }
}

static int oci_dp_check(sword status, OCIError *error_handle,
                        const char *operation)
{
    if (status == OCI_SUCCESS)
    {
        return 1;
    }
    if (status == OCI_SUCCESS_WITH_INFO)
    {
        oci_dp_report_error(error_handle, operation);
        return 1;
    }
    oci_dp_report_error(error_handle, operation);
    return 0;
}

static int oci_dp_read_password(char *password, size_t password_capacity)
{
    FILE *terminal;
    struct termios original_attributes;
    struct termios hidden_attributes;
    size_t password_length;
    int terminal_fd;
    int input_char;
    int overflow;
    int result;

    terminal = fopen("/dev/tty", "r+");
    if (terminal == (FILE *)0)
    {
        (void)fprintf(stderr,
                      "Cannot open /dev/tty to read the database password.\n");
        return 0;
    }

    terminal_fd = fileno(terminal);
    if (tcgetattr(terminal_fd, &original_attributes) != 0)
    {
        (void)fprintf(stderr, "Cannot read terminal settings.\n");
        (void)fclose(terminal);
        return 0;
    }

    hidden_attributes = original_attributes;
    hidden_attributes.c_lflag &= (tcflag_t)~ECHO;
    if (tcsetattr(terminal_fd, TCSAFLUSH, &hidden_attributes) != 0)
    {
        (void)fprintf(stderr, "Cannot disable terminal echo.\n");
        (void)fclose(terminal);
        return 0;
    }

    (void)fprintf(terminal, "Oracle password: ");
    (void)fflush(terminal);
    password_length = 0U;
    overflow = 0;
    while ((input_char = fgetc(terminal)) != EOF && input_char != '\n')
    {
        if (input_char != '\r')
        {
            if (password_length + 1U < password_capacity)
            {
                password[password_length] = (char)input_char;
                password_length++;
            }
            else
            {
                overflow = 1;
            }
        }
    }
    password[password_length] = '\0';

    result = tcsetattr(terminal_fd, TCSAFLUSH, &original_attributes);
    (void)fprintf(terminal, "\n");
    (void)fclose(terminal);

    if (input_char == EOF || result != 0 || overflow != 0)
    {
        (void)memset(password, 0, password_capacity);
        (void)fprintf(stderr, "Could not read the full database password.\n");
        return 0;
    }
    return 1;
}

static int oci_dp_build_connect_identifier(const char *connect_name,
                                           char *result,
                                           size_t result_capacity)
{
    size_t connect_length;

    connect_length = strlen(connect_name);
    if (connect_length == 0U)
    {
        (void)fprintf(stderr, "DB_ALIAS must not be empty.\n");
        return 0;
    }

    if (connect_name[0] == '(' || strncmp(connect_name, "//", 2U) == 0 ||
        strstr(connect_name, "://") != (char *)0 ||
        strchr(connect_name, '/') == (char *)0)
    {
        if (connect_length >= result_capacity)
        {
            (void)fprintf(stderr, "DB_ALIAS is too long.\n");
            return 0;
        }
        (void)memcpy(result, connect_name, connect_length + 1U);
        return 1;
    }

    if (connect_length + 3U > result_capacity)
    {
        (void)fprintf(stderr, "DB_ALIAS is too long.\n");
        return 0;
    }
    result[0] = '/';
    result[1] = '/';
    (void)memcpy(result + 2, connect_name, connect_length + 1U);
    return 1;
}

static int oci_dp_exec_ddl(OCIEnv *environment, OCIError *error_handle,
                           OCISvcCtx *service_context, const char *sql_text)
{
    OCIStmt *statement;
    sword status;

    statement = (OCIStmt *)0;
    status = OCIHandleAlloc((void *)environment, (void **)&statement,
                            (ub4)OCI_HTYPE_STMT, (size_t)0, (void **)0);
    if (!oci_dp_check(status, error_handle, "OCIHandleAlloc(statement)"))
    {
        return 0;
    }

    status = OCIStmtPrepare(statement, error_handle,
                            (const OraText *)sql_text,
                            (ub4)strlen(sql_text), (ub4)OCI_NTV_SYNTAX,
                            (ub4)OCI_DEFAULT);
    if (!oci_dp_check(status, error_handle, "OCIStmtPrepare"))
    {
        (void)OCIHandleFree((void *)statement, (ub4)OCI_HTYPE_STMT);
        return 0;
    }

    status = OCIStmtExecute(service_context, statement, error_handle,
                            (ub4)1, (ub4)0, (const OCISnapshot *)0,
                            (OCISnapshot *)0, (ub4)OCI_DEFAULT);
    if (!oci_dp_check(status, error_handle, "OCIStmtExecute"))
    {
        (void)OCIHandleFree((void *)statement, (ub4)OCI_HTYPE_STMT);
        return 0;
    }

    (void)OCIHandleFree((void *)statement, (ub4)OCI_HTYPE_STMT);
    return 1;
}

static int oci_dp_set_context_attributes(OCIError *error_handle,
                                         OCIDirPathCtx *direct_path,
                                         OCIParam **column_list)
{
    ub2 column_count;
    ub2 row_capacity;
    ub4 stream_bytes;
    sword status;

    column_count = (ub2)OCI_DP_COLUMN_COUNT;
    row_capacity = (ub2)OCI_DP_BATCH_ROWS;
    stream_bytes = (ub4)OCI_DP_STREAM_BYTES;

    status = OCIAttrSet((void *)direct_path, (ub4)OCI_HTYPE_DIRPATH_CTX,
                        (void *)oci_dp_table_name,
                        (ub4)strlen((const char *)oci_dp_table_name),
                        (ub4)OCI_ATTR_NAME, error_handle);
    if (!oci_dp_check(status, error_handle, "OCIAttrSet(table name)"))
    {
        return 0;
    }

    status = OCIAttrSet((void *)direct_path, (ub4)OCI_HTYPE_DIRPATH_CTX,
                        (void *)&column_count, (ub4)0,
                        (ub4)OCI_ATTR_NUM_COLS, error_handle);
    if (!oci_dp_check(status, error_handle, "OCIAttrSet(column count)"))
    {
        return 0;
    }

    status = OCIAttrSet((void *)direct_path, (ub4)OCI_HTYPE_DIRPATH_CTX,
                        (void *)&row_capacity, (ub4)0,
                        (ub4)OCI_ATTR_NUM_ROWS, error_handle);
    if (!oci_dp_check(status, error_handle, "OCIAttrSet(row capacity)"))
    {
        return 0;
    }

    status = OCIAttrSet((void *)direct_path, (ub4)OCI_HTYPE_DIRPATH_CTX,
                        (void *)&stream_bytes, (ub4)0,
                        (ub4)OCI_ATTR_BUF_SIZE, error_handle);
    if (!oci_dp_check(status, error_handle, "OCIAttrSet(stream size)"))
    {
        return 0;
    }

    status = OCIAttrGet((const void *)direct_path,
                        (ub4)OCI_HTYPE_DIRPATH_CTX,
                        (void *)column_list, (ub4 *)0,
                        (ub4)OCI_ATTR_LIST_COLUMNS, error_handle);
    return oci_dp_check(status, error_handle,
                        "OCIAttrGet(direct path columns)");
}

static int oci_dp_set_column_attributes(OCIError *error_handle,
                                        OCIParam *column_list)
{
    static OraText date_format[] = "YYYY-MM-DD";
    OCIParam *column_descriptor;
    ub4 column_position;
    sword status;
    int column_index;
    int result;

    result = 1;
    for (column_index = 0; column_index < OCI_DP_COLUMN_COUNT;
         column_index++)
    {
        column_descriptor = (OCIParam *)0;
        column_position = (ub4)(column_index + 1);
        status = OCIParamGet((const void *)column_list,
                             (ub4)OCI_DTYPE_PARAM, error_handle,
                             (void **)&column_descriptor, column_position);
        if (!oci_dp_check(status, error_handle, "OCIParamGet(column)"))
        {
            result = 0;
            break;
        }

        status = OCIAttrSet((void *)column_descriptor,
                            (ub4)OCI_DTYPE_PARAM,
                            (void *)oci_dp_column_names[column_index],
                            (ub4)strlen((const char *)
                                        oci_dp_column_names[column_index]),
                            (ub4)OCI_ATTR_NAME, error_handle);
        if (oci_dp_check(status, error_handle,
                         "OCIAttrSet(column name)"))
        {
            status = OCIAttrSet((void *)column_descriptor,
                                (ub4)OCI_DTYPE_PARAM,
                                (void *)&oci_dp_column_types[column_index],
                                (ub4)0, (ub4)OCI_ATTR_DATA_TYPE,
                                error_handle);
        }
        if (oci_dp_check(status, error_handle,
                         "OCIAttrSet(column type)"))
        {
            status = OCIAttrSet((void *)column_descriptor,
                                (ub4)OCI_DTYPE_PARAM,
                                (void *)&oci_dp_column_sizes[column_index],
                                (ub4)0, (ub4)OCI_ATTR_DATA_SIZE,
                                error_handle);
        }
        if (oci_dp_check(status, error_handle,
                         "OCIAttrSet(column data size)") && column_index == 3)
        {
            status = OCIAttrSet((void *)column_descriptor,
                                (ub4)OCI_DTYPE_PARAM,
                                (void *)date_format,
                                (ub4)(sizeof(date_format) - 1U),
                                (ub4)OCI_ATTR_DATEFORMAT, error_handle);
        }
        if (!oci_dp_check(status, error_handle,
                          column_index == 3 ?
                          "OCIAttrSet(date format)" :
                          "OCIAttrSet(column data size)"))
        {
            result = 0;
            (void)OCIDescriptorFree((void *)column_descriptor,
                                    (ub4)OCI_DTYPE_PARAM);
            break;
        }

        status = OCIDescriptorFree((void *)column_descriptor,
                                  (ub4)OCI_DTYPE_PARAM);
        if (!oci_dp_check(status, error_handle,
                          "OCIDescriptorFree(column)"))
        {
            result = 0;
            break;
        }
    }
    return result;
}

static int oci_dp_parse_number(OCIError *error_handle, const char *value,
                               const OraText *format, ub4 format_length,
                               OCINumber *number)
{
    static OraText nls_parameters[] = "NLS_NUMERIC_CHARACTERS='.,'";
    sword status;

    status = OCINumberFromText(error_handle, (const OraText *)value,
                               (ub4)strlen(value), format, format_length,
                               nls_parameters,
                               (ub4)(sizeof(nls_parameters) - 1U), number);
    return oci_dp_check(status, error_handle, "OCINumberFromText");
}

static int oci_dp_parse_record(OCIError *error_handle, char *line,
                               oci_dp_row *row, ub4 input_line_number)
{
    static OraText integer_format[] = "9999999999";
    static OraText customer_format[] = "9999999";
    static OraText amount_format[] = "99999999D99";
    static OraText date_format[] = "YYYY-MM-DD";
    char *field_start[OCI_DP_COLUMN_COUNT];
    char *separator;
    size_t field_length[OCI_DP_COLUMN_COUNT];
    size_t line_length;
    size_t field_index;
    OCIDate parsed_date;
    sword status;

    line_length = strlen(line);
    while (line_length > 0U &&
           (line[line_length - 1U] == '\n' || line[line_length - 1U] == '\r'))
    {
        line[line_length - 1U] = '\0';
        line_length--;
    }

    field_start[0] = line;
    for (field_index = 0U; field_index < OCI_DP_COLUMN_COUNT - 1U;
         field_index++)
    {
        separator = strchr(field_start[field_index], '|');
        if (separator == (char *)0)
        {
            (void)fprintf(stderr,
                          "Input line %u does not contain five fields.\n",
                          (unsigned int)input_line_number);
            return 0;
        }
        field_length[field_index] =
            (size_t)(separator - field_start[field_index]);
        *separator = '\0';
        field_start[field_index + 1U] = separator + 1;
    }
    field_length[OCI_DP_COLUMN_COUNT - 1] =
        strlen(field_start[OCI_DP_COLUMN_COUNT - 1]);
    if (strchr(field_start[OCI_DP_COLUMN_COUNT - 1], '|') != (char *)0)
    {
        (void)fprintf(stderr,
                      "Input line %u has more than five fields.\n",
                      (unsigned int)input_line_number);
        return 0;
    }

    for (field_index = 0U; field_index < OCI_DP_COLUMN_COUNT; field_index++)
    {
        if (field_length[field_index] == 0U)
        {
            (void)fprintf(stderr, "Input line %u has an empty field.\n",
                          (unsigned int)input_line_number);
            return 0;
        }
    }
    if (field_length[OCI_DP_COLUMN_COUNT - 1] > sizeof(row->payload))
    {
        (void)fprintf(stderr,
                      "Input line %u has a PAYLOAD longer than 20 bytes.\n",
                      (unsigned int)input_line_number);
        return 0;
    }

    if (!oci_dp_parse_number(error_handle, field_start[0], integer_format,
                             (ub4)(sizeof(integer_format) - 1U), &row->id) ||
        !oci_dp_parse_number(error_handle, field_start[1], customer_format,
                             (ub4)(sizeof(customer_format) - 1U),
                             &row->customer_id) ||
        !oci_dp_parse_number(error_handle, field_start[2], amount_format,
                             (ub4)(sizeof(amount_format) - 1U),
                             &row->amount))
    {
        (void)fprintf(stderr,
                      "Invalid numeric value on input line %u.\n",
                      (unsigned int)input_line_number);
        return 0;
    }

    status = OCIDateFromText(error_handle,
                             (const OraText *)field_start[3],
                             (ub4)field_length[3], date_format,
                             (ub4)(sizeof(date_format) - 1U),
                             (const OraText *)0, (ub4)0, &parsed_date);
    if (!oci_dp_check(status, error_handle, "OCIDateFromText"))
    {
        (void)fprintf(stderr,
                      "Invalid LOAD_DATE on input line %u.\n",
                      (unsigned int)input_line_number);
        return 0;
    }
    (void)memcpy(row->load_date, field_start[3], field_length[3]);
    row->load_date_length = (ub4)field_length[3];

    (void)memcpy(row->payload, field_start[4], field_length[4]);
    row->payload_length = (ub4)field_length[4];
    return 1;
}

static int oci_dp_set_row(OCIError *error_handle, OCIDirPathColArray *array,
                          ub4 row_index, const oci_dp_row *row)
{
    const void *values[OCI_DP_COLUMN_COUNT];
    ub4 value_lengths[OCI_DP_COLUMN_COUNT];
    ub2 column_index;
    sword status;

    values[0] = (const void *)&row->id;
    values[1] = (const void *)&row->customer_id;
    values[2] = (const void *)&row->amount;
    values[3] = (const void *)row->load_date;
    values[4] = (const void *)row->payload;
    value_lengths[0] = (ub4)sizeof(row->id);
    value_lengths[1] = (ub4)sizeof(row->customer_id);
    value_lengths[2] = (ub4)sizeof(row->amount);
    value_lengths[3] = row->load_date_length;
    value_lengths[4] = row->payload_length;

    for (column_index = (ub2)0; column_index < (ub2)OCI_DP_COLUMN_COUNT;
         column_index++)
    {
        status = OCIDirPathColArrayEntrySet(
            array, error_handle, row_index, column_index,
            (ub1 *)values[column_index], value_lengths[column_index],
            (ub1)OCI_DIRPATH_COL_COMPLETE);
        if (!oci_dp_check(status, error_handle,
                          "OCIDirPathColArrayEntrySet"))
        {
            return 0;
        }
    }
    return 1;
}

static int oci_dp_load_stream(OCIError *error_handle,
                              OCIDirPathCtx *direct_path,
                              OCIDirPathStream *stream)
{
    sword status;

    status = OCIDirPathLoadStream(direct_path, stream, error_handle);
    if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO ||
        status == OCI_NO_DATA)
    {
        if (status == OCI_SUCCESS_WITH_INFO)
        {
            oci_dp_report_error(error_handle, "OCIDirPathLoadStream warning");
        }
        status = OCIDirPathStreamReset(stream, error_handle);
        return oci_dp_check(status, error_handle,
                            "OCIDirPathStreamReset");
    }
    if (status == OCI_NEED_DATA)
    {
        (void)fprintf(stderr,
                      "OCI requested a partial row; this loader sends complete fields.\n");
    }
    oci_dp_report_error(error_handle, "OCIDirPathLoadStream");
    return 0;
}

static int oci_dp_flush_batch(OCIError *error_handle,
                              OCIDirPathCtx *direct_path,
                              OCIDirPathColArray *array,
                              OCIDirPathStream *stream,
                              ub4 row_count)
{
    ub4 converted_rows;
    ub4 row_offset;
    sword conversion_status;
    sword status;

    row_offset = (ub4)0;
    while (row_offset < row_count)
    {
        conversion_status = OCIDirPathColArrayToStream(
            array, direct_path, stream, error_handle, row_count, row_offset);
        converted_rows = (ub4)0;
        status = OCIAttrGet((const void *)array,
                            (ub4)OCI_HTYPE_DIRPATH_COLUMN_ARRAY,
                            (void *)&converted_rows, (ub4 *)0,
                            (ub4)OCI_ATTR_ROW_COUNT, error_handle);
        if (!oci_dp_check(status, error_handle,
                          "OCIAttrGet(converted rows)"))
        {
            return 0;
        }

        if (conversion_status == OCI_ERROR)
        {
            oci_dp_report_error(error_handle,
                                "OCIDirPathColArrayToStream");
            (void)oci_dp_load_stream(error_handle, direct_path, stream);
            return 0;
        }
        if (conversion_status != OCI_SUCCESS &&
            conversion_status != OCI_SUCCESS_WITH_INFO &&
            conversion_status != OCI_CONTINUE &&
            conversion_status != OCI_NEED_DATA)
        {
            oci_dp_report_error(error_handle,
                                "OCIDirPathColArrayToStream");
            return 0;
        }
        if (converted_rows == (ub4)0)
        {
            (void)fprintf(stderr,
                          "Direct-path conversion made no row progress.\n");
            return 0;
        }
        if (!oci_dp_load_stream(error_handle, direct_path, stream))
        {
            return 0;
        }

        row_offset += converted_rows;
        if (conversion_status == OCI_NEED_DATA)
        {
            (void)fprintf(stderr,
                          "OCI requested partial-column input, which this file format does not use.\n");
            return 0;
        }
    }

    status = OCIDirPathColArrayReset(array, error_handle);
    return oci_dp_check(status, error_handle,
                        "OCIDirPathColArrayReset");
}

static int oci_dp_save_batch(OCIError *error_handle,
                             OCIDirPathCtx *direct_path)
{
    sword status;

    status = OCIDirPathDataSave(direct_path, error_handle,
                                (ub4)OCI_DIRPATH_DATASAVE_SAVEONLY);
    return oci_dp_check(status, error_handle, "OCIDirPathDataSave");
}

static void oci_dp_cleanup(OCIEnv *environment, OCIError *error_handle,
                           OCISvcCtx *service_context,
                           OCIDirPathCtx *direct_path,
                           OCIDirPathColArray *column_array,
                           OCIDirPathStream *stream, int prepared,
                           int finished)
{
    if (direct_path != (OCIDirPathCtx *)0 && prepared != 0 && finished == 0)
    {
        sword status;

        status = OCIDirPathAbort(direct_path, error_handle);
        if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO)
        {
            oci_dp_report_error(error_handle, "OCIDirPathAbort");
        }
    }
    if (column_array != (OCIDirPathColArray *)0)
    {
        (void)OCIHandleFree((void *)column_array,
                            (ub4)OCI_HTYPE_DIRPATH_COLUMN_ARRAY);
    }
    if (stream != (OCIDirPathStream *)0)
    {
        (void)OCIHandleFree((void *)stream, (ub4)OCI_HTYPE_DIRPATH_STREAM);
    }
    if (direct_path != (OCIDirPathCtx *)0)
    {
        (void)OCIHandleFree((void *)direct_path,
                            (ub4)OCI_HTYPE_DIRPATH_CTX);
    }
    if (service_context != (OCISvcCtx *)0)
    {
        (void)OCILogoff(service_context, error_handle);
    }
    if (error_handle != (OCIError *)0)
    {
        (void)OCIHandleFree((void *)error_handle, (ub4)OCI_HTYPE_ERROR);
    }
    if (environment != (OCIEnv *)0)
    {
        (void)OCIHandleFree((void *)environment, (ub4)OCI_HTYPE_ENV);
    }
}

int main(int argc, char **argv)
{
    OCIEnv *environment;
    OCIError *error_handle;
    OCISvcCtx *service_context;
    OCIDirPathCtx *direct_path;
    OCIDirPathColArray *column_array;
    OCIDirPathStream *stream;
    OCIParam *column_list;
    oci_dp_row *rows;
    FILE *input_file;
    char *line;
    char password[OCI_DP_PASSWORD_BYTES];
    char connect_identifier[1024];
    const char *database_user;
    const char *database_alias;
    const char *input_path;
    ub4 batch_count;
    ub4 input_line_number;
    ub4 loaded_row_count;
    ub4 context_row_count;
    time_t start_time;
    time_t finish_time;
    sword status;
    int prepared;
    int finished;
    int result;

    environment = (OCIEnv *)0;
    error_handle = (OCIError *)0;
    service_context = (OCISvcCtx *)0;
    direct_path = (OCIDirPathCtx *)0;
    column_array = (OCIDirPathColArray *)0;
    stream = (OCIDirPathStream *)0;
    column_list = (OCIParam *)0;
    rows = (oci_dp_row *)0;
    input_file = (FILE *)0;
    line = (char *)0;
    password[0] = '\0';
    batch_count = (ub4)0;
    input_line_number = (ub4)0;
    loaded_row_count = (ub4)0;
    context_row_count = (ub4)0;
    prepared = 0;
    finished = 0;
    result = EXIT_FAILURE;

    if (argc != 4)
    {
        (void)fprintf(stderr,
                      "Usage: %s DB_USER DB_ALIAS DATA_FILE\n",
                      argv[0]);
        return EXIT_FAILURE;
    }
    database_user = argv[1];
    database_alias = argv[2];
    input_path = argv[3];
    if (database_user[0] == '\0' ||
        !oci_dp_build_connect_identifier(database_alias,
                                         connect_identifier,
                                         sizeof(connect_identifier)))
    {
        return EXIT_FAILURE;
    }

    input_file = fopen(input_path, "r");
    if (input_file == (FILE *)0)
    {
        (void)fprintf(stderr, "Cannot open input file: %s\n", input_path);
        return EXIT_FAILURE;
    }
    line = (char *)malloc((size_t)OCI_DP_LINE_BYTES);
    rows = (oci_dp_row *)calloc((size_t)OCI_DP_BATCH_ROWS, sizeof(*rows));
    if (line == (char *)0 || rows == (oci_dp_row *)0)
    {
        (void)fprintf(stderr, "Cannot allocate loader buffers.\n");
        goto cleanup;
    }

    (void)fprintf(stdout,
                  "OCI Direct Path target: %s (will be truncated)\n",
                  (const char *)oci_dp_table_name);
    (void)fprintf(stdout, "Input file: %s\n", input_path);
    if (!oci_dp_read_password(password, sizeof(password)))
    {
        goto cleanup;
    }

    start_time = time((time_t *)0);
    status = OCIEnvCreate(&environment, (ub4)OCI_DEFAULT, (void *)0,
                          (void *(*)(void *, size_t))0,
                          (void *(*)(void *, void *, size_t))0,
                          (void (*)(void *, void *))0, (size_t)0,
                          (void **)0);
    if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO)
    {
        (void)fprintf(stderr, "OCIEnvCreate failed with OCI status %d\n",
                      (int)status);
        goto cleanup;
    }
    status = OCIHandleAlloc((void *)environment, (void **)&error_handle,
                            (ub4)OCI_HTYPE_ERROR, (size_t)0, (void **)0);
    if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO)
    {
        (void)fprintf(stderr,
                      "OCIHandleAlloc(error) failed with OCI status %d\n",
                      (int)status);
        goto cleanup;
    }

    status = OCILogon2(environment, error_handle, &service_context,
                       (const OraText *)database_user,
                       (ub4)strlen(database_user),
                       (const OraText *)password,
                       (ub4)strlen(password),
                       (const OraText *)connect_identifier,
                       (ub4)strlen(connect_identifier),
                       (ub4)OCI_DEFAULT);
    (void)memset(password, 0, sizeof(password));
    if (!oci_dp_check(status, error_handle, "OCILogon2"))
    {
        goto cleanup;
    }

    if (!oci_dp_exec_ddl(environment, error_handle, service_context,
                         "TRUNCATE TABLE BSLOAD_BULK_TEST_20261009"))
    {
        goto cleanup;
    }

    status = OCIHandleAlloc((void *)environment, (void **)&direct_path,
                            (ub4)OCI_HTYPE_DIRPATH_CTX, (size_t)0,
                            (void **)0);
    if (!oci_dp_check(status, error_handle,
                      "OCIHandleAlloc(direct path context)"))
    {
        goto cleanup;
    }
    if (!oci_dp_set_context_attributes(error_handle, direct_path,
                                       &column_list) ||
        !oci_dp_set_column_attributes(error_handle, column_list))
    {
        goto cleanup;
    }

    status = OCIDirPathPrepare(direct_path, service_context, error_handle);
    if (!oci_dp_check(status, error_handle, "OCIDirPathPrepare"))
    {
        goto cleanup;
    }
    prepared = 1;

    status = OCIHandleAlloc((void *)direct_path, (void **)&column_array,
                            (ub4)OCI_HTYPE_DIRPATH_COLUMN_ARRAY, (size_t)0,
                            (void **)0);
    if (!oci_dp_check(status, error_handle,
                      "OCIHandleAlloc(column array)"))
    {
        goto cleanup;
    }
    status = OCIHandleAlloc((void *)direct_path, (void **)&stream,
                            (ub4)OCI_HTYPE_DIRPATH_STREAM, (size_t)0,
                            (void **)0);
    if (!oci_dp_check(status, error_handle,
                      "OCIHandleAlloc(stream)"))
    {
        goto cleanup;
    }

    for (;;)
    {
        if (fgets(line, OCI_DP_LINE_BYTES, input_file) == (char *)0)
        {
            break;
        }
        input_line_number++;
        if (strchr(line, '\n') == (char *)0 && feof(input_file) == 0)
        {
            (void)fprintf(stderr,
                          "Input line %u exceeds %d bytes.\n",
                          (unsigned int)input_line_number,
                          OCI_DP_LINE_BYTES - 1);
            goto cleanup;
        }
        if (!oci_dp_parse_record(error_handle, line, &rows[batch_count],
                                 input_line_number))
        {
            goto cleanup;
        }
        if (!oci_dp_set_row(error_handle, column_array, batch_count,
                            &rows[batch_count]))
        {
            goto cleanup;
        }
        batch_count++;

        if (batch_count == (ub4)OCI_DP_BATCH_ROWS)
        {
            if (!oci_dp_flush_batch(error_handle, direct_path, column_array,
                                    stream, batch_count) ||
                !oci_dp_save_batch(error_handle, direct_path))
            {
                goto cleanup;
            }
            loaded_row_count += batch_count;
            batch_count = (ub4)0;
        }
    }
    if (ferror(input_file) != 0)
    {
        (void)fprintf(stderr, "Error reading input file: %s\n", input_path);
        goto cleanup;
    }
    if (batch_count > (ub4)0)
    {
        if (!oci_dp_flush_batch(error_handle, direct_path, column_array,
                                stream, batch_count) ||
            !oci_dp_save_batch(error_handle, direct_path))
        {
            goto cleanup;
        }
        loaded_row_count += batch_count;
    }

    status = OCIDirPathFinish(direct_path, error_handle);
    if (!oci_dp_check(status, error_handle, "OCIDirPathFinish"))
    {
        goto cleanup;
    }
    finished = 1;
    status = OCIAttrGet((const void *)direct_path,
                        (ub4)OCI_HTYPE_DIRPATH_CTX,
                        (void *)&context_row_count, (ub4 *)0,
                        (ub4)OCI_ATTR_NUM_ROWS, error_handle);
    if (!oci_dp_check(status, error_handle,
                      "OCIAttrGet(loaded row count)"))
    {
        goto cleanup;
    }

    finish_time = time((time_t *)0);
    (void)fprintf(stdout,
                  "OCI Direct Path rows loaded: %u; elapsed seconds: %.0f\n",
                  (unsigned int)context_row_count,
                  difftime(finish_time, start_time));
    if (context_row_count != loaded_row_count)
    {
        (void)fprintf(stderr,
                      "Client submitted %u rows but OCI reports %u loaded.\n",
                      (unsigned int)loaded_row_count,
                      (unsigned int)context_row_count);
        goto cleanup;
    }
    result = EXIT_SUCCESS;

cleanup:
    (void)memset(password, 0, sizeof(password));
    if (input_file != (FILE *)0)
    {
        (void)fclose(input_file);
    }
    free((void *)rows);
    free((void *)line);
    oci_dp_cleanup(environment, error_handle, service_context, direct_path,
                   column_array, stream, prepared, finished);
    return result;
}
