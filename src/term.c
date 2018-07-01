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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define SYSFS_ACTIVE_CONSOLE "/sys/class/tty/console/active"

#define TTY_MAX_PATH_LENGTH 32
#define TTY_PREFIX "/dev/"
#define TTY_PREFIX_LENGTH 5

static int readsysfs(const char *path, char *buffer, int maxlen)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    int count = read(fd, buffer, maxlen - 1);

    close(fd);
    if (count <= 0)
        return 0;

    // Trim trailing \n
    count--;
    buffer[count] = 0;
    return count;
}

void warn_unused_tty()
{
    debug("warn_unused_tty");

    // warn_unused_tty must be called after set_ctty to ensure this is set.
    const char *used_tty = options.controlling_terminal;

    char hostname[32];
    if (gethostname(hostname, sizeof(hostname)) < 0)
        strcpy(hostname, "unknown");
    hostname[sizeof(hostname) - 1] = '\0';

    char all_ttys[TTY_MAX_PATH_LENGTH];
    if (readsysfs(SYSFS_ACTIVE_CONSOLE, all_ttys, sizeof(all_ttys)) == 0)
        return;

    char *tty = strtok(all_ttys, " ");
    while (tty != NULL) {
        if (strcmp(used_tty, tty) != 0) {
            char ttypath[TTY_MAX_PATH_LENGTH + 1] = TTY_PREFIX;
            strncat(&ttypath[TTY_PREFIX_LENGTH], tty, sizeof(ttypath) - TTY_PREFIX_LENGTH - 1);

            int fd = open(ttypath, O_WRONLY);
            if (fd >= 0) {
                char *msg;
                int len = asprintf(&msg,
                                   PROGRAM_NAME ": The shell will be launched on tty '%s'.\r\n"
                                   PROGRAM_NAME ": If you would like the shell to be on this tty,\r\n"
                                   PROGRAM_NAME ": configure erlinit with '-c %s'.\r\n"
                                   PROGRAM_NAME ": The hostname is '%s'.\r\n",
                                   used_tty, tty, hostname);
                ssize_t ignored = write(fd, msg, len);
                (void) ignored;
                close(fd);
                free(msg);
            }
        }
        tty = strtok(NULL, " ");
    }
}

void set_ctty()
{
    debug("set_ctty");

    // Set up a controlling terminal for Erlang so that
    // it's possible to get to shell management mode.
    // See http://www.busybox.net/FAQ.html#job_control
    setsid();

    char ttypath[TTY_MAX_PATH_LENGTH + 1] = TTY_PREFIX;

    // Check if the user is forcing the controlling terminal
    if (options.controlling_terminal &&
            strlen(options.controlling_terminal) < sizeof(ttypath) - TTY_PREFIX_LENGTH) {
        strcat(&ttypath[TTY_PREFIX_LENGTH], options.controlling_terminal);
    } else {
        // Pick the active console(s)
        if (readsysfs(SYSFS_ACTIVE_CONSOLE, &ttypath[TTY_PREFIX_LENGTH], sizeof(ttypath) - TTY_PREFIX_LENGTH) == 0) {
            // No active console?
            warn("no active consoles found!");
            return;
        }

        // It's possible that multiple consoles are active, so pick the first one.
        char *sep = strchr(&ttypath[TTY_PREFIX_LENGTH], ' ');
        if (sep)
            *sep = 0;

        // Save the chosen controlling terminal
        options.controlling_terminal = strdup(&ttypath[TTY_PREFIX_LENGTH]);
    }

    int fd = open(ttypath, O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    } else {
        warn("error setting controlling terminal: %s", ttypath);
    }
}
