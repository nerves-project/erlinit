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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

static int system_cmd(const char *cmd, char *output_buffer, int length)
{
    debug("system_cmd '%s'", cmd);
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        warn("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // child
        int devnull = open("/dev/null", O_RDWR);
        if (devnull < 0) {
            warn("Couldn't open /dev/null");
            exit(EXIT_FAILURE);
        }

        close(pipefd[0]); // no reads from the pipe
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        if (dup2(devnull, STDIN_FILENO) < 0)
            warn("dup2 devnull");
        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            warn("dup2 pipe");
        close(devnull);

        char *cmd_copy = strdup(cmd);
        char *exec_path = strtok(cmd_copy, " ");
        char *exec_argv[16];
        int arg = 0;

        exec_argv[arg++] = exec_path;
        while ((exec_argv[arg] = strtok(NULL, " ")) != NULL)
            arg++;
        exec_argv[arg] = 0;
        if (exec_path)
            execvp(exec_path, exec_argv);

        // Not supposed to reach here.
        warn("execvp '%s' failed", cmd);
        exit(EXIT_FAILURE);
    } else {
        // parent
        close(pipefd[1]); // No writes to the pipe

        length--; // Save room for a '\0'
        int index = 0;
        int amt;
        while (index != length &&
               (amt = read(pipefd[0], &output_buffer[index], length - index)) > 0)
            index += amt;
        output_buffer[index] = '\0';
        close(pipefd[0]);

        int status;
        if (waitpid(pid, &status, 0) != pid) {
            warn("waitpid");
            return -1;
        }
        return status;
    }
}

static void trim_whitespace(char *s)
{
    char *left = s;
    while (*left != 0 && isspace(*left))
        left++;
    char *right = s + strlen(s) - 1;
    while (right >= left && isspace(*right))
        right--;

    int len = right - left + 1;
    if (len)
        memmove(s, left, len);
    s[len] = 0;
}

static void kill_whitespace(char *s)
{
    // This function trims whitespace off the front and back, and if
    // there's any whitespace in the middle, it truncates the string there.
    trim_whitespace(s);
    while (*s) {
        if (isspace(*s))
            *s = '\0';
        else
            s++;
    }
}

static void configure_hostname()
{
    debug("configure_hostname");
    char hostname[128] = "\0";

    if (options.hostname_pattern) {
        // Set the hostname based on a pattern
        char unique_id[64] = "xxxxxxxx";
        if (options.uniqueid_exec) {
            system_cmd(options.uniqueid_exec, unique_id, sizeof(unique_id));
            kill_whitespace(unique_id);
        }
        sprintf(hostname, options.hostname_pattern, unique_id);
        kill_whitespace(hostname);
    } else {
        // Set the hostname from /etc/hostname
        FILE *fp = fopen("/etc/hostname", "r");
        if (!fp) {
            warn("/etc/hostname not found");
            return;
        }

        // The hostname should be the first line of the file
        if (fgets(hostname, sizeof(hostname), fp))
            trim_whitespace(hostname);
        fclose(fp);
    }

    if (*hostname == '\0') {
        warn("Not setting empty hostname");
        return;
    }

    debug("Hostname: %s", hostname);
    OK_OR_WARN(sethostname(hostname, strlen(hostname)), "Error setting hostname: %s", strerror(errno));
}

static void enable_loopback()
{
    // Set the loopback interface to up
    int fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        warn("socket(PF_INET) failed");
        return;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_name[0] = 'l';
    ifr.ifr_name[1] = 'o';
    ifr.ifr_name[2] = '\0';
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        warn("SIOCGIFFLAGS failed on lo");
        goto cleanup;
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr)) {
        warn("SIOCSIFFLAGS failed on lo");
        goto cleanup;
    }

    // Currently only configuring IPv4.
    struct sockaddr_in *addr = (struct sockaddr_in *) &ifr.ifr_addr;
    addr->sin_family = AF_INET;
    addr->sin_port = 0;
    addr->sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
    if (ioctl(fd, SIOCSIFADDR, &ifr)) {
        warn("SIOCSIFADDR failed on lo");
        goto cleanup;
    }

    addr->sin_addr.s_addr = htonl(0xff000000); // 255.0.0.0
    if (ioctl(fd, SIOCSIFNETMASK, &ifr)) {
        warn("SIOCSIFNETMASK failed on lo");
        goto cleanup;
    }

cleanup:
    close(fd);
}

void setup_networking()
{
    debug("setup_networking");

    // Bring up the loopback interface (needed if the erlang distribute protocol code gets run)
    enable_loopback();
    configure_hostname();
}
