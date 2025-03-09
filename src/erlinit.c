// SPDX-FileCopyrightText: 2013 Frank Hunleth
// SPDX-FileCopyrightText: 2016 Christian Taedcke
// SPDX-FileCopyrightText: 2019 João Henrique Ferreira de Freitas
// SPDX-FileCopyrightText: 2019 Justin Schneck
// SPDX-FileCopyrightText: 2020 Michał Kalbarczyk
// SPDX-FileCopyrightText: 2021 Connor Rigby
//
// SPDX-License-Identifier: MIT
//

#include "erlinit.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/reboot.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <pwd.h>

struct erl_run_info {
    // This is the base directory for the release
    // e.g., <base>/[release_name]
    // The release_name directory is optional and may not be included. It
    // is normally omitted by Nerves.
    char *release_base_dir;

    // This is the directory containing the release start scripts
    // e.g., <release_base_dir>/releases/<version>
    char *releases_version_dir;

    // This is the search path for the .beams created by
    // Elixir's Protocol consolidation code.
    // e.g., <release_base_dir>/lib/*/consolidated
    char *consolidated_protocols_path;

    // This is the name of the release. It could be empty if there's
    // no name and this is typical for Nerves.
    char *release_name;

    // ERTS version if specified by start_erl.data
    char *erts_version;

    // The directory containing ERTS
    char *erts_dir;

    // This is the path to the .boot file
    char *boot_path;

    // This is the path to sys.config
    char *sys_config;

    // This is the path to vm.args
    char *vmargs_path;
};

static void erlinit_asprintf(char **strp, const char *fmt, ...)
{
    // Free *strp afterwards if this is being called a second time.
    // (Be careful with string pointers)
    char *old_str = *strp;

    va_list ap;
    va_start(ap, fmt);
    OK_OR_FATAL(vasprintf(strp, fmt, ap), "asprintf failed");
    va_end(ap);

    if (old_str)
        free(old_str);
}

static int set_core_pattern(const char *pattern)
{
    FILE *fp = fopen("/proc/sys/kernel/core_pattern", "w");
    if (fp == NULL)
        return -errno;

    fprintf(fp, "%s", pattern);
    fclose(fp);
    debug("Set core pattern to '%s'", pattern);
    return 0;
}


static int is_directory(const char *path)
{
    struct stat sb;
    return stat(path, &sb) == 0 &&
           S_ISDIR(sb.st_mode);
}

static int starts_with(const char *str, const char *what)
{
    return strstr(str, what) == str;
}

static int erts_filter(const struct dirent *d)
{
    return starts_with(d->d_name, "erts-");
}

static void find_erts_directory(const char *erts_version,
                                const char *release_base_dir,
                                char **erts_dir)
{
    debug("find_erts_directory");

    // Common case: Release specifies an ERTS version. See if it's included
    // in the release first and then try the system version (if any).
    if (erts_version) {
        erlinit_asprintf(erts_dir, "%s/erts-%s", release_base_dir, erts_version);
        if (is_directory(*erts_dir))
            return;

        erlinit_asprintf(erts_dir, ERLANG_ROOT_DIR "/erts-%s", erts_version);
        if (is_directory(*erts_dir))
            return;

        warn("start_erl.data specifies erts-%s, but it wasn't found! Looking for any erts version",
             erts_version);
    }

    // Fall back: This path really shouldn't be exercised any more, but at one time
    // it was helpful to try to find something rather than give up.
    const char *tmp_release_base_dir;
    if (options.release_include_erts)
        tmp_release_base_dir = release_base_dir;
    else
        tmp_release_base_dir = ERLANG_ROOT_DIR;

    struct dirent **namelist;
    int n = scandir(tmp_release_base_dir,
                    &namelist,
                    erts_filter,
                    NULL);
    if (n < 0)
        fatal("Erlang installation not found. Check that %s exists", tmp_release_base_dir);
    else if (n == 0)
        fatal("erts not found. Check that erlang was installed to %s", tmp_release_base_dir);
    else if (n > 1)
        fatal("Found multiple erts directories. Clean up the installation or check that a start_erl.data was generated.");

    erlinit_asprintf(erts_dir, "%s/%s", tmp_release_base_dir, namelist[0]->d_name);

    free(namelist[0]);
    free(namelist);
}

