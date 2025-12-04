// SPDX-FileCopyrightText: 2015 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//

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

// This is similar to fgets except that it drops the remainder of lines that are too long
static char *get_line(char *line, int max_len, FILE *fp)
{
    int count = 0;

    // Leave room for the NULL terminator
    max_len--;

    while (count < max_len) {
        int c = fgetc(fp);
        if (c == EOF || c == '\n') {
            line[count] = 0;

            if (count == 0 && c == EOF)
                return NULL;
            else
                return line;
        }
        line[count] = (char) c;
        count++;
    }
    line[count] = 0;

    // Trim off the rest of the line
    for (;;) {
        int c = fgetc(fp);
        if (c == EOF || c == '\n')
            break;
    }

    return line;
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
    char line[256];
    while (get_line(line, sizeof(line), fp) && argc < max_args) {
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
        elog(ELOG_ERROR, "Too many arguments specified between the config file and commandline. Dropping some.");
        argc = MAX_ARGC - *merged_argc + 1;
    }

    memcpy(&merged_argv[*merged_argc], &argv[1], (argc - 1) * sizeof(char **));
    *merged_argc += argc - 1;
}


