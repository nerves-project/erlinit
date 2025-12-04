// SPDX-FileCopyrightText: 2015 Frank Hunleth
// SPDX-FileCopyrightText: 2016 Christian Taedcke
// SPDX-FileCopyrightText: 2019 João Henrique Ferreira de Freitas
// SPDX-FileCopyrightText: 2019 Justin Schneck
// SPDX-FileCopyrightText: 2021 Connor Rigby
//
// SPDX-License-Identifier: MIT
//

#ifndef ERLINIT_H
#define ERLINIT_H

#include <time.h>

#define PROGRAM_NAME "erlinit"
#ifndef PROGRAM_VERSION
#error PROGRAM_VERSION is undefined
#endif

#define xstr(s) str(s)
#define str(s) #s
#define PROGRAM_VERSION_STR xstr(PROGRAM_VERSION)

#define ERLANG_ROOT_DIR "/usr/lib/erlang"

// If the system libraries exist on the target, they would
// be found here. Note that Nerves strips this directory
// since the system libraries are bundled with the release.
#define ERLANG_ERTS_LIB_DIR ERLANG_ROOT_DIR "/lib"

#define DEFAULT_RELEASE_ROOT_DIR "/srv/erlang"

// This is the maximum number of mounted filesystems that
// is expected in a running system. It is used on shutdown
// when trying to unmount everything gracefully.
#define MAX_MOUNTS 32

#define MAX_ARGC 64

// PATH_MAX wasn't in the musl include files, so rather
// than pulling an arbitrary number in from linux/limits.h,
// just define to something that should be trivially safe
// for erlinit use.
#define ERLINIT_PATH_MAX 1024

// See /usr/include/syslog.h for values. They're also standardized in RFC5424.
enum elog_severity {
    ELOG_EMERG = 0,
    ELOG_ALERT,
    ELOG_CRIT,
    ELOG_ERROR,
    ELOG_WARNING,
    ELOG_NOTICE,
    ELOG_INFO,
    ELOG_DEBUG
};

struct erlinit_options {
    enum elog_severity verbose;
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
    int  release_include_erts;
    char *extra_mounts;
    char *run_on_exit;
    char *pre_run_exec;
    char *boot_path;
    char *working_directory;
    int uid;
    int gid;
    int graceful_shutdown_timeout_ms;
    int update_clock;
    char *tty_options;
    char *shutdown_report;
    char *limits;
    int x_pivot_root_on_overlayfs;
    char *core_pattern;
};

extern struct erlinit_options options;

struct erlinit_exit_info {
    int is_intentional_exit;
    int desired_reboot_cmd;
    int wait_status;
    struct timespec shutdown_start;
    struct timespec shutdown_complete;
    int graceful_shutdown_ok;
    char reboot_args[32];
};

// Logging functions
void elog(enum elog_severity sev, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void fatal(const char *fmt, ...)
	__attribute__((format(printf, 1, 2), noreturn));

#define OK_OR_FATAL(WORK, MSG, ...) do { if ((WORK) < 0) fatal(MSG, ## __VA_ARGS__); } while (0)
#define OK_OR_WARN(WORK, MSG, ...) do { if ((WORK) < 0) elog(ELOG_WARNING, MSG, ## __VA_ARGS__); } while (0)

// Configuration loading
void merge_config(int argc, char *argv[], int *merged_argc, char **merged_argv);

// Argument parsing
void parse_args(int argc, char *argv[]);

// Networking
void setup_networking(void);
void configure_hostname(void);

// Filesystems
void pivot_root_on_overlayfs(void);
void setup_pseudo_filesystems(void);
void create_rootdisk_symlinks(void);
void mount_filesystems(void);
void unmount_all(void);

// Limits
void create_limits(void);

// Terminal
void set_ctty(void);
void warn_unused_tty(void);

// External commands
int system_cmd(const char *cmd, char *output_buffer, int length);

// Shutdown report
void shutdown_report_create(const char *path, const struct erlinit_exit_info *info);

// seedrng
int seedrng(void);

// Utility functions
void trim_whitespace(char *s);

#ifdef __APPLE__
#include "compat.h"
#endif

#endif // ERLINIT_H
