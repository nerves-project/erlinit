
erlinit: $(wildcard src/*.c)
	$(CC) -Wall -O2 -o $@ $^

test: erlinit
	tests/run_tests.sh

clean:
	-rm -f erlinit

.PHONY: test clean
