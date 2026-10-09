-- Run after both loads finish. These checks are outside the timed load runs.
SET PAGESIZE 100
SET LINESIZE 220
SET NUMWIDTH 20

SELECT 'SQLLDR' AS loader,
       COUNT(*) AS row_count,
       COUNT(DISTINCT id) AS distinct_ids,
       MIN(id) AS min_id,
       MAX(id) AS max_id,
       SUM(customer_id) AS customer_id_sum,
       SUM(amount) AS amount_sum,
       SUM(ORA_HASH(id || '|' || customer_id || '|' ||
                    TO_CHAR(amount, 'FM99999990D00', 'NLS_NUMERIC_CHARACTERS=''.,''') || '|' ||
                    TO_CHAR(load_date, 'YYYY-MM-DD') || '|' || payload)) AS row_hash_sum
FROM SQLLDR_BULK_TEST_20261009
UNION ALL
SELECT 'BSLOAD',
       COUNT(*),
       COUNT(DISTINCT id),
       MIN(id),
       MAX(id),
       SUM(customer_id),
       SUM(amount),
       SUM(ORA_HASH(id || '|' || customer_id || '|' ||
                    TO_CHAR(amount, 'FM99999990D00', 'NLS_NUMERIC_CHARACTERS=''.,''') || '|' ||
                    TO_CHAR(load_date, 'YYYY-MM-DD') || '|' || payload))
FROM BSLOAD_BULK_TEST_20261009;

EXIT SUCCESS