static int file_exists(const char *path)
{
    struct stat sb;
    return stat(path, &sb) == 0;
}

static int dotfile_filter(const struct dirent *d)
{
    return strcmp(d->d_name, ".") != 0 &&
           strcmp(d->d_name, "..") != 0;
}

static int bootfile_filter(const struct dirent *d)
{
    // Look for files that end with .boot
    return strstr(d->d_name, ".boot") != NULL;
}

static int normal_directory_filter(const struct dirent *d)
{
    return d->d_type == DT_DIR && dotfile_filter(d);
}

static void find_sys_config(const char *release_version_dir, char **sys_config)
{
    debug("find_sys_config");
    erlinit_asprintf(sys_config, "%s/sys.config", release_version_dir);
    if (!file_exists(*sys_config)) {
        warn("%s not found?", *sys_config);
        free(*sys_config);
        *sys_config = NULL;
    }
}

static void find_vm_args(const char *release_version_dir, char **vmargs_path)
{
    debug("find_vm_args");
    erlinit_asprintf(vmargs_path, "%s/vm.args", release_version_dir);
    if (!file_exists(*vmargs_path)) {
        warn("%s not found?", *vmargs_path);
        free(*vmargs_path);
        *vmargs_path = NULL;
    }
}

static int find_boot_path_user(const char *release_version_dir, char **boot_path)
{
    if (options.boot_path) {
        // Handle a user-specified boot file. Absolute or relative is ok.
        if (options.boot_path[0] == '/')
            erlinit_asprintf(boot_path, "%s", options.boot_path);
        else
            erlinit_asprintf(boot_path, "%s/%s", release_version_dir, options.boot_path);

        // If the file exists, then everything is ok.
        if (file_exists(*boot_path))
            return 1;

        // Erlang also appends .boot the the path, so if a path with .boot
        // exists, we're ok as well.
        char *boot_path_with_dotboot = NULL;
        erlinit_asprintf(&boot_path_with_dotboot, "%s.boot", *boot_path);
        if (file_exists(boot_path_with_dotboot)) {
            free(boot_path_with_dotboot);
            return 1;
        }
        free(boot_path_with_dotboot);
        warn("Specified boot file '%s' not found. Auto-detecting.", options.boot_path);
    }

    return 0;
}

static int find_boot_path_by_release_name(const char *release_version_dir, const char *release_name,
                                          char **boot_path)
{
    // If not a named release, skip this option.
    if (release_name == NULL || *release_name == '\0')
        return 0;

    erlinit_asprintf(boot_path, "%s/%s.boot", release_version_dir, release_name);
    if (!file_exists(*boot_path)) {
        free(*boot_path);
        *boot_path = NULL;
        return 0;
    }

    // Strip off the .boot since that's what erl wants.
    char *dot = strrchr(*boot_path, '.');
    *dot = '\0';

    return 1;
}

static void find_boot_path_auto(const char *release_version_dir, char **boot_path)
{
    struct dirent **namelist;
    int n = scandir(release_version_dir,
                    &namelist,
                    bootfile_filter,
                    alphasort);
    if (n <= 0)
        fatal("No boot file found in %s.", release_version_dir);

    if (n > 1)
        warn("Found more than one boot file. Using %s.", namelist[0]->d_name);

    // Use the first
    erlinit_asprintf(boot_path, "%s/%s", release_version_dir, namelist[0]->d_name);

    // Strip off the .boot since that's what erl wants.
    char *dot = strrchr(*boot_path, '.');
    *dot = '\0';

    // Free everything
    while (--n >= 0)
        free(namelist[n]);
    free(namelist);
}

static void find_boot_path(const char *release_version_dir, const char *release_name,
                           char **boot_path)
{
    debug("find_boot_path");

    if (!find_boot_path_user(release_version_dir, boot_path) &&
            !find_boot_path_by_release_name(release_version_dir, release_name, boot_path))
        find_boot_path_auto(release_version_dir, boot_path);
}

static int find_consolidated_dirs(const char *release_base_dir,
                                  struct erl_run_info *run_info)
{
    // Elixir creates a special `consolidated` directory that contains
    // .beam files. It speeds up the use of Elixir Protocols
    // significantly so it should be loaded if available.
    //
    // It's found in directories of the following form:
    // <release_base_dir>/lib/<application-version>/consolidated
    //
    char *search_path = NULL;
    erlinit_asprintf(&search_path, "%s/lib", release_base_dir);

