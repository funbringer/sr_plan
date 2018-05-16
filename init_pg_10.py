#!/usr/bin/env python

import os
import time

PG_BIN = '/home/dmitry/pg_10/bin'
os.environ['PG_BIN'] = PG_BIN

from testgres import PostgresNode

with PostgresNode(port=5432) as node:
    node.init()
    node.append_conf("postgresql.conf",
                     "shared_preload_libraries = 'pg_stat_statements, sr_plan'")
    node.start()

    node.execute('create extension pg_stat_statements')
    node.execute('create extension sr_plan')

    print('Node:\t{}'.format(node.name))
    print('Info:\t{}'.format(node.execute('select version()')[0][0]))
    print('BIN:\t{}'.format(PG_BIN))
    print('Logs:\t{}'.format(node.pg_log_name))
    print('Pid:\t{}'.format(node.pid))
    print('Port:\t{}'.format(node.port))

    print()
    print("Press ctrl+C to exit")

    while True:
        time.sleep(1)
