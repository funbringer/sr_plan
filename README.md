# Save and restore query plans in PostgreSQL

## Rationale

sr_plan is similar to Oracle Outline system: it can be used to lock the execution plan for a tricky query. It is necessary if you do not trust the planner or able to form a better plan.

## Build

Supported versions of PostgreSQL: 9.5, 9.6, 10.

Dependencies: python 2.7 or 3.2+, mako, pycparser.

You might want to use virtual environment:
```bash
virtualenv env
source ./env/bin/activate
pip install -r ./requirements.txt
```

After that, compile and install sr_plan:
```bash
make USE_PGXS=1 install
```

Finally, add the following line to your `postgresql.conf`:
```
shared_preload_libraries = 'sr_plan'
```

## Usage
In your db:
```SQL
CREATE EXTENSION sr_plan;
```
If you want to save the query plan is necessary to set the variable:
```SQL
set sr_plan.write_mode = true;
```
Now plans for all subsequent queries will be stored in the table sr_plans. Don't forget that all queries will be stored including duplicates.
Making an example query:
```SQL
select query_hash from sr_plans where query_hash=10;
```
disable saving the query:
```SQL
set sr_plan.write_mode = false;
```
Now verify that your query is saved:
```SQL
select query_hash, enable, valid, query, explain_jsonb_plan(plan) from sr_plans;

 query_hash | enable | valid |                        query                         |                 explain_jsonb_plan                 
------------+--------+-------+------------------------------------------------------+----------------------------------------------------
 1783086253 | f      | t     | select query_hash from sr_plans where query_hash=10; | Bitmap Heap Scan on sr_plans                      +
            |        |       |                                                      |   Recheck Cond: (query_hash = 10)                 +
            |        |       |                                                      |   ->  Bitmap Index Scan on sr_plans_query_hash_idx+
            |        |       |                                                      |         Index Cond: (query_hash = 10)             +
            |        |       |                                                      | 

```

`explain_jsonb_plan` function shows you a pretty EXPLAINish output. By default, recorded plans are disabled, so you have to enable them:

```SQL
/* why not 1783086253? */
update sr_plans set enable=true where query_hash=1783086253;
```

After that, the plan for the query will be taken from the `sr_plans`.

In addition sr_plan allows you to save a parameterized query plan. For parameters we use a special function `_p`:

```SQL
select query_hash from sr_plans where query_hash=1000+_p(10);
```

As a result, the following queries will use the recorded plan:

```SQL
select query_hash from sr_plans where query_hash=1000+_p(11);
select query_hash from sr_plans where query_hash=1000+_p(-5);
```