    struct dirent **namelist;
    int n = scandir(search_path,
                    &namelist,
                    normal_directory_filter,
                    alphasort);
    if (n < 0)
        return -1;

    int i;
    int num_found = 0;
    for (i = 0; i < n; i++) {
        char *consolidated_path = NULL;
        erlinit_asprintf(&consolidated_path, "%s/%s/consolidated", search_path, namelist[i]->d_name);
        if (is_directory(consolidated_path)) {
            num_found++;
            if (num_found == 1) {
                run_info->consolidated_protocols_path = consolidated_path;
                continue;
            }
        }
        free(consolidated_path);
    }
    if (num_found > 1)
        warn("More than one consolidated directory found. Using '%s'",
             run_info->consolidated_protocols_path);

    // Free everything
    while (--n >= 0)
        free(namelist[n]);
    free(namelist);

    return 0;
}

static int read_start_erl(const char *releases_dir, char **erts_version, char **release_version)
{
    char *start_erl_path = NULL;
    erlinit_asprintf(&start_erl_path, "%s/start_erl.data", releases_dir);
    FILE *fp = fopen(start_erl_path, "r");
    if (!fp) {
        warn("%s not found.", start_erl_path);
        free(start_erl_path);
        return 0;
    }

#define MAX_VERSION_AND_RELEASE_NAMES 32
    char erts_string[MAX_VERSION_AND_RELEASE_NAMES + 1];
    char rel_string[MAX_VERSION_AND_RELEASE_NAMES + 1];
    if (fscanf(fp, "%" xstr(MAX_VERSION_AND_RELEASE_NAMES) "s %" xstr(MAX_VERSION_AND_RELEASE_NAMES)
               "s",
               erts_string, rel_string) != 2) {
        warn("%s doesn't contain expected contents. Skipping.", start_erl_path);
        free(start_erl_path);
        fclose(fp);
        return 0;
    }
    erlinit_asprintf(erts_version, "%s", erts_string);
    erlinit_asprintf(release_version, "%s", rel_string);

    free(start_erl_path);
    fclose(fp);
    return 1;
}

static int reverse_alphasort(const struct dirent **e1,
                             const struct dirent **e2)
{
    return -alphasort(e1, e2);
}

static int find_release_version_dir(const char *releases_dir,
                                    char **version_dir,
                                    char **erts_version)
{
    // Check for a start_erl.data file. If one exists, then it will say
    // which release version to use.
    char *release_version = NULL;
    if (read_start_erl(releases_dir, erts_version, &release_version)) {
        // Check if the release_version corresponds to a good path.
        erlinit_asprintf(version_dir, "%s/%s", releases_dir, release_version);

        if (is_directory(*version_dir))
            return 1;

        warn("start_erl.data specifies %s, but %s isn't a directory", release_version, *version_dir);
        free(release_version);
    }

    // No start_erl.data, so pick the first subdirectory.
    struct dirent **namelist = NULL;
    int n = scandir(releases_dir,
                    &namelist,
                    dotfile_filter,
                    reverse_alphasort);
    int i;
    int success = 0;
    for (i = 0; i < n; i++) {
        erlinit_asprintf(version_dir, "%s/%s", releases_dir, namelist[i]->d_name);

        // Pick the first directory. There should only be one directory
        // anyway.
        if (is_directory(*version_dir)) {
            success = 1;
            break;
        }
    }

    if (n > 0) {
        for (i = 0; i < n; i++)
            free(namelist[i]);
        free(namelist);
    }

    return success;
}

static int find_release_dirs(const char *base,
                             int depth,
                             struct erl_run_info *run_info)
{
    // The "releases" directory could either be in the current folder
    // or one directory immediately below. For example,
    //
    // <base>/releases/<release_version>
    //
    // -or-
    //
    // <base>/<release_name>/releases/<release_version>
    //
    // Check for both. On a successful return, run_info's release_root_dir,
    // release_version_dir, release_name and release_version fields are set.
    int success = 0;
    struct dirent **namelist;
    int n = scandir(base,
                    &namelist,
                    dotfile_filter,
                    alphasort);
    int i;
    for (i = 0; i < n; i++) {
        char dirpath[ERLINIT_PATH_MAX];
        snprintf(dirpath, sizeof(dirpath), "%s/%s", base, namelist[i]->d_name);

        if (!is_directory(dirpath))
            continue;

        if (strcmp(namelist[i]->d_name, "releases") == 0 &&
                find_release_version_dir(dirpath, &run_info->releases_version_dir, &run_info->erts_version)) {
            erlinit_asprintf(&run_info->release_base_dir, "%s", base);
            success = 1;
            break;
        }

        // Recurse to the next directory down if allowed.
        if (depth && find_release_dirs(dirpath, depth - 1, run_info)) {
            erlinit_asprintf(&run_info->release_name, "%s", namelist[i]->d_name);
            success = 1;
            break;
        }
    }

