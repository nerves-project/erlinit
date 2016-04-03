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

#ifndef ERLINIT_H
#define ERLINIT_H

#define PROGRAM_NAME "erlinit"
#ifndef PROGRAM_VERSION
#define PROGRAM_VERSION unknown
#endif

#define xstr(s) str(s)
#define str(s) #s
#define PROGRAM_VERSION_STR xstr(PROGRAM_VERSION)

#define ERLANG_ROOT_DIR "/usr/lib/erlang"
#define DEFAULT_RELEASE_ROOT_DIR "/srv/erlang"

// This is the maximum number of mounted filesystems that
// is expected in a running system. It is used on shutdown
// when trying to unmount everything gracefully.
#define MAX_MOUNTS 32

#define MAX_ARGC 32

// PATH_MAX wasn't in the musl include files, so rather
// than pulling an arbitrary number in from linux/limits.h,
// just define to something that should be trivially safe
// for erlinit use.
#define ERLINIT_PATH_MAX 1024

struct erlinit_options {
    int verbose;
    int print_timing;
    int unintentional_exit_cmd; // Invoked when erlang exits. See linux/reboot.h for options
    int fatal_reboot_cmd;       // Invoked on fatal() log message. See linux/reboot.h for options
    int warn_unused_tty;
    char *controlling_terminal;
    char *alternate_exec;
    char *uniqueid_exec;
    char *hostname_pattern;
    char *additional_env;
    char *release_search_path;
    char *extra_mounts;
};

extern struct erlinit_options options;

// Logging functions
void debug(const char *fmt, ...);
void warn(const char *fmt, ...);
void fatal(const char *fmt, ...);

#define OK_OR_FATAL(WORK, MSG, ...) do { if ((WORK) < 0) fatal(MSG, ## __VA_ARGS__); } while (0)
#define OK_OR_WARN(WORK, MSG, ...) do { if ((WORK) < 0) warn(MSG, ## __VA_ARGS__); } while (0)

// Configuration loading
void merge_config(int argc, char *argv[], int *merged_argc, char **merged_argv);

// Argument parsing
void parse_args(int argc, char *argv[]);

// Networking
void setup_networking();

// Filesystems
void setup_pseudo_filesystems();
void setup_filesystems();
void unmount_all();

// Terminal
void set_ctty();

#endif // ERLINIT_H

