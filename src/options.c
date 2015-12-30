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

void parse_args(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "c:d:e:hm:n:r:s:tv")) != -1) {
        switch (opt) {
        case 'c':
            options.controlling_terminal = strdup(optarg);
            break;
        case 'd':
            options.uniqueid_exec = strdup(optarg);
            break;
        case 'e':
            options.additional_env = strdup(optarg);
            break;
        case 'h':
            options.hang_on_exit = 1;
            break;
        case 'm':
            options.extra_mounts = strdup(optarg);
            break;
        case 'n':
            options.hostname_pattern = strdup(optarg);
            break;
        case 'r':
            options.release_search_path = strdup(optarg);
            break;
        case 's':
            options.alternate_exec = strdup(optarg);
            break;
        case 't':
            options.print_timing = 1;
            break;
        case 'v':
            options.verbose = 1;
            break;
        default:
            warn("ignoring command line argument '%c'", opt);
            break;
        }
    }
}