    if (n >= 0) {
        for (i = 0; i < n; i++)
            free(namelist[i]);
        free(namelist);
    }

    return success;
}

static void find_release(struct erl_run_info *run_info)
{
    debug("find_release");

    if (options.release_search_path == NULL)
        options.release_search_path = strdup(DEFAULT_RELEASE_ROOT_DIR);

    // The user may specify several directories to be searched for
    // releases. Pick the first one.
    const char *search_path = strtok(options.release_search_path, ":");
    while (search_path != NULL) {
        if (find_release_dirs(search_path, 1, run_info)) {
            debug("Using release in %s.", run_info->releases_version_dir);

            find_sys_config(run_info->releases_version_dir, &run_info->sys_config);
            find_vm_args(run_info->releases_version_dir, &run_info->vmargs_path);
            find_boot_path(run_info->releases_version_dir, run_info->release_name, &run_info->boot_path);
            find_consolidated_dirs(run_info->release_base_dir, run_info);

            return;
        }

        warn("No release found in %s.", search_path);
        search_path = strtok(NULL, ":");
    }
#if 0
    if (sys_config) {
        free(sys_config);
        sys_config = NULL;
    }
    if (boot_path) {
        free(boot_path);
        boot_path = NULL;
    }
#endif
    erlinit_asprintf(&run_info->release_base_dir, "%s", ERLANG_ROOT_DIR);
}

static int has_erts_library_directory()
{
    // Nerves packages the ERTS libraries in with the release, but on
    // systems that don't do this, we need to pass the directory in the Erlang
    // commandline arguments. Currently, the directory is hardcoded in a
    // similar way to ERLANG_ROOT_DIR, so we just check whether it exists.
    // See https://github.com/nerves-project/erlinit/pull/4 for more details.
    return is_directory(ERLANG_ERTS_LIB_DIR);
}

static void setup_home_directory()
{
    struct passwd *pwd = getpwuid(options.uid);
    const char *pw_dir = pwd ? pwd->pw_dir : "/root";

    char *envvar = NULL;
    erlinit_asprintf(&envvar, "HOME=%s", pw_dir);
    putenv(envvar);
}

static void setup_environment(const struct erl_run_info *run_info)
{
    debug("setup_environment");

    // PATH appears to only be needed for user convenience when running os:cmd/1
    // It may be possible to remove in the future.
    putenv("PATH=/usr/sbin:/usr/bin:/sbin:/bin");
    putenv("TERM=xterm-256color");

    // Erlang environment

    // ROOTDIR points to the release unless it wasn't found.
    char *envvar = NULL;
    erlinit_asprintf(&envvar, "ROOTDIR=%s", run_info->release_base_dir);
    putenv(envvar);
    envvar = NULL; // putenv owns memory

    // BINDIR points to the erts bin directory.
    erlinit_asprintf(&envvar, "BINDIR=%s/bin", run_info->erts_dir);
    putenv(envvar);
    envvar = NULL; // putenv owns memory

    putenv("EMU=beam");
    putenv("PROGNAME=erlexec");

    // RELEASE_SYS_CONFIG points to the release's sys.config (but without the .config)
    // If using it, set other Elixir release environment variables
    if (run_info->sys_config) {
        int sys_config_len = strlen(run_info->sys_config);
        erlinit_asprintf(&envvar, "RELEASE_SYS_CONFIG=%.*s", sys_config_len - 7, run_info->sys_config);
        putenv(envvar);
        envvar = NULL; // putenv owns memory

        erlinit_asprintf(&envvar, "RELEASE_ROOT=%s", run_info->release_base_dir);
        putenv(envvar);
        envvar = NULL; // putenv owns memory

        erlinit_asprintf(&envvar, "RELEASE_TMP=/tmp");
        putenv(envvar);
        envvar = NULL; // putenv owns memory
    }

    // Set any additional environment variables from the user
    if (options.additional_env) {
        char *envstr = strtok(options.additional_env, ";");
        while (envstr) {
            putenv(strdup(envstr));
            envstr = strtok(NULL, ";");
        }
    }

    if (options.core_pattern && set_core_pattern(options.core_pattern) < 0) {
        warn("Failed to set core pattern to '%s'", options.core_pattern);
    }
}

