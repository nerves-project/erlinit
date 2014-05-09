#!/bin/bash

FAKECHROOT=/usr/bin/fakechroot
CHROOT=/usr/sbin/chroot

TESTS_DIR=$(dirname $(readlink -f $0))

WORK=$TESTS_DIR/root
ERLINIT=$TESTS_DIR/../erlinit
RESULTS=$WORK/results

# Just in case there are some leftover from a previous test, clear it out
rm -fr $WORK

[ -e $ERLINIT ] || ( echo "Build $ERLINIT first"; exit 1 )
[ -e $FAKECHROOT ] || ( echo "Can't find $FAKECHROOT"; exit 1 )
[ -e $CHROOT ] || ( echo "Can't find $CHROOT"; exit 1 )

run() {
    TEST=$1
    CONFIG=$TESTS_DIR/$TEST.config
    CMDLINE_FILE=$TESTS_DIR/$TEST.cmdline
    EXPECTED=$TESTS_DIR/$TEST.expected

    echo Running $TEST...

    # setup
    rm -fr $WORK
    mkdir -p $WORK/sbin
    mkdir -p $WORK/etc
    ln -s $ERLINIT $WORK/sbin/erlinit
    if [ -e $CONFIG ]; then
       ln -s $CONFIG $WORK/etc/erlinit.config
    fi
    if [ -e $CMDLINE_FILE ]; then
       CMDLINE=`cat $CMDLINE_FILE`
    else
       CMDLINE= 
    fi

    $FAKECHROOT $CHROOT $WORK /sbin/erlinit $CMDLINE 2> $RESULTS

    diff $RESULTS $EXPECTED
    if [ $? != 0 ]; then
        echo Test $TEST failed!
        exit 1
    fi
}

# Test command line arguments
TESTS=`ls $TESTS_DIR/*.expected | sort`
for TEST_CONFIG in $TESTS; do
    TEST=`basename $TEST_CONFIG .expected`
    run $TEST
done

rm -fr $WORK
echo Pass!
exit 0
