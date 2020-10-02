/*
The MIT License (MIT)

Copyright (c) 2013-20 Frank Hunleth

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
#include <stdio.h>
#include <string.h>

#include <linux/reboot.h>

// Initialize the default options
struct erlinit_options options = {
    .verbose = 0,
    .print_timing = 0,
    .unintentional_exit_cmd = LINUX_REBOOT_CMD_RESTART,
    .fatal_reboot_cmd = LINUX_REBOOT_CMD_RESTART,
    .warn_unused_tty = 0,
    .controlling_terminal = NULL,
    .alternate_exec = NULL,
    .uniqueid_exec = NULL,
    .hostname_pattern = NULL,
    .additional_env = NULL,
    .release_search_path = NULL,
    .release_include_erts = 0,
    .extra_mounts = NULL,
    .run_on_exit = NULL,
    .pre_run_exec = NULL,
    .boot_path = NULL,
    .working_directory = NULL,
    .gid = 0,
    .uid = 0,
    .graceful_shutdown_timeout_ms = 10000,
    .update_clock = 0,
    .shutdown_report = NULL
};

enum erlinit_option_value {
    // Options with short versions (match their value to the short option's character)
    // NOTE: Except for -e, -m, and -v, most of the short options aren't used and it's much more
    //       readable to use the long option form.
    OPT_BOOT = 'b',
    OPT_CTTY = 'c',
    OPT_UNIQUEID_EXEC= 'd',
    OPT_ENV = 'e',
    OPT_HANG_ON_EXIT = 'h',
    OPT_MOUNT = 'm',
    OPT_HOSTNAME_PATTERN = 'n',
    OPT_RELEASE_PATH = 'r',
    OPT_ALTERNATE_EXEC = 's',
    OPT_PRINT_TIMING = 't',
    OPT_VERBOSE = 'v',

    // Long option only
    OPT_REBOOT_ON_EXIT = 256,
    OPT_POWEROFF_ON_EXIT,
    OPT_HANG_ON_FATAL,
    OPT_REBOOT_ON_FATAL,
    OPT_POWEROFF_ON_FATAL,
    OPT_RUN_ON_EXIT,
    OPT_WARN_UNUSED_TTY,
    OPT_WORKING_DIRECTORY,
    OPT_UID,
    OPT_GID,
    OPT_PRE_RUN_EXEC,
    OPT_GRACEFUL_SHUTDOWN_TIMEOUT,
    OPT_UPDATE_CLOCK,
    OPT_RELEASE_INCLUDE_ERTS,
    OPT_TTY_OPTIONS,
    OPT_SHUTDOWN_REPORT
};

static struct option long_options[] = {
    {"boot", required_argument, 0, OPT_BOOT },
    {"ctty",  required_argument, 0, OPT_CTTY },
    {"uniqueid-exec",  required_argument, 0, OPT_UNIQUEID_EXEC },
    {"hang-on-exit", no_argument, 0, OPT_HANG_ON_EXIT },
    {"reboot-on-exit", no_argument, 0, OPT_REBOOT_ON_EXIT },
    {"poweroff-on-exit", no_argument, 0, OPT_POWEROFF_ON_EXIT },
    {"hang-on-fatal", no_argument, 0, OPT_HANG_ON_FATAL },
    {"reboot-on-fatal", no_argument, 0, OPT_REBOOT_ON_FATAL },
    {"poweroff-on-fatal", no_argument, 0, OPT_POWEROFF_ON_FATAL },
    {"env", required_argument, 0, OPT_ENV },
    {"mount", required_argument, 0, OPT_MOUNT },
    {"hostname-pattern", required_argument, 0, OPT_HOSTNAME_PATTERN },
    {"release-path", required_argument, 0, OPT_RELEASE_PATH },
    {"release-include-erts", no_argument, 0, OPT_RELEASE_INCLUDE_ERTS },
    {"run-on-exit", required_argument, 0, OPT_RUN_ON_EXIT },
    {"alternate-exec", required_argument, 0, OPT_ALTERNATE_EXEC },
    {"print-timing", no_argument, 0, OPT_PRINT_TIMING },
    {"verbose", no_argument, 0, OPT_VERBOSE },
    {"warn-unused-tty", no_argument, 0, OPT_WARN_UNUSED_TTY },
    {"working-directory", required_argument, 0, OPT_WORKING_DIRECTORY},
    {"uid", required_argument, 0, OPT_UID },
    {"gid", required_argument, 0, OPT_GID },
    {"pre-run-exec", required_argument, 0, OPT_PRE_RUN_EXEC },
    {"graceful-shutdown-timeout", required_argument, 0, OPT_GRACEFUL_SHUTDOWN_TIMEOUT },
    {"update-clock", no_argument, 0, OPT_UPDATE_CLOCK },
    {"tty-options", required_argument, 0, OPT_TTY_OPTIONS},
    {"shutdown-report", required_argument, 0, OPT_SHUTDOWN_REPORT},
    {0,     0,      0, 0 }
};

#define SET_STRING_OPTION(opt) if (opt) free(opt); opt = strdup(optarg);
#define APPEND_STRING_OPTION(opt, sep) if (opt) opt = append_string_option(opt, sep, optarg); else opt = strdup(optarg);

static char *append_string_option(char *previous_opt, char sep, const char *arg)
{
    char *new_opt;
    if (asprintf(&new_opt, "%s%c%s", previous_opt, sep, arg) < 0)
        fatal("asprintf failed");
    free(previous_opt);
    return new_opt;
}

void parse_args(int argc, char *argv[])
{
    for (;;) {
        int option_index;
        int opt = getopt_long(argc,
                              argv,
                              "b:c:d:e:hm:n:r:s:tv",
                              long_options,
                              &option_index);
        if (opt < 0)
            break;

        switch (opt) {
        case OPT_BOOT: // --boot path
            SET_STRING_OPTION(options.boot_path);
            break;
        case OPT_CTTY: // --ctty ttyS0
            SET_STRING_OPTION(options.controlling_terminal);
            break;
        case OPT_UNIQUEID_EXEC: // --uniqueid-exec program
            SET_STRING_OPTION(options.uniqueid_exec);
            break;
        case OPT_ENV: // --env FOO=bar;FOO2=bar2
            APPEND_STRING_OPTION(options.additional_env, ';');
            break;
        case OPT_HANG_ON_EXIT: // --hang-on-exit
            options.unintentional_exit_cmd = LINUX_REBOOT_CMD_HALT;
            break;
        case OPT_REBOOT_ON_EXIT: // --reboot-on-exit
            options.unintentional_exit_cmd = LINUX_REBOOT_CMD_RESTART;
            break;
        case OPT_POWEROFF_ON_EXIT: // --poweroff-on-exit
            options.unintentional_exit_cmd = LINUX_REBOOT_CMD_POWER_OFF;
            break;
        case OPT_HANG_ON_FATAL: // --hang-on-fatal
            options.fatal_reboot_cmd = LINUX_REBOOT_CMD_HALT;
            break;
        case OPT_REBOOT_ON_FATAL: // --reboot-on-fatal
            options.fatal_reboot_cmd = LINUX_REBOOT_CMD_RESTART;
            break;
        case OPT_POWEROFF_ON_FATAL: // --poweroff-on-fatal
            options.fatal_reboot_cmd = LINUX_REBOOT_CMD_POWER_OFF;
            break;
        case OPT_MOUNT: // --mount /dev/mmcblk0p3:/root:vfat::
            APPEND_STRING_OPTION(options.extra_mounts, ';');
            break;
        case OPT_HOSTNAME_PATTERN: // --hostname-pattern nerves-%.4s
            SET_STRING_OPTION(options.hostname_pattern);
            break;
        case OPT_RELEASE_PATH: // --release-path /srv/erlang
            SET_STRING_OPTION(options.release_search_path);
            break;
        case OPT_RELEASE_INCLUDE_ERTS: // --release-include-erts
            options.release_include_erts = 1;
            break;
        case OPT_RUN_ON_EXIT: // --run-on-exit /bin/sh
            SET_STRING_OPTION(options.run_on_exit);
            break;
        case OPT_ALTERNATE_EXEC: // --alternate-exec "/usr/bin/dtach -N /tmp/iex_prompt"
            SET_STRING_OPTION(options.alternate_exec);
            break;
        case OPT_PRINT_TIMING: // --print-timing
            options.print_timing = 1;
            break;
        case OPT_VERBOSE: // --verbose
            options.verbose = 1;
            break;
        case OPT_WARN_UNUSED_TTY: // --warn-unused-tty
            options.warn_unused_tty = 1;
            break;
        case OPT_UID: // --uid 100
            options.uid = strtol(optarg, NULL, 0);
            break;
        case OPT_GID: // --gid 200
            options.gid = strtol(optarg, NULL, 0);
            break;
        case OPT_PRE_RUN_EXEC: // --pre-run-exec /bin/special-init
            SET_STRING_OPTION(options.pre_run_exec);
            break;
        case OPT_WORKING_DIRECTORY: // --working_directory /root
            SET_STRING_OPTION(options.working_directory);
            break;
        case OPT_GRACEFUL_SHUTDOWN_TIMEOUT: // --graceful-shutdown-timeout 10000
            options.graceful_shutdown_timeout_ms = strtol(optarg, NULL, 0);
            break;
        case OPT_UPDATE_CLOCK: // --update-clock
            options.update_clock = 1;
            break;
        case OPT_TTY_OPTIONS: // --tty-options 115200n8
            SET_STRING_OPTION(options.tty_options);
            break;
        case OPT_SHUTDOWN_REPORT: // --shutdown-report /root/shutdown.txt
            SET_STRING_OPTION(options.shutdown_report);
            break;
        default:
            // getopt prints a warning, so we don't have to
            break;
        }
    }
}