static void update_time()
{
    if (!options.update_clock)
        return;

    debug("checking that the clock is after %d", BUILD_TIME);

    // Force the time to at least the build date.  For systems w/o real-time
    // clocks, the time will be closer to the actual date until NTP kicks in.
    // This makes it possible to use certificates assuming that the build date
    // isn't too old.

    struct timespec tp;
    if (clock_gettime(CLOCK_REALTIME, &tp) < 0) {
        warn("clock_gettime failed. Skipping time check");
        return;
    }

    if (tp.tv_sec < BUILD_TIME) {
        tp.tv_sec = BUILD_TIME;
        tp.tv_nsec = 0;
        debug("updating the clock to %d", tp.tv_sec);
        if (clock_settime(CLOCK_REALTIME, &tp) < 0) {
            warn("clock_settime");
        }
    }
}

static int run_cmd(const char *cmd)
{
    debug("run_cmd '%s'", cmd);

    pid_t pid = fork();
    if (pid == 0) {
        // child
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
        int status = -1;
        int rc;
        do {
            rc = waitpid(pid, &status, 0);
        } while (rc < 0 && errno == EINTR);

        if ((rc < 0 && errno != ECHILD) || rc != pid) {
            warn("unexpected return from waitpid: rc=%d, errno=%d", rc, errno);
            return -1;
        }
        return status;
    }
}

static void drop_privileges()
{
    if (options.gid > 0) {
        debug("setting gid to %d", options.gid);

        OK_OR_FATAL(setgid(options.gid), "setgid failed");
    }
    if (options.uid > 0) {
        debug("setting uid to %d", options.uid);

        OK_OR_FATAL(setuid(options.uid), "setuid failed");
    }
}

static char **concat_options(char **argv, const char *option, int append)
{
    if (append) {
        erlinit_asprintf(argv, "%s %s", *argv, option);
    } else {
        *argv = strdup(option);
        argv++;
    }
    return argv;
}

