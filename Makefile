VERSION=1.5.1

CFLAGS+=-Wall -Wextra -O2

# _GNU_SOURCE is for asprintf
EXTRA_CFLAGS= -D_GNU_SOURCE -DPROGRAM_VERSION=$(VERSION) -DBUILD_TIME=$(shell date +%s)

ifeq ($(shell uname),Darwin)
EXTRA_CFLAGS+=-Isrc/compat
EXTRA_SRC=src/compat/compat.c
endif

erlinit: $(wildcard src/*.c) $(EXTRA_SRC)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $^

fixture:
	$(MAKE) -C tests/fixture

test: check
check: erlinit fixture
	tests/run_tests.sh

clean:
	-rm -fr erlinit tests/work
	$(MAKE) -C tests/fixture clean

.PHONY: test clean check fixture
