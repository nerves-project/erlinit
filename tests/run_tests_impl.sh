#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2015 Frank Hunleth
#
# SPDX-License-Identifier: MIT
#

# "readlink -f" implementation for BSD
# This code was extracted from the Elixir shell scripts
readlink_f () {
    cd "$(dirname "$1")" > /dev/null
    filename="$(basename "$1")"
    if [ -h "$filename" ]; then
        readlink_f "$(readlink "$filename")"
    else
        echo "$(pwd -P)/$filename"
    fi
}

TESTS_DIR=$(dirname "$(readlink_f "$0")")

WORK=$TESTS_DIR/work
RESULTS=$WORK/results

ERLINIT=$TESTS_DIR/../erlinit
FIXTURE=$TESTS_DIR/fixture/erlinit_fixture.so

FAKE_ERLEXEC=$TESTS_DIR/fake_erlexec
FAKE_ERTS_DIR=$WORK/usr/lib/erlang/erts-6.0

# Collect the tests from the commandline
TESTS=$*
if [ -z $TESTS ]; then
    TESTS=$(ls "$TESTS_DIR"/[0-9][0-9][0-9]_*)
fi

# Just in case there are some leftover from a previous test, clear it out
rm -fr "$WORK"

[ -e "$ERLINIT" ] || ( echo "Build $ERLINIT first"; exit 1 )
[ -e "$FIXTURE" ] || ( echo "Build $FIXTURE first"; exit 1 )

