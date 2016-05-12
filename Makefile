
VERSION=0.7.2

erlinit: $(wildcard src/*.c)
	$(CC) -Wall -O2 -DPROGRAM_VERSION=$(VERSION) -o $@ $^

# This is a special version of erlinit that can be unit tested
erlinit-test: $(wildcard src/*.c)
	$(CC) -Wall -O2 -DPROGRAM_VERSION=$(VERSION) -DUNITTEST -o $@ $^

test: check
check: erlinit erlinit-test
	tests/run_tests.sh

clean:
	-rm -fr erlinit erlinit-test tests/work

.PHONY: test clean check
