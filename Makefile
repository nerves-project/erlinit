VERSION=1.4.1

CFLAGS+=-Wall -Wextra -O2

# _GNU_SOURCE is for asprintf
EXTRA_CFLAGS= -D_GNU_SOURCE -DPROGRAM_VERSION=$(VERSION) -DBUILD_TIME=$(shell date +%s)

erlinit: $(wildcard src/*.c)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $^

# This is a special version of erlinit that can be unit tested
erlinit-test: $(wildcard src/*.c)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -DUNITTEST -o $@ $^

test: check
check: erlinit erlinit-test
	tests/run_tests.sh

clean:
	-rm -fr erlinit erlinit-test tests/work

.PHONY: test clean check
