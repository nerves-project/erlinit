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
#ifdef __linux__
#include <sys/ioctl.h>
// Avoid including <linux/termios.h> directly to prevent conflicts with
// the system <termios.h>. Define the termios2 struct and TCGETS2/TCSETS2
// ioctl macros locally so we can set arbitrary baud rates on Linux.
struct termios2 {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[19];
    speed_t c_ispeed;
    speed_t c_ospeed;
};
#ifndef TCGETS2
#define TCGETS2 _IOR('T', 0x2A, struct termios2)
#define TCSETS2 _IOW('T', 0x2B, struct termios2)
#endif
#endif
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

// Parses the tty options string (expected to be a baud number). On success
// sets *speed to a standard speed_t when possible and *raw_baud to 0.
// When the baud is not one of the standard constants (e.g. > 115200),
// sets *speed to 0 and *raw_baud to the numeric baud so the caller may
// attempt an OS-specific method to set the baud (termios2 on Linux).
static int lookup_tty_options(const char *options, speed_t *speed, unsigned long *raw_baud)
{
    // NOTE: Only setting the speed is supported now. No parity and 8 bits are assumed.
    unsigned long baud = strtoul(options, NULL, 10);
    *raw_baud = 0;
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
        // If it's a reasonable numeric baud, return it in raw_baud so the
        // caller can try platform specific means to configure it.
        if (baud > 0) {
            *speed = 0;
            *raw_baud = baud;
        } else {
            warn("Couldn't parse tty option '%s'. Defaulting to 115200n8", options);
            *speed = B115200;
        }
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
    unsigned long raw_baud = 0;
    lookup_tty_options(options.tty_options, &speed, &raw_baud);

    // Set baud. If raw_baud is non-zero attempt Linux-specific termios2
    // handling to support arbitrary baud rates > 115200. Otherwise use
    // the standard cfset* helpers.
    if (raw_baud == 0) {
        cfsetispeed(&termios, speed);
        cfsetospeed(&termios, speed);
    } else {
#ifdef __linux__
        // Use termios2 to set arbitrary baud rates (BOTHER).
        struct termios2 tio2;
        if (ioctl(fd, TCGETS2, &tio2) < 0) {
            warn("Could not get console settings (termios2), falling back to 115200");
            cfsetispeed(&termios, B115200);
            cfsetospeed(&termios, B115200);
        } else {
            // Copy the flags we configured into the termios2 structure so
            // our other settings (8N1, flow control, etc.) are preserved.
            tio2.c_cflag = termios.c_cflag;
#ifdef BOTHER
            tio2.c_cflag |= BOTHER;
#endif
            tio2.c_ispeed = raw_baud;
            tio2.c_ospeed = raw_baud;

            tio2.c_iflag = termios.c_iflag;
            tio2.c_oflag = termios.c_oflag;
            tio2.c_lflag = termios.c_lflag;
            memcpy(tio2.c_cc, termios.c_cc, sizeof(tio2.c_cc));

            if (ioctl(fd, TCSETS2, &tio2) < 0) {
                warn("Could not set console settings (termios2), falling back to 115200");
                cfsetispeed(&termios, B115200);
                cfsetospeed(&termios, B115200);
            }
            // Note: when termios2 is used we don't call tcsetattr below for speed
            // because we've already applied settings via TCSETS2. We'll still call
            // tcsetattr to set any remaining flags later.
        }
#else
        warn("Arbitrary baud rates >115200 are only supported on Linux. Defaulting to 115200n8");
        cfsetispeed(&termios, B115200);
        cfsetospeed(&termios, B115200);
#endif
    }

    // 8 bits
    termios.c_cflag &= ~CSIZE;
    termios.c_cflag |= CS8;

    // no parity
    termios.c_cflag &= ~PARENB;

    // 1 stop bit
    termios.c_cflag &= ~CSTOPB;

    // No control flow
#ifdef CRTSCTS
    termios.c_cflag &= ~CRTSCTS;
#endif
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

        if (fd > 2)
            close(fd);
    } else {
        warn("error setting controlling terminal: %s", ttypath);
    }
}
