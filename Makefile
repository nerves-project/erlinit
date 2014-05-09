
erlinit: erlinit.c
	$(CC) -o $@ $<

test: erlinit
	tests/run_tests.sh

.PHONY: test
