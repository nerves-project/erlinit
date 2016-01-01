
erlinit: $(wildcard src/*.c)
	$(CC) -Wall -O2 -o $@ $^

test: check
check: erlinit
	tests/run_tests.sh

clean:
	-rm -f erlinit

.PHONY: test clean check
