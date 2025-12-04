// SPDX-FileCopyrightText: 2018 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//

#include "erlinit.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static inline int digit2int(char digit)
{
    return digit - '0';
}

static inline int min(int a, int b)
{
    if (a < b)
        return a;
    else
        return b;
}

static void hostname_sprintf(char *output, const char *pattern, const char *serial)
{
    // Simple version of sprintf that supports a subset of patterns:
    //   %s        - Include the whole string
    //   %.<len>s  - Include len characters of the string starting at the left
    //   %-.<len>s - Include len characters of the string starting at the right
    while (*pattern) {
        if (*pattern == '%') {
            pattern++;
            const char *start = serial;
            int max_len = strlen(serial);
            int len;
            if (pattern[0] == 's') {
                pattern++;
                len = max_len;
            } else if (pattern[0] == '.' && isdigit(pattern[1]) && pattern[2] == 's') {
                len = min(max_len, digit2int(pattern[1]));
                pattern += 3;
            } else if (pattern[0] == '.' && isdigit(pattern[1]) && isdigit(pattern[2]) && pattern[3] == 's') {
                len = min(max_len, digit2int(pattern[1]) * 10 + digit2int(pattern[2]));
                pattern += 4;
            } else if (pattern[0] == '-' && pattern[1] == '.' && isdigit(pattern[2]) && pattern[3] == 's') {
                len = min(max_len, digit2int(pattern[2]));
                start += max_len - len;
                pattern += 4;
            } else if (pattern[0] == '-' && pattern[1] == '.' && isdigit(pattern[2]) && isdigit(pattern[3]) && pattern[4] == 's') {
                len = min(max_len, digit2int(pattern[2]) * 10 + digit2int(pattern[3]));
                start += max_len - len;
                pattern += 5;
            } else {
                // Just give up on bad percent escapes. The config file is wrong and
                // hopefully it will be obvious that the pattern was broke. Don't crash
                // or error, since this shouldn't mess up booting.
                break;
            }
            memcpy(output, start, len);
            output += len;
        } else {
            *output++ = *pattern++;
        }
    }
    *output = '\0';
}

void configure_hostname()
{
    elog(ELOG_DEBUG, "configure_hostname");
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
                elog(ELOG_WARNING, "`%s` failed. Using default ID: '%s'", options.uniqueid_exec, unique_id);
            }
        }
        hostname_sprintf(hostname, options.hostname_pattern, unique_id);
        kill_whitespace(hostname);
        make_rfc1123_compatible(hostname);
    } else {
        // Set the hostname from /etc/hostname
        FILE *fp = fopen("/etc/hostname", "r");
        if (!fp) {
            elog(ELOG_WARNING, "/etc/hostname not found");
            return;
        }

        // The hostname should be the first line of the file
        if (fgets(hostname, sizeof(hostname), fp))
            trim_whitespace(hostname);
        fclose(fp);
    }

    if (*hostname == '\0') {
        elog(ELOG_WARNING, "Not setting empty hostname");
        return;
    }

    elog(ELOG_DEBUG, "Hostname: %s", hostname);
    OK_OR_WARN(sethostname(hostname, strlen(hostname)), "Error setting hostname: %s", strerror(errno));
}
