
VERSION=0.7.0-dev

erlinit: $(wildcard src/*.c)
	$(CC) -Wall -O2 -DPROGRAM_VERSION=$(VERSION) -o $@ $^

test: check
check: erlinit
	tests/run_tests.sh

clean:
	-rm -f erlinit

.PHONY: test clean check
