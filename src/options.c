/*
The MIT License (MIT)

Copyright (c) 2013-16 Frank Hunleth

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
#include <stdlib.h>
#include <string.h>

#include <linux/reboot.h>

// Initialize the default options
struct erlinit_options options = {
    .verbose = 0,
    .print_timing = 0,
    .unintentional_exit_cmd = LINUX_REBOOT_CMD_RESTART,
    .fatal_reboot_cmd = LINUX_REBOOT_CMD_HALT,
    .warn_unused_tty = 0,
    .controlling_terminal = NULL,
    .alternate_exec = NULL,
    .uniqueid_exec = NULL,
    .hostname_pattern = NULL,
    .additional_env = NULL,
    .release_search_path = NULL,
    .extra_mounts = NULL
};

static struct option long_options[] = {
    {"ctty",  required_argument, 0, 'c' },
    {"uniqueid-exec",  required_argument, 0, 'd' },
    {"hang-on-exit", no_argument, 0, 'h' },
    {"reboot-on-exit", no_argument, 0, 'H' },
    {"poweroff-on-exit", no_argument, 0, '+' },
    {"hang-on-fatal", no_argument, 0, 'z' },
    {"reboot-on-fatal", no_argument, 0, 'Z' },
    {"poweroff-on-fatal", no_argument, 0, '$' },
    {"env", required_argument, 0, 'e' },
    {"mount", required_argument, 0, 'm' },
    {"hostname-pattern", required_argument, 0, 'n' },
    {"release-path", required_argument, 0, 'r' },
    {"alternate-exec", required_argument, 0, 's' },
    {"print-timing", no_argument, 0, 't' },
    {"verbose", no_argument, 0, 'v' },
    {"warn-unused-tty", no_argument, 0, '!' },
    {0,     0,      0, 0 }
};

#define SET_STRING_OPTION(opt) if (opt) free(opt); opt = strdup(optarg);

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
            SET_STRING_OPTION(options.controlling_terminal)
            break;
        case 'd': // --uniqueid-exec program
            SET_STRING_OPTION(options.uniqueid_exec)
            break;
        case 'e': // --env FOO=bar;FOO2=bar2
            SET_STRING_OPTION(options.additional_env)
            break;
        case 'h': // --hang-on-exit
            options.unintentional_exit_cmd = LINUX_REBOOT_CMD_HALT;
            break;
        case 'H': // --reboot-on-exit
            options.unintentional_exit_cmd = LINUX_REBOOT_CMD_RESTART;
            break;
        case '+': // --poweroff-on-exit
            options.unintentional_exit_cmd = LINUX_REBOOT_CMD_POWER_OFF;
            break;
        case 'z': // --hang-on-fatal
            options.fatal_reboot_cmd = LINUX_REBOOT_CMD_HALT;
            break;
        case 'Z': // --reboot-on-fatal
            options.fatal_reboot_cmd = LINUX_REBOOT_CMD_RESTART;
            break;
        case '$': // --poweroff-on-fatal
            options.fatal_reboot_cmd = LINUX_REBOOT_CMD_POWER_OFF;
            break;
        case 'm': // --mount /dev/mmcblk0p3:/root:vfat::
            SET_STRING_OPTION(options.extra_mounts)
            break;
        case 'n': // --hostname-pattern nerves-%.4s
            SET_STRING_OPTION(options.hostname_pattern)
            break;
        case 'r': // --release-path /srv/erlang
            SET_STRING_OPTION(options.release_search_path)
            break;
        case 's': // --alternate-exec "/usr/bin/dtach -N /tmp/iex_prompt"
            SET_STRING_OPTION(options.alternate_exec)
            break;
        case 't': // --print-timing
            options.print_timing = 1;
            break;
        case 'v': // --verbose
            options.verbose = 1;
            break;
        case '!': // --warn-unused-tty
            options.warn_unused_tty = 1;
            break;
        default:
            // getopt prints a warning, so we don't have to
            break;
        }
    }
}
