#!/bin/bash

# "readlink -f" implementation for BSD
# This code was extracted from the Elixir shell scripts
readlink_f () {
    cd "$(dirname "$1")" > /dev/null
    filename="$(basename "$1")"
    if [ -h "$filename" ]; then
        readlink_f "$(readlink "$filename")"
    else
        echo "`pwd -P`/$filename"
    fi
}

# Start the tests up in an empty environment
env -i /bin/bash $(dirname $(readlink_f $0))/run_tests_impl.sh $*
