/*
The MIT License (MIT)

Copyright (c) 2013-15 Frank Hunleth

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "erlinit.h"
#include <getopt.h>
#include <string.h>

static struct option long_options[] = {
    {"ctty",  required_argument, 0, 'c' },
    {"uniqueid-exec",  required_argument, 0, 'd' },
    {"hang-on-exit", no_argument, 0, 'h' },
    {"reboot-on-exit", no_argument, 0, 'H' },
    {"env", required_argument, 0, 'e' },
    {"mount", required_argument, 0, 'm' },
    {"hostname-pattern", required_argument, 0, 'n' },
    {"release-path", required_argument, 0, 'r' },
    {"alternate-exec", required_argument, 0, 's' },
    {"print-timing", no_argument, 0, 't' },
    {"verbose", no_argument, 0, 'v' },
    {0,     0,      0, 0 }
};

void parse_args(int argc, char *argv[])
{
    for (;;) {
        int option_index;
        int opt = getopt_long(argc,
                              argv,
                              "c:d:e:hm:n:r:s:tv",
                              long_options,
                              &option_index);
        if (opt < 0)
            break;

        switch (opt) {
        case 'c': // --ctty ttyS0
            options.controlling_terminal = strdup(optarg);
            break;
        case 'd': // --uniqueid-exec program
            options.uniqueid_exec = strdup(optarg);
            break;
        case 'e': // --env FOO=bar;FOO2=bar2
            options.additional_env = strdup(optarg);
            break;
        case 'h': // --hang-on-exit
            options.hang_on_exit = 1;
            break;
        case 'H': // --reboot-on-exit
            options.hang_on_exit = 0;
            break;
        case 'm': // --mount /dev/mmcblk0p3:/root:vfat::
            options.extra_mounts = strdup(optarg);
            break;
        case 'n': // --hostname-pattern nerves-%.4s
            options.hostname_pattern = strdup(optarg);
            break;
        case 'r': // --release-path /srv/erlang
            options.release_search_path = strdup(optarg);
            break;
        case 's': // --alternate-exec "/usr/bin/dtach -N /tmp/iex_prompt"
            options.alternate_exec = strdup(optarg);
            break;
        case 't': // --print-timing
            options.print_timing = 1;
            break;
        case 'v': // --verbose
            options.verbose = 1;
            break;
        default:
            // Note: We don't print usage, since this
            warn("ignoring option '%c'. See erlinit documentation.", argv[optind]);
            break;
        }
    }
}
