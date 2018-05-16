# contrib/sr_plan/Makefile

MODULE_big = sr_plan
OBJS = sr_plan.o $(WIN32RES)

EXTENSION = sr_plan
EXTVERSION = 1.1
PGFILEDESC = "save and reuse plans for tricky queries"

DATA_built = sr_plan--$(EXTVERSION).sql
DATA = sr_plan--1.0.sql sr_plan--1.0--1.1.sql

REGRESS = sr_plan

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/sr_plan
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

sr_plan--$(EXTVERSION).sql: $(DATA)
	cat $^ > $@
