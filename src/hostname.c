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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

void configure_hostname()
{
    debug("configure_hostname");
    char hostname[128] = "\0";

    if (options.hostname_pattern) {
        // Set the hostname based on a pattern
        const char *default_unique_id = "00000000";
        const char *unique_id = default_unique_id;
        char buffer[64];
        if (options.uniqueid_exec) {
            if (system_cmd(options.uniqueid_exec, buffer, sizeof(buffer)) == EXIT_SUCCESS) {
                kill_whitespace(buffer);
                unique_id = buffer;
            } else {
                warn("`%s` failed. Using default ID: '%s'", options.uniqueid_exec, unique_id);
            }
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
