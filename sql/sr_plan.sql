CREATE EXTENSION sr_plan;

/* check _p() */
SELECT _p(100);
SELECT _p(36.6);
SELECT _p('abc'::text);


/* create table for tests */
CREATE TABLE test_table(test_attr1 int, test_attr2 int);
INSERT INTO test_table SELECT i, i from generate_series(1, 100) AS i;
CREATE INDEX test_table_idx_1 ON test_table (test_attr1);
CREATE INDEX test_table_idx_2 ON test_table (test_attr2);
VACUUM ANALYZE;


/* record some plan */
SET sr_plan.write_mode = true;
SET enable_seqscan = f;
SET enable_bitmapscan = f;
SET enable_indexonlyscan = f;

SELECT * FROM test_table WHERE test_attr1 = 15;
SELECT * FROM test_table WHERE test_attr2 = _p(15);

SET enable_seqscan = t;
SET enable_bitmapscan = t;
SET enable_indexonlyscan = t;
SET sr_plan.write_mode = false;


/* check plan */
SELECT	enable,
		valid,
		query,
		explain_jsonb_plan(plan)
FROM sr_plans
ORDER BY query ASC;


/* fresh plan vs stored plan */
SET enable_indexscan = f;
SET enable_bitmapscan = f;
SET enable_indexonlyscan = f;

EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr1 = _p(15);
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr1 = 10;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr1 = 15;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr1 = 20;

EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = 15;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = _p(15) OR test_attr2 = 0;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = 10;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = 15;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = 20;

UPDATE sr_plans SET enable = true RETURNING query;

EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr1 = _p(15);
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr1 = 10;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr1 = 15;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr1 = 20;

EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = 15;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = _p(15) OR test_attr2 = 0;
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = _p(10);
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = _p(15);
EXPLAIN (COSTS OFF) SELECT * FROM test_table WHERE test_attr2 = _p(20);

SET enable_indexscan = t;
SET enable_bitmapscan = t;
SET enable_indexonlyscan = t;


DROP TABLE test_table;
DROP EXTENSION sr_plan;
