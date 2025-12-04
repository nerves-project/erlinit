// SPDX-FileCopyrightText: 2015 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//

#include "erlinit.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/reboot.h>

static int kmsg_format(enum elog_severity severity, char **strp, const char *msg)
{
    int prival = 3 * 8 + severity; // facility=daemon(3)
    return asprintf(strp, "<%d>%s\n", prival, msg);
}

static int stderr_format(char **strp, const char *msg)
{
    return asprintf(strp, PROGRAM_NAME ": %s\n", msg);
}

static void log_write(enum elog_severity severity, const char *msg)
{
    char *str;
    size_t ignore;
    int log_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (log_fd >= 0) {
        int len = kmsg_format(severity, &str, msg);
        if (len > 0) {
            ignore = write(log_fd, str, len);
            free(str);
        }
        close(log_fd);
    } else {
        int len = stderr_format(&str, msg);
        if (len > 0) {
            ignore = write(STDERR_FILENO, str, len);
            free(str);
        }
    }
    (void) ignore;
}

void elog(enum elog_severity severity, const char *fmt, ...)
{
    if (severity <= options.verbose) {
        va_list ap;
        va_start(ap, fmt);

        char *msg;
        if (vasprintf(&msg, fmt, ap) > 0) {
            log_write(severity, msg);
            free(msg);
        }

        va_end(ap);
    }
}

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    char *msg;
    if (vasprintf(&msg, fmt, ap) > 0) {
        log_write(ELOG_EMERG, msg);
        free(msg);
    }

    va_end(ap);

    log_write(ELOG_EMERG, "FATAL ERROR. CANNOT CONTINUE.");

    // Sleep so that the message can be printed
    sleep(1);

    // Halt/reboot/poweroff
    reboot(options.fatal_reboot_cmd);

    // Kernel panic if reboot() returns.
    exit(1);
}