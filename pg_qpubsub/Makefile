EXTENSION = pg_qpubsub
DATA = pg_qpubsub--1.0.sql

REGRESS = 01_setup 02_push 03_pop 04_ack 05_notify 06_long_poll

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Build the extension SQL from source files
build:
	./build.sh

# Install after building
install: build
	$(MAKE) -f $(PGXS) install

clean:
	rm -f pg_qpubsub--1.0.sql
	$(MAKE) -f $(PGXS) clean

