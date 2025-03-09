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

static int format_message(char **strp, const char *fmt, va_list ap)
{
    char *msg;
    if (vasprintf(&msg, fmt, ap) < 0) {
        return -1;
    }

    int rc = asprintf(strp, PROGRAM_NAME ": %s\n", msg);
    free(msg);

    return rc;
}

static void log_write(const char *str, size_t len)
{
    size_t ignore;
    int log_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (log_fd >= 0) {
        ignore = write(log_fd, str, len);
        close(log_fd);
    } else {
        ignore = write(STDERR_FILENO, str, len);
    }
    (void) ignore;
}

static void log_format(const char *fmt, va_list ap)
{
    char *line;
    int len = format_message(&line, fmt, ap);

    if (len >= 0) {
        log_write(line, len);
        free(line);
    }
}

void debug(const char *fmt, ...)
{
    if (options.verbose) {
        va_list ap;
        va_start(ap, fmt);
        log_format(fmt, ap);
        va_end(ap);
    }
}

void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_format(fmt, ap);
    va_end(ap);
}

void fatal(const char *fmt, ...)
{
    log_write("\n\nFATAL ERROR:\n", 15);

    va_list ap;
    va_start(ap, fmt);
    log_format(fmt, ap);
    va_end(ap);

    log_write("\n\nCANNOT CONTINUE.\n", 19);

    // Sleep so that the message can be printed
    sleep(1);

    // Halt/reboot/poweroff
    reboot(options.fatal_reboot_cmd);

    // Kernel panic if reboot() returns.
    exit(1);
}


