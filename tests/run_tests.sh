#!/bin/bash

env -i /bin/bash $(dirname $(readlink -f $0))/run_tests_impl.sh $*
