// SPDX-FileCopyrightText: 2021 Connor Rigby
//
// SPDX-License-Identifier: MIT
//

#include "erlinit.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

static rlim_t str_to_limit(const char *s)
{
    // empty string means infinity
    if (strcmp(s, "unlimited") == 0 || strlen(s) == 0)
        return RLIM_INFINITY;
    else
        return strtoul(s, NULL, 10);
}

static int str_to_resource(const char *s)
{
    struct entry {
        const char *str;
        int value;
    };
    static const struct entry table[] = {
        {"core", RLIMIT_CORE},
        {"data", RLIMIT_DATA},
        {"nice", RLIMIT_NICE},
        {"fsize", RLIMIT_FSIZE},
        {"sigpending", RLIMIT_SIGPENDING},
        {"memlock", RLIMIT_MEMLOCK},
        {"rss", RLIMIT_RSS},
        {"nofile", RLIMIT_NOFILE},
        {"msgqueue", RLIMIT_MSGQUEUE},
        {"rtprio", RLIMIT_RTPRIO},
        {"stack", RLIMIT_STACK},
        {"cpu", RLIMIT_CPU},
        {"nproc", RLIMIT_NPROC},
        {"as", RLIMIT_AS},
        {"locks", RLIMIT_LOCKS},
        {"rttime", RLIMIT_RTTIME},
        {NULL, 0}
    };

    for (const struct entry *i = table; i->str; ++i) {
        if (strcmp(s, i->str) == 0)
            return i->value;
    }

    warn("Unrecognized resource %s", s);
    return -1;
}

void create_limits()
{
    char *temp = options.limits;
    while (temp) {
        const char *resource_name = strsep(&temp, ":");
        const char *soft = strsep(&temp, ":");
        const char *hard = strsep(&temp, ";"); // multi-limit separator
        if (resource_name && soft && hard) {
            int resource = str_to_resource(resource_name);
            struct rlimit limit;
            limit.rlim_cur = str_to_limit(soft);
            limit.rlim_max = str_to_limit(hard);
            if (setrlimit(resource, &limit) != 0)
                warn("could not set limits %s");

        } else {
            warn("Invalid parameter to -l. Expecting 3 colon-separated fields");
        }
    }
}
