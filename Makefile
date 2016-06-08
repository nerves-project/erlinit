
VERSION=0.7.3

CFLAGS+=-Wall -Wextra -O2 -DPROGRAM_VERSION=$(VERSION)

erlinit: $(wildcard src/*.c)
	$(CC) $(CFLAGS) -o $@ $^

# This is a special version of erlinit that can be unit tested
erlinit-test: $(wildcard src/*.c)
	$(CC) $(CFLAGS) -DUNITTEST -o $@ $^

test: check
check: erlinit erlinit-test
	tests/run_tests.sh

clean:
	-rm -fr erlinit erlinit-test tests/work

.PHONY: test clean check
