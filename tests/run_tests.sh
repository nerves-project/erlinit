#!/bin/bash

# Set absolute paths to utilities since we'll clear the environment for testing
# NOTE: It should be possible to delay clearing the environment to make this less
#       painful.
CAT=/bin/cat
ECHO=/bin/echo
GREP=/bin/grep
LN=/bin/ln
LS=/bin/ls
MKDIR=/bin/mkdir
RM=/bin/rm
SH=/bin/sh
DIFF=/usr/bin/diff
FAKECHROOT=/usr/bin/fakechroot
SORT=/usr/bin/sort
TOUCH=/usr/bin/touch
CHROOT=/usr/sbin/chroot

TESTS_DIR=$(dirname $(readlink -f $0))

WORK=$TESTS_DIR/work
ERLINIT=$TESTS_DIR/../erlinit
FAKE_ERLEXEC=$TESTS_DIR/fake_erlexec
RESULTS=$WORK/results

FAKE_ERTS_DIR=$WORK/usr/lib/erlang/erts-6.0

# Just in case there are some leftover from a previous test, clear it out
$RM -fr $WORK

[ -e $ERLINIT ] || ( $ECHO "Build $ERLINIT first"; exit 1 )
[ -e $FAKECHROOT ] || ( $ECHO "Can't find $FAKECHROOT"; exit 1 )
[ -e $CHROOT ] || ( $ECHO "Can't find $CHROOT"; exit 1 )

# Forget the environment, since that's how erlinit runs
unset -v `env | sed -e 's/=.*//'`

run() {
    TEST=$1
    CONFIG=$WORK/$TEST.config
    CMDLINE_FILE=$WORK/$TEST.cmdline
    EXPECTED=$WORK/$TEST.expected

    $ECHO Running $TEST...

    # Setup a fake chroot to simulate erlinit boot
    $RM -fr $WORK
    $MKDIR -p $WORK/sbin
    $MKDIR -p $WORK/bin
    $MKDIR -p $WORK/etc
    $MKDIR -p $WORK/usr/bin
    $LN -s $ECHO $WORK/bin/echo
    $LN -s $SH $WORK/bin/sh
    $LN -s $ERLINIT $WORK/sbin/erlinit
    $MKDIR -p $FAKE_ERTS_DIR/bin
    $LN -s $FAKE_ERLEXEC $FAKE_ERTS_DIR/bin/erlexec

    # Run the test script to setup files for the test
    source $TESTS_DIR/$TEST

    if [ -e $CONFIG ]; then
       $LN -s $CONFIG $WORK/etc/erlinit.config
    fi

    if [ -e $CMDLINE_FILE ]; then
        CMDLINE=$($CAT $CMDLINE_FILE)
    else
        CMDLINE= 
    fi

    # run
    $FAKECHROOT $CHROOT $WORK /sbin/erlinit $CMDLINE 2> $RESULTS.raw

    # Trim the results of known lines that vary between runs
    $CAT $RESULTS.raw | \
        $GREP -vi "erlinit: Env:.*FAKECHROOT" | \
        $GREP -v "erlinit: Env: 'LD_" | \
        $GREP -v "erlinit: Env: 'SHLVL=" | \
        $GREP -v "erlinit: Env: '_=" | \
        $GREP -v "erlinit: Env: 'PWD=" \
        > $RESULTS

    # check results
    $DIFF $RESULTS $EXPECTED
    if [ $? != 0 ]; then
        $ECHO Test $TEST failed!
        exit 1
    fi
}

# Test command line arguments
TESTS=$($LS $TESTS_DIR/test_* | $SORT)
for TEST_CONFIG in $TESTS; do
    TEST=$(/usr/bin/basename $TEST_CONFIG .expected)
    run $TEST
done

$RM -fr $WORK
$ECHO Pass!
exit 0
