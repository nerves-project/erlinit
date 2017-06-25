
VERSION=1.1.1

CFLAGS+=-Wall -Wextra -O2

erlinit: $(wildcard src/*.c)
	$(CC) $(CFLAGS) -DPROGRAM_VERSION=$(VERSION) -o $@ $^

# This is a special version of erlinit that can be unit tested
erlinit-test: $(wildcard src/*.c)
	$(CC) $(CFLAGS) -DPROGRAM_VERSION=$(VERSION) -DUNITTEST -o $@ $^

test: check
check: erlinit erlinit-test
	tests/run_tests.sh

clean:
	-rm -fr erlinit erlinit-test tests/work

.PHONY: test clean check
