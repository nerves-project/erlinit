#!/bin/bash

# Set absolute paths to utilities since we'll clear the environment for testing
FAKECHROOT=/usr/bin/fakechroot
CHROOT=/usr/sbin/chroot
RM=/bin/rm
LN=/bin/ln
DIFF=/usr/bin/diff
MKDIR=/bin/mkdir
ECHO=/bin/echo
SORT=/usr/bin/sort
LS=/bin/ls
CAT=/bin/cat
SH=/bin/sh
GREP=/bin/grep

TESTS_DIR=$(dirname $(readlink -f $0))

WORK=$TESTS_DIR/root
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
    CONFIG=$TESTS_DIR/$TEST.config
    CMDLINE_FILE=$TESTS_DIR/$TEST.cmdline
    EXPECTED=$TESTS_DIR/$TEST.expected

    $ECHO Running $TEST...

    # setup a fake chroot to simulate erlinit boot
    $RM -fr $WORK
    $MKDIR -p $WORK/sbin
    $MKDIR -p $WORK/bin
    $MKDIR -p $WORK/etc
    $LN -s $ECHO $WORK/bin/echo
    $LN -s $SH $WORK/bin/sh
    $LN -s $ERLINIT $WORK/sbin/erlinit
    $MKDIR -p $FAKE_ERTS_DIR/bin
    $LN -s $FAKE_ERLEXEC $FAKE_ERTS_DIR/bin/erlexec

    if [ -e $CONFIG ]; then
       $LN -s $CONFIG $WORK/etc/erlinit.config
    fi

    if [ -e $CMDLINE_FILE ]; then
        CMDLINE=$($CAT $CMDLINE_FILE)
    else
        CMDLINE= 
    fi

    # run
    $FAKECHROOT -e $TESTS_DIR/clearenv $CHROOT $WORK /sbin/erlinit $CMDLINE 2> $RESULTS.raw

    # Trim the results of known lines that vary between runs
    $CAT $RESULTS.raw | \
        $GREP -vi "erlinit: Env:.*FAKECHROOT" | \
        $GREP -v "erlinit: Env:.*LD_" | \
        $GREP -v "erlinit: Env:.*PWD" \
        > $RESULTS

    # check results
    $DIFF $RESULTS $EXPECTED
    if [ $? != 0 ]; then
        $ECHO Test $TEST failed!
        exit 1
    fi
}

# Test command line arguments
TESTS=$($LS $TESTS_DIR/*.expected | $SORT)
for TEST_CONFIG in $TESTS; do
    TEST=$(/usr/bin/basename $TEST_CONFIG .expected)
    run $TEST
done

$RM -fr $WORK
$ECHO Pass!
exit 0