static void child()
{
    update_time();

    mount_filesystems();

    // Locate everything needed to configure the environment
    // and pass to erlexec.
    struct erl_run_info run_info;
    memset(&run_info, 0, sizeof(run_info));

    find_release(&run_info);

    find_erts_directory(run_info.erts_version, run_info.release_base_dir, &run_info.erts_dir);

    // Set up $HOME
    setup_home_directory();

    // Set up the environment for running erlang.
    setup_environment(&run_info);

    // Set up the minimum networking we need for Erlang.
    setup_networking();

    // Warn the user if they're on an inactive TTY
    if (options.warn_unused_tty)
        warn_unused_tty();

    // Set the working directory. First try a directory specified
    // in the options, but if that doesn't work, go to the root of
    // the release.
    if (options.working_directory == NULL ||
            chdir(options.working_directory) < 0) {
        OK_OR_FATAL(chdir(run_info.release_base_dir), "Cannot chdir to release directory (%s)",
                    run_info.release_base_dir);
    }

    // Restore the seed for the random number generator (failures ignored)
    //
    // This has to happen after `mount_filesystems()` and setting up the
    // environment. Running it before pre_run_exec allows that program to use
    // random numbers.
    //
    // It's not uncommon for pre_run_exec to be used to start hardware entropy
    // daemons to help with random number generation too.
    seedrng();

    // Optionally run a "pre-run" program
    if (options.pre_run_exec)
        run_cmd(options.pre_run_exec);

    // Optionally drop privileges
    drop_privileges();

    // Start Erlang up
    char erlexec_path[ERLINIT_PATH_MAX];
    char alternate_exec_path[ERLINIT_PATH_MAX];
    snprintf(erlexec_path, sizeof(erlexec_path), "%s/bin/erlexec", run_info.erts_dir);
    char *exec_path = erlexec_path;

    char *exec_argv[64];
    memset(exec_argv, 0, sizeof(exec_argv));

    char **argv = exec_argv;
    // If there's an alternate exec and it's set properly, then use it.
    char *alternate_exec_program = NULL;
    int append = 0;
    if (options.alternate_exec &&
        (alternate_exec_program = strtok(options.alternate_exec, " ")) &&
        alternate_exec_program[0] != '\0') {
        // If the alternate exec is a relative path, force it to the ERTS
        // directory. Otherwise the user should pass absolute paths.
        if (alternate_exec_program[0] == '/') {
            exec_path = alternate_exec_program;
        } else {
            snprintf(alternate_exec_path, sizeof(alternate_exec_path), "%s/bin/%s", run_info.erts_dir, alternate_exec_program);
            exec_path = alternate_exec_path;
        }

        argv = concat_options(argv, exec_path, 0); // argv0

        char *arg;
        while ((arg = strtok(NULL, " ")) != NULL) {
            argv = concat_options(argv, arg, append);

            // Hack around run_erl calling "sh -c" and requiring "exec" and
            // everything after it to be quoted. I'm guessing that any other
            // use of "exec" will also be for an "sh -c", since "exec" is a
            // shell command. I really hope that it's never a legit argument
            // to start erlang.
            if (strcmp(arg, "exec") == 0) {
                append = 1;
                argv--;
            }
        }

        argv = concat_options(argv, erlexec_path, append);
    } else
        argv = concat_options(argv, "erlexec", 0); // argv0

    if (run_info.consolidated_protocols_path) {
        argv = concat_options(argv, "-pa", append);
        argv = concat_options(argv, run_info.consolidated_protocols_path, append);
    }
    if (run_info.sys_config) {
        argv = concat_options(argv, "-config", append);
        argv = concat_options(argv, run_info.sys_config, append);
    }
    if (run_info.boot_path) {
        argv = concat_options(argv, "-boot", append);
        argv = concat_options(argv, run_info.boot_path, append);
    }
    if (run_info.vmargs_path) {
        argv = concat_options(argv, "-args_file", append);
        argv = concat_options(argv, run_info.vmargs_path, append);
    }
    if (has_erts_library_directory()) {
        argv = concat_options(argv, "-boot_var", append);
        argv = concat_options(argv, "ERTS_LIB_DIR", append);
        argv = concat_options(argv, ERLANG_ERTS_LIB_DIR, append);
    }

    // Set the RELEASE_LIB boot_var for Elixir 1.9+ releases
    argv = concat_options(argv, "-boot_var", append);
    argv = concat_options(argv, "RELEASE_LIB", append);
    argv = concat_options(argv, RELEASE_ROOT_LIB, append);

    if (options.verbose) {
        // Dump the environment and commandline
        extern char **environ;
        char **env = environ;
        while (*env != 0)
            debug("Env: '%s'", *env++);

        int i;
        for (i = 0; exec_argv[i] != NULL; i++)
            debug("Arg: '%s'", exec_argv[i]);
    }

    debug("Launching erl...");
    if (options.print_timing)
        warn("stop");

    execvp(exec_path, exec_argv);

    // execvp is not supposed to return
    fatal("execvp failed to run %s: %s", exec_path, strerror(errno));
}

static void disable_core_dumps()
{
    if (set_core_pattern("|/bin/false") < 0)
        warn("Failed to disable core dumps");
}

static void kill_all()
{
    debug("kill_all");

    // Sync as much as possible to disk before going on the process killing spree to
    // reduce I/O from the processes exiting.
    sync();

    disable_core_dumps();

    // Kill processes the nice way
    warn("Sending SIGTERM to all processes");
    kill(-1, SIGTERM);

    sleep(1);

    // Brutal kill the stragglers
    warn("Sending SIGKILL to all processes");
    kill(-1, SIGKILL);
    sync();
}

