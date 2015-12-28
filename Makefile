
erlinit: src/erlinit.c
	$(CC) -Wall -o $@ $<

test: erlinit
	tests/run_tests.sh

clean:
	-rm -f erlinit

.PHONY: test clean
