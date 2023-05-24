VERSION=1.12.3

CFLAGS+=-Wall -Wextra -O2

# If creating a reproducible build, force the buildtime to the hardcoded date.
# See https://reproducible-builds.org/specs/source-date-epoch/
ifneq ($(SOURCE_DATE_EPOCH),)
    BUILD_TIME=$(SOURCE_DATE_EPOCH)
else
    BUILD_TIME=$(shell date +%s)
endif

# _GNU_SOURCE is for asprintf
EXTRA_CFLAGS= -D_GNU_SOURCE -DPROGRAM_VERSION=$(VERSION) -DBUILD_TIME=$(BUILD_TIME)

ifeq ($(shell uname),Darwin)
EXTRA_CFLAGS+=-Isrc/compat
EXTRA_SRC=src/compat/compat.c
endif

erlinit: $(wildcard src/*.c) $(EXTRA_SRC)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $^

fixture:
	$(MAKE) -C tests/fixture

test: check
check:
	# Ensure that erlinit is built the reproducible way for the tests
	$(RM) erlinit
	SOURCE_DATE_EPOCH=1568377824 $(MAKE) do_check

do_check: erlinit fixture
	tests/run_tests.sh

format:
	astyle \
	    --style=kr \
	    --indent=spaces=4 \
	    --align-pointer=name \
	    --align-reference=name \
	    --convert-tabs \
	    --attach-namespaces \
	    --max-code-length=100 \
	    --max-instatement-indent=120 \
	    --pad-header \
	    --pad-oper \
	    src/*.[ch]

clean:
	$(RM) erlinit
	$(RM) -r tests/work
	$(MAKE) -C tests/fixture clean

.PHONY: test clean check fixture do_check