run() {
    TEST=$1
    CONFIG=$WORK/$TEST.config
    CMDLINE_FILE=$WORK/$TEST.cmdline
    EXPECTED=$WORK/$TEST.expected

    echo "Running $TEST..."

    # Setup a fake root directory to simulate erlinit boot
    rm -fr "$WORK"
    mkdir -p "$WORK/proc"
    mkdir -p "$WORK/dev"
    mkdir -p "$WORK/sbin"
    mkdir -p "$WORK/bin"
    mkdir -p "$WORK/etc"
    mkdir -p "$WORK/run"
    mkdir -p "$WORK/tmp"
    mkdir -p "$WORK/usr/bin"
    ln -s "$ERLINIT" "$WORK/sbin/init"
    mkdir -p "$FAKE_ERTS_DIR/bin"
    ln -s "$FAKE_ERLEXEC" "$FAKE_ERTS_DIR/bin/erlexec"
    mkdir -p "$WORK/root"

    # Create some device files (the fixture sets their types)
    touch "$WORK/dev/mmcblk0" "$WORK/dev/mmcblk0p1" "$WORK/dev/mmcblk0p2" "$WORK/dev/mmcblk0p3" "$WORK/dev/mmcblk0p4"
    ln -s /dev/null "$WORK/dev/null"
    mkdir -p "$WORK/sys/block/mmcblk0/mmcblk0p1"
    mkdir -p "$WORK/sys/block/mmcblk0/mmcblk0p2"
    mkdir -p "$WORK/sys/block/mmcblk0/mmcblk0p3"
    mkdir -p "$WORK/sys/block/mmcblk0/mmcblk0p4"
    mkdir -p "$WORK/sys/block/mmcblk0/queue"
    mkdir -p "$WORK/sys/block/mmcblk0/slaves"
    mkdir -p "$WORK/sys/block/mmcblk0/mq"
    mkdir -p "$WORK/sys/block/mmcblk0/holders"
    mkdir -p "$WORK/sys/block/sda/sda1"
    mkdir -p "$WORK/sys/block/sda/sda2"
    echo "179:0" > "$WORK/sys/block/mmcblk0/dev"
    echo "179:1" > "$WORK/sys/block/mmcblk0/mmcblk0p1/dev"
    echo "1" > "$WORK/sys/block/mmcblk0/mmcblk0p1/partition"
    echo "179:2" > "$WORK/sys/block/mmcblk0/mmcblk0p2/dev"
    echo "2" > "$WORK/sys/block/mmcblk0/mmcblk0p2/partition"
    echo "179:3" > "$WORK/sys/block/mmcblk0/mmcblk0p3/dev"
    echo "3" > "$WORK/sys/block/mmcblk0/mmcblk0p3/partition"
    echo "179:4" > "$WORK/sys/block/mmcblk0/mmcblk0p4/dev"
    echo "4" > "$WORK/sys/block/mmcblk0/mmcblk0p4/partition"
    echo "8:0" > "$WORK/sys/block/sda/dev"
    echo "8:1" > "$WORK/sys/block/sda/sda1/dev"
    echo "1" > "$WORK/sys/block/sda/sda1/partition"
    echo "8:2" > "$WORK/sys/block/sda/sda2/dev"
    echo "2" > "$WORK/sys/block/sda/sda2/partition"

    # Fake active console
    mkdir -p "$WORK/sys/class/tty/console"
    echo "ttyF1" >"$WORK/sys/class/tty/console/active"
    ln -s "$(tty)" "$WORK/dev/ttyF1"
    ln -s "$(tty)" "$WORK/dev/ttyAMA0"
    ln -s "$(tty)" "$WORK/dev/tty1"

    # Fake mounts
    cat >"$WORK/proc/mounts" << EOF
/dev/root / squashfs ro,relatime 0 0
sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0
proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0
devpts /dev/pts devpts rw,nosuid,noexec,relatime,gid=5,mode=620,ptmxmode=000 0 0
tmpfs /dev/shm tmpfs rw,nosuid,nodev 0 0
tmpfs /sys/fs/cgroup tmpfs ro,nosuid,nodev,noexec,mode=755 0 0
EOF
    # Fake random info
    mkdir -p "$WORK/proc/sys/kernel/random"
    echo "256" > "$WORK/proc/sys/kernel/random/poolsize"
    touch "$WORK/dev/urandom"

    # Run the test script to setup files for the test
    source "$TESTS_DIR/$TEST"

    if [ -e "$CONFIG" ]; then
       ln -s "$CONFIG" "$WORK/etc/erlinit.config"
    fi

    if [ -e "$CMDLINE_FILE" ]; then
        CMDLINE=$(cat "$CMDLINE_FILE")
    else
        CMDLINE=
    fi

    # Run erlinit
    # NOTE: Call 'exec' so that it's possible to set argv0, but that means we
    #       need a subshell - hence the parentheses.
    (LD_PRELOAD=$FIXTURE DYLD_INSERT_LIBRARIES=$FIXTURE WORK=$WORK exec -a /sbin/init $ERLINIT $CMDLINE 2> "$RESULTS.raw")

    # Trim the results of known lines that vary between runs
    # The calls to sed fixup differences between getopt implementations.
    cat "$RESULTS.raw" | \
        grep -v "erlinit 1\.[0-9]\+\.[0-9]\+" | \
        grep -v "erlinit: Env: 'LD_" | \
        grep -v "erlinit: Env: 'SHLVL=" | \
        grep -v "erlinit: Env: '_=" | \
        grep -v "erlinit: Env: 'PWD=" | \
        grep -v "erlinit: Env: 'WORK=" | \
        grep -v "erlinit: Env: 'SED=" | \
        $SED -e "s/\`/'/g" | \
        $SED -e "s@^/sbin/init@init@" | \
        $SED -e "s/^erlinit: unrecognized option/init: unrecognized option/" | \
        $SED -e "s/^erlinit: invalid option/init: invalid option/" | \
        $SED -e "s/invalid option -- 'Z'/invalid option -- Z/" \
        > "$RESULTS"

    # check results
    diff -w "$RESULTS" "$EXPECTED"
    if [ $? != 0 ]; then
        echo "Test $TEST failed!"
        exit 1
    fi
}

# Test command line arguments
for TEST_CONFIG in $TESTS; do
    TEST=$(/usr/bin/basename "$TEST_CONFIG" .expected)
    run $TEST
done

rm -fr "$WORK"
echo Pass!
exit 0
