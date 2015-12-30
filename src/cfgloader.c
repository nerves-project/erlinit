/*
The MIT License (MIT)

Copyright (c) 2013-15 Frank Hunleth

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
#include <stdio.h>
#include <string.h>

static int parse_config_line(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *c = line;
    char *token = NULL;
#define STATE_SPACE 0
#define STATE_TOKEN 1
#define STATE_QUOTED_TOKEN 2
    int state = STATE_SPACE;
    while (*c != '\0') {
        switch (state) {
        case STATE_SPACE:
            if (*c == '#')
                return argc;
            else if (isspace(*c))
                break;
            else if (*c == '"') {
                token = c + 1;
                state = STATE_QUOTED_TOKEN;
            } else {
                token = c;
                state = STATE_TOKEN;
            }
            break;
        case STATE_TOKEN:
            if (*c == '#' || isspace(*c)) {
                *argv = strndup(token, c - token);
                argv++;
                argc++;
                token = NULL;

                if (*c == '#' || argc == max_args)
                    return argc;

                state = STATE_SPACE;
            }
            break;
        case STATE_QUOTED_TOKEN:
            if (*c == '"') {
                *argv = strndup(token, c - token);
                argv++;
                argc++;
                token = NULL;
                state = STATE_SPACE;

                if (argc == max_args)
                    return argc;
            }
            break;
        }
        c++;
    }

    if (token) {
        *argv = strndup(token, c - token);
        argc++;
    }

    return argc;
}

// This is a very simple config file parser that extracts
// commandline arguments from the specified file.
static int load_config(const char *filename,
                       char **argv,
                       int max_args)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return 0;

    int argc = 0;
    char line[128];
    while (fgets(line, sizeof(line), fp) && argc < max_args) {
        int new_args = parse_config_line(line, argv, max_args - argc);
        argc += new_args;
        argv += new_args;
    }

    fclose(fp);
    return argc;
}

void merge_config(int argc, char *argv[], int *merged_argc, char **merged_argv)
{
    // When merging, argv[0] is first, then the
    // arguments from erlinit.config and then any
    // additional arguments from the commandline.
    // This way, the commandline takes precedence.
    *merged_argc = 1;
    merged_argv[0] = argv[0];

    *merged_argc += load_config("/etc/erlinit.config",
                                &merged_argv[1],
                                MAX_ARGC - argc);

    if (*merged_argc + argc - 1 > MAX_ARGC) {
        warn("Too many arguments specified between the config file and commandline. Dropping some.");
        argc = MAX_ARGC - *merged_argc + 1;
    }

    memcpy(&merged_argv[*merged_argc], &argv[1], (argc - 1) * sizeof(char**));
    *merged_argc += argc - 1;
}


