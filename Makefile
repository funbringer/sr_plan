# contrib/sr_plan/Makefile

MODULE_big = sr_plan
PARSER_SRC = serialize.c deserialize.c
OBJS = sr_plan.o $(PARSER_SRC:.c=.o) $(WIN32RES)
PG_CPPFLAGS = -Wno-misleading-indentation  # code is ugly

EXTENSION = sr_plan
EXTVERSION = 1.1
PGFILEDESC = "sr_plan - save and reuse plans for tricky queries"

DATA_built = sr_plan--$(EXTVERSION).sql
DATA = sr_plan--1.0--1.1.sql

REGRESS = sr_plan

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
PG_SRV_INCLUDES := $(shell $(PG_CONFIG) --includedir-server)
include $(PGXS)
else
subdir = contrib/sr_plan
top_builddir = ../..
PG_SRV_INCLUDES := $(top_builddir)/src/include
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif


# additional clean step
clean: rm_parser rm_coverage

sr_plan--$(EXTVERSION).sql: $(DATA)
	cat sr_plan--1.0.sql sr_plan--1.0--1.1.sql > $@

# generate C files for parser
%.c: %.mako
	python gen_parser.py nodes.h $(PG_SRV_INCLUDES) $@

rm_parser:
	rm -f $(PARSER_SRC)

rm_coverage:
	rm -f *.gcda *.gcno *.gcov

.PHONY: rm_parser rm_coverage
