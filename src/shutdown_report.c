// SPDX-FileCopyrightText: 2020 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//

#include "erlinit.h"
#include <stdio.h>
#include <stdlib.h>
#include <linux/reboot.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static const char *reboot_cmd(int cmd)
{
    switch (cmd) {
    case LINUX_REBOOT_CMD_RESTART:
        return "restart";
    case LINUX_REBOOT_CMD_HALT:
        return "halt";
    case LINUX_REBOOT_CMD_POWER_OFF:
        return "power off";
    default:
        return "unknown";
    }
}

static const char *yes_or_no(int value)
{
    return value ? "yes" : "no";
}

static double delta_seconds(const struct timespec *ts1, const struct timespec *ts2)
{
    double first = ts1->tv_sec + ts1->tv_nsec * 0.0000000001;
    double second = ts2->tv_sec + ts2->tv_nsec * 0.0000000001;
    return second - first;
}

static void report_uptime(FILE *fp)
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    div_t q;
    q = div(tp.tv_sec, 60);
    int seconds = q.rem;
    q = div(q.quot, 60);
    int minutes = q.rem;
    q = div(q.quot, 24);
    int hours = q.rem;
    int days = q.quot;

    fprintf(fp, "Uptime: %d days, %d:%02d:%02d\n", days, hours, minutes, seconds);
}

static void report_current_time(FILE *fp)
{
    char buffer[64];

    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    time_t now;
    now = tp.tv_sec;
    struct tm result;
    fprintf(fp, "Current time: %s", asctime_r(gmtime_r(&now, &result), buffer));
}

static void report_exit_info(FILE *fp, const struct erlinit_exit_info *exit_info)
{
    fprintf(fp, "Intentional exit: %s\n", yes_or_no(exit_info->is_intentional_exit));
    fprintf(fp, "Graceful shutdown succeeded: %s\n", yes_or_no(exit_info->graceful_shutdown_ok));
    fprintf(fp, "Graceful shutdown time: ");
    double shutdown_seconds = delta_seconds(&exit_info->shutdown_start, &exit_info->shutdown_complete);
    if (shutdown_seconds > 0)
        fprintf(fp, "%.3f seconds\n", shutdown_seconds);
    else
        fprintf(fp, "N/A\n");

    if (WIFEXITED(exit_info->wait_status))
        fprintf(fp, "Erlang exit status: %d\n", WEXITSTATUS(exit_info->wait_status));
    if (WIFSIGNALED(exit_info->wait_status))
        fprintf(fp, "Erlang exited due to signal: %d\n", WTERMSIG(exit_info->wait_status));
    fprintf(fp, "Shutdown action: %s\n", reboot_cmd(exit_info->desired_reboot_cmd));
    if (exit_info->reboot_args[0] != '\0')
        fprintf(fp, "Reboot args: %s\n", (const char *) exit_info->reboot_args);
}

static void report_dmesg(FILE *fp)
{
    fprintf(fp, "\n## dmesg\n\n");

    int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(fp, "Error opening /dev/kmsg: %s\n", strerror(errno));
        return;
    }

    fprintf(fp, "```\n");
    int in_message = 0;
    for (;;) {
        char buffer[4096];
        ssize_t num_read = read(fd, buffer, sizeof(buffer));
        if (num_read <= 0)
            break;

        // Trivial log parser that prints the messages
        int start_ix = 0;
        for (int i = 0; i < num_read; i++) {
            if (in_message && buffer[i] == '\n') {
                fwrite(&buffer[start_ix], 1, i + 1 - start_ix, fp);
                start_ix = i + 1;
                in_message = 0;
            } else if (!in_message && buffer[i] == ';') {
                start_ix = i + 1;
                in_message = 1;
            }
        }
        if (in_message)
            fwrite(&buffer[start_ix], 1, num_read - start_ix, fp);
    }
    close(fd);
    fprintf(fp, "```\n");
}

void shutdown_report_create(const char *path, const struct erlinit_exit_info *exit_info)
{
    elog(ELOG_DEBUG, "Writing shutdown report to '%s'", path);

    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        elog(ELOG_WARNING, "Failed to write shutdown dump to '%s'", path);
        return;
    }
    fprintf(fp, "# Erlinit shutdown report\n\n");

    report_uptime(fp);

    report_current_time(fp);

    report_exit_info(fp, exit_info);

    report_dmesg(fp);

    fclose(fp);
}