static void wait_for_graceful_shutdown(pid_t pid, struct erlinit_exit_info *exit_info)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    clock_gettime(CLOCK_MONOTONIC, &exit_info->shutdown_start);
    exit_info->graceful_shutdown_ok = 0; // assume failure

    // Timeout note: The timer gets reset every time we get a signal
    // that's ignored. That doesn't appear to happen in practice, but
    // if it ever did, the total timeout would be longer than you'd expect.
    struct timespec timeout;
    if (options.graceful_shutdown_timeout_ms <= 0)
        options.graceful_shutdown_timeout_ms = 1;
    timeout.tv_sec = options.graceful_shutdown_timeout_ms / 1000;
    timeout.tv_nsec = (options.graceful_shutdown_timeout_ms % 1000) * 1000000;

    for (;;) {
        debug("waiting %d ms for graceful shutdown", options.graceful_shutdown_timeout_ms);
        int rc = sigtimedwait(&mask, NULL, &timeout);
        if (rc == SIGCHLD) {
            rc = waitpid(-1, &exit_info->wait_status, WNOHANG);
            if (rc == pid) {
                debug("graceful shutdown detected");
                exit_info->graceful_shutdown_ok = 1;
                break;
            } else if (rc < 0) {
                warn("Unexpected error from waitpid %d", errno);
                break;
            } else if (rc == 0) {
                // No child exited
                debug("Ignoring spurious SIGCHLD");
            } else {
                debug("Ignoring SIGCHLD from pid %d", rc);
            }
        } else if (rc == 0) {
            warn("Ignoring unexpected return from sigtimedwait");
        } else if (rc < 0) {
            if (errno == EAGAIN) {
                // Timeout. Brutal kill our child so that the shutdown process can continue.
                warn("Graceful shutdown timer expired (%d ms). Killing Erlang VM process shortly. Adjust timeout with --graceful-shutdown-timeout option.",
                     options.graceful_shutdown_timeout_ms);
                break;
            } else if (errno != EINTR) {
                warn("Unexpected errno %d from sigtimedwait", errno);
                break;
            }
        } else if (rc > 0) {
            warn("Ignoring signal %d while waiting for graceful shutdown", rc);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &exit_info->shutdown_complete);
}

static void read_reboot_args(char *args, size_t max_length)
{
    FILE *fp = fopen("/run/reboot-param", "r");
    if (fp == NULL) {
        args[0] = '\0';
        return;
    }

    if (fgets(args, max_length, fp) != NULL) {
        trim_whitespace(args);
    } else {
        args[0] = '\0';
    }
    fclose(fp);
}

static void fork_and_wait(struct erlinit_exit_info *exit_info)
{
    sigset_t mask;
    sigset_t orig_mask;

    memset(exit_info, 0, sizeof(struct erlinit_exit_info));

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGPWR);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGTERM);

    // Block signals from the child process so that they're
    // handled by sigtimedwait.
    if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) < 0)
        fatal("sigprocmask(SIG_BLOCK) failed");

    // Do most of the work in a child process so that if it
    // crashes, we can handle the crash.
    pid_t pid = fork();
    if (pid == 0) {
        // Unblock signals in the child
        if (sigprocmask(SIG_SETMASK, &orig_mask, NULL) < 0)
            fatal("sigprocmask(SIG_SETMASK) failed");

        child();
        exit(1);
    }

    exit_info->wait_status = 0;
    for (;;) {
        int rc = sigwaitinfo(&mask, NULL);
        if (rc == SIGCHLD) {
            // Child process exited
            //   Reap all processes that exited
            //   If our immediate child exited, exit too
            do {
                rc = waitpid(-1, &exit_info->wait_status, WNOHANG);
                if (rc == pid)
                    goto prepare_to_exit;
                else if (rc > 0)
                    debug("reaped pid %d", rc);
            } while (rc > 0);
        } else if (rc < 0) {
            // An error occurred.
            debug("sigwaitinfo->errno %d", errno);
            if (errno != EINTR)
                fatal("Unexpected error from sigwaitinfo: %d", errno);
        } else if (rc == SIGPWR || rc == SIGUSR1) {
            // Halt request
            debug("sigpwr|sigusr1 -> halt");
            exit_info->desired_reboot_cmd = LINUX_REBOOT_CMD_HALT;
            wait_for_graceful_shutdown(pid, exit_info);
            break;
        } else if (rc == SIGTERM) {
            // Reboot request
            debug("sigterm -> reboot");
            read_reboot_args(exit_info->reboot_args, sizeof(exit_info->reboot_args));
            exit_info->desired_reboot_cmd = exit_info->reboot_args[0] == '\0' ? LINUX_REBOOT_CMD_RESTART : LINUX_REBOOT_CMD_RESTART2;
            wait_for_graceful_shutdown(pid, exit_info);
            break;
        } else if (rc == SIGUSR2) {
            debug("sigusr2 -> power off");
            exit_info->desired_reboot_cmd = LINUX_REBOOT_CMD_POWER_OFF;
            wait_for_graceful_shutdown(pid, exit_info);
            break;
        } else {
            debug("sigwaitinfo unexpected rc=%d", rc);
        }
    }

