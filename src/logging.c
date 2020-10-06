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
    log_write("\n\nFATAL ERROR:\n", 18);

    va_list ap;
    va_start(ap, fmt);
    log_format(fmt, ap);
    va_end(ap);

    log_write("\n\nCANNOT CONTINUE.\n", 22);

    // Sleep so that the message can be printed
    sleep(1);

    // Halt/reboot/poweroff
    reboot(options.fatal_reboot_cmd);

    // Kernel panic if reboot() returns.
    exit(1);
}


