// SPDX-FileCopyrightText: 2015 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//

#include "erlinit.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

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

static int lookup_tty_options(const char *options, speed_t *speed)
{
    // NOTE: Only setting the speed is supported now. No parity and 8 bits are assumed.
    unsigned long baud = strtoul(options, NULL, 10);
    switch (baud) {
    case 9600:
        *speed = B9600;
        break;
    case 19200:
        *speed = B19200;
        break;
    case 38400:
        *speed = B38400;
        break;
    case 57600:
        *speed = B57600;
        break;
    case 115200:
        *speed = B115200;
        break;
    default:
        warn("Couldn't parse tty option '%s'. Defaulting to 115200n8", options);
        *speed = B115200;
        break;
    }
    return 0;
}

static void init_terminal(int fd)
{
    if (options.tty_options == NULL)
        return;

    struct termios termios;
    if (tcgetattr(fd, &termios) < 0) {
        warn("Could not get console settings");
        return;
    }

    speed_t speed;
    lookup_tty_options(options.tty_options, &speed);

    // Set baud
    cfsetispeed(&termios, speed);
    cfsetospeed(&termios, speed);

    // 8 bits
    termios.c_cflag &= ~CSIZE;
    termios.c_cflag |= CS8;

    // no parity
    termios.c_cflag &= ~PARENB;

    // 1 stop bit
    termios.c_cflag &= ~CSTOPB;

    // No control flow
    termios.c_cflag &= ~CRTSCTS;
    termios.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Forced options
    termios.c_cflag |= CLOCAL; // Ignore modem control lines
    termios.c_cflag |= CREAD;  // Enable receiver
    termios.c_oflag = 0;
    termios.c_lflag = 0;
    termios.c_iflag &= ~(ICRNL | INLCR); // No CR<->LF conversions

    termios.c_cc[VMIN] = 1;
    termios.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &termios) < 0)
        warn("Could not set console settings");
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
        if (readsysfs(SYSFS_ACTIVE_CONSOLE, &ttypath[TTY_PREFIX_LENGTH],
                      sizeof(ttypath) - TTY_PREFIX_LENGTH) == 0) {
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
        init_terminal(fd);

        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    } else {
        warn("error setting controlling terminal: %s", ttypath);
    }
}