prepare_to_exit:
    // Check if this was a clean exit.
    if (exit_info->desired_reboot_cmd != 0) {
        // Intentional exit since reboot/poweroff/halt was called.
        exit_info->is_intentional_exit = 1;

        if (WIFSIGNALED(exit_info->wait_status))
            warn("Ignoring unexpected error during shutdown.");
    } else {
        // Unintentional exit either due to a crash or an actual call to exit()
        exit_info->is_intentional_exit = 0;
        exit_info->desired_reboot_cmd = options.unintentional_exit_cmd;
        if (WIFSIGNALED(exit_info->wait_status))
            warn("Erlang terminated due to signal %d", WTERMSIG(exit_info->wait_status));
        else
            warn("Erlang VM exited");
    }
}


/* See reboot(2) for details about the reboot command with arguments (arg is a NULL-terminated string)*/
#include <sys/syscall.h>
static inline int reboot_with_args(int cmd, const void *arg) {
    return (int) syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, arg);
}

int main(int argc, char *argv[])
{
    if (getpid() != 1)
        fatal("Refusing to run since not pid 1");

    // Merge the config file and the command line arguments
    static int merged_argc;
    static char *merged_argv[MAX_ARGC];
    merge_config(argc, argv, &merged_argc, merged_argv);

    parse_args(merged_argc, merged_argv);

    if (options.print_timing)
        warn("start");

    debug("Starting " PROGRAM_NAME " " PROGRAM_VERSION_STR "...");

    debug("cmdline argc=%d, merged argc=%d", argc, merged_argc);
    int i;
    for (i = 0; i < merged_argc; i++)
        debug("merged argv[%d]=%s", i, merged_argv[i]);

    // Set up experimental writable file system overlay
    if (options.x_pivot_root_on_overlayfs)
        pivot_root_on_overlayfs();

    // Mount /dev, /proc and /sys
    setup_pseudo_filesystems();

    // Create symlinks for partitions on the drive containing the
    // root filesystem.
    create_rootdisk_symlinks();

    // Fix the terminal settings so output goes to the right
    // terminal and the CTRL keys work in the shell..
    set_ctty();

    // Set resource limits. This has to be done before fork.
    create_limits();

    struct erlinit_exit_info exit_info;
    fork_and_wait(&exit_info);

    // If the user specified a command to run on an unexpected exit, run it.
    if (options.run_on_exit && !exit_info.is_intentional_exit)
        run_cmd(options.run_on_exit);

    // Exit everything that's still running.
    kill_all();

    // Dump state for post-mortem analysis of why the power off or reboot occurred.
    if (options.shutdown_report)
        shutdown_report_create(options.shutdown_report, &exit_info);

    // Save the seed for the random number generator (failures ignored)
    seedrng();

    // Unmount almost everything.
    unmount_all();

    // Sync just to be safe.
    sync();

    // See if the user wants us to halt or poweroff on an "unintentional" exit
    if (!exit_info.is_intentional_exit &&
            options.unintentional_exit_cmd != LINUX_REBOOT_CMD_RESTART) {
        // Sometimes Erlang exits on initialization. Hanging on exit
        // makes it easier to debug these cases since messages don't
        // keep scrolling on the screen.
        warn("Not rebooting on exit as requested by the erlinit configuration...");

        // Make sure that the user sees the message.
        sleep(5);
    }

    // Close stdio file handles to avoid hanging the musb driver when running
    // g_serial. Not all platforms hang when these are left open, but it seems
    // like a reasonable thing to do especially since we can't use them
    // anyway.
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Reboot/poweroff/halt
    if (exit_info.reboot_args[0] != '\0')
        reboot_with_args(exit_info.desired_reboot_cmd, exit_info.reboot_args);
    else
        reboot(exit_info.desired_reboot_cmd);

    // If we get here, oops the kernel.
    return 0;
}
