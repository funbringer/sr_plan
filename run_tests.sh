#!/usr/bin/env bash

# Copyright (c) 2018, Postgres Professional

set -eux

status=0

# set python version
VIRTUALENV="virtualenv --python=/usr/bin/python3"
PIP="pip3"

# fail early
echo check that pg_config is in PATH
command -v pg_config

# prepare environment
VENV_PATH=/tmp/sr_plan_venv
rm -rf $VENV_PATH
$VIRTUALENV $VENV_PATH
export VIRTUAL_ENV_DISABLE_PROMPT=1
source $VENV_PATH/bin/activate

# install dependencies
$PIP install -r ./requirements.txt

# perform static analyzis
scan-build make USE_PGXS=1

# don't forget to "make clean"
make USE_PGXS=1 clean

# initialize database
initdb -D /pg/data

# build and install sr_plan (using PG_CPPFLAGS and SHLIB_LINK for gcov)
make USE_PGXS=1 PG_CPPFLAGS="-coverage" SHLIB_LINK="-coverage"
make USE_PGXS=1 install

# add sr_plan to shared_preload_libraries and restart cluster 'test'
echo "shared_preload_libraries = 'sr_plan'" >> $PGDATA/postgresql.conf
echo "port = 55435" >> $PGDATA/postgresql.conf
pg_ctl start -l /tmp/postgres.log -w

# run regression tests
export PG_REGRESS_DIFF_OPTS="-w -U3" # for alpine's diff (BusyBox)
PGPORT=55435 make USE_PGXS=1 installcheck || status=$?

# show diff if it exists
if test -f regression.diffs; then cat regression.diffs; fi

# generate *.gcov files
gcov *.c *.h


# attempt to fix codecov
set +eux

# send coverage stats to Codecov
bash <(curl -s https://codecov.io/bash)
