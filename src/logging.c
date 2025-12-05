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

static int kmsg_format(int severity, char **strp, const char *msg)
{
    int prival = 3 * 8 + (severity & ELOG_SEVERITY_MASK); // facility=daemon(3)
    return asprintf(strp, "<%d>%s\n", prival, msg);
}

static int stderr_format(char **strp, const char *msg)
{
    return asprintf(strp, PROGRAM_NAME ": %s\n", msg);
}

static int pmsg_format(char **strp, const char *msg)
{
    time_t rawtime;
    time(&rawtime);
    struct tm timeinfo;
    if (gmtime_r(&rawtime, &timeinfo) == NULL)
        return -1; // Or handle error appropriately

    // Match the RFC3339 timestamps from Erlang's logger_formatter
    // 2025-12-04T00:01:34.200744+00:00
    char timestamp[48];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000000+00:00", &timeinfo);

    return asprintf(strp, "%s " PROGRAM_NAME " %s\n", timestamp, msg);
}

static void log_pmsg_breadcrumb(const char *msg)
{
    static int open_failed = 0;

    int pmsg_fd;
    if (open_failed) {
        // Don't bother trying again on failures.
        return;
    } else {
        pmsg_fd = open("/dev/pmsg0", O_WRONLY | O_CLOEXEC);
        if (pmsg_fd < 0) {
            open_failed = 1;
            return;
        }
    }

    char *str;
    int len = pmsg_format(&str, msg);
    if (len > 0) {
        ssize_t ignore = write(pmsg_fd, str, len);
        (void) ignore;
        free(str);
    }
    close(pmsg_fd);
}

static void log_write(int severity, const char *msg)
{
    char *str;
    ssize_t ignore;
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

void elog(int severity, const char *fmt, ...)
{
    int level = severity & ELOG_SEVERITY_MASK;
    int log_pmsg = severity & ELOG_PMSG;
    if (level <= options.verbose || log_pmsg) {
        va_list ap;
        va_start(ap, fmt);

        char *msg;
        if (vasprintf(&msg, fmt, ap) > 0) {
            if (log_pmsg)
                log_pmsg_breadcrumb(msg);

            if (level <= options.verbose)
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
        log_pmsg_breadcrumb(msg);
        log_write(ELOG_EMERG, msg);
        free(msg);
    }

    va_end(ap);

    log_write(ELOG_EMERG, "FATAL ERROR. CANNOT CONTINUE.");

    // Sleep so that the message can be printed
    sleep(1);

    // Halt/reboot/poweroff
    log_pmsg_breadcrumb("Rebooting due to fatal error...");
    reboot(options.fatal_reboot_cmd);
    log_pmsg_breadcrumb("Rebooting failed. Going to try to trigger a kernel panic reboot.");

    // Kernel panic if reboot() returns.
    exit(1);
}
