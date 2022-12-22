// SPDX-FileCopyrightText: 2013 Frank Hunleth
//
// SPDX-License-Identifier: MIT

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

static void make_rfc1123_compatible(char *s)
{
    // This function modifies the hostname to make sure that it's compatible
    // with RFC 1123. I.e. a-z, 0-9, -, and '.'. Capitals are made lowercase
    // for consistency with tools that automatically normalize capitalization.
    // Invalid characters are removed.
    char *d = s;
    while (*s) {
        char c = *s;
        if ((c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '.') {
            // Good
            *d = c;
            d++;
        } else if (c >= 'A' && c <= 'Z') {
            // Make lowercase
            *d = c - 'A' + 'a';
            d++;
        }
        s++;
    }
    *d = '\0';
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
        make_rfc1123_compatible(hostname);
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
