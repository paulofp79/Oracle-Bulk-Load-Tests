-- Run while connected to the schema that will own the test tables.
-- The two tables intentionally have the same definition and are LOGGING.
-- They have no indexes, constraints, or triggers so the load methods are
-- compared on the same minimal heap-table workload.

WHENEVER SQLERROR EXIT SQL.SQLCODE

CREATE TABLE SQLLDR_BULK_TEST_20261009 (
    ID          NUMBER(10)     NOT NULL,
    CUSTOMER_ID NUMBER(7)      NOT NULL,
    AMOUNT      NUMBER(10,2)   NOT NULL,
    LOAD_DATE   DATE           NOT NULL,
    PAYLOAD     VARCHAR2(20)   NOT NULL
) LOGGING;

CREATE TABLE BSLOAD_BULK_TEST_20261009 (
    ID          NUMBER(10)     NOT NULL,
    CUSTOMER_ID NUMBER(7)      NOT NULL,
    AMOUNT      NUMBER(10,2)   NOT NULL,
    LOAD_DATE   DATE           NOT NULL,
    PAYLOAD     VARCHAR2(20)   NOT NULL
) LOGGING;

PROMPT Created SQLLDR_BULK_TEST_20261009 and BSLOAD_BULK_TEST_20261009.
EXIT SUCCESS
