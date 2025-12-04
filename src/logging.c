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

// See /usr/include/syslog.h for values. They're also standardized in RFC5424.
#define PRIVAL_DEBUG "<31>"  // 3*8 + 7
#define PRIVAL_ERROR "<27>"  // 3*8 + 3
#define PRIVAL_ALERT "<25>"  // 3*8 + 1
#define LEN_PRIVAL 4

static int format_message(const char *prival, char **strp, const char *fmt, va_list ap)
{
    char *msg;
    if (vasprintf(&msg, fmt, ap) < 0) {
        return -1;
    }

    // Hard-code to use the daemon facility (3)
    int rc = asprintf(strp, "%s" PROGRAM_NAME ": %s\n", prival, msg);
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
        // Strip the prival part of the log message if going to stderr
        ignore = write(STDERR_FILENO, &str[LEN_PRIVAL], len - LEN_PRIVAL);
    }
    (void) ignore;
}

static void log_format(const char *prival, const char *fmt, va_list ap)
{
    char *line;
    int len = format_message(prival, &line, fmt, ap);

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
        log_format(PRIVAL_DEBUG, fmt, ap);
        va_end(ap);
    }
}

void warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    // Log at the error level since warnings here are pretty important
    log_format(PRIVAL_ERROR, fmt, ap);
    va_end(ap);
}

void fatal(const char *fmt, ...)
{
    log_write(PRIVAL_ALERT "FATAL ERROR:\n", LEN_PRIVAL + 13);

    va_list ap;
    va_start(ap, fmt);
    log_format(PRIVAL_ALERT, fmt, ap);
    va_end(ap);

    log_write(PRIVAL_ALERT "CANNOT CONTINUE.\n", LEN_PRIVAL + 17);

    // Sleep so that the message can be printed
    sleep(1);

    // Halt/reboot/poweroff
    reboot(options.fatal_reboot_cmd);

    // Kernel panic if reboot() returns.
    exit(1);
}


