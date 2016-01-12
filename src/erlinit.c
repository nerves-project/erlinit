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

#define _GNU_SOURCE // for asprintf

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/reboot.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>

static char erts_dir[ERLINIT_PATH_MAX];
static char release_info_dir[ERLINIT_PATH_MAX];
static char release_root_dir[ERLINIT_PATH_MAX];
static char boot_path[ERLINIT_PATH_MAX];
static char sys_config[ERLINIT_PATH_MAX];
static char vmargs_path[ERLINIT_PATH_MAX];

static int desired_reboot_cmd = 0;

static int starts_with(const char *str, const char *what)
{
    return strstr(str, what) == str;
}

static int erts_filter(const struct dirent *d)
{
    return starts_with(d->d_name, "erts-");
}

static void find_erts_directory()
{
    debug("find_erts_directory");
    struct dirent **namelist;
    int n = scandir(ERLANG_ROOT_DIR,
                    &namelist,
                    erts_filter,
                    NULL);
    if (n < 0)
        fatal("Erlang installation not found. Check that %s exists", ERLANG_ROOT_DIR);
    else if (n == 0)
        fatal("erts not found. Check that erlang was installed to %s", ERLANG_ROOT_DIR);
    else if (n > 1)
        fatal("Found multiple erts directories. Clean up the installation.");

    sprintf(erts_dir, "%s/%s", ERLANG_ROOT_DIR, namelist[0]->d_name);

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

static void find_sys_config()
{
    debug("find_sys_config");
    sprintf(sys_config, "%s/sys.config", release_info_dir);
    if (!file_exists(sys_config)) {
        warn("%s not found?", sys_config);
        *sys_config = '\0';
    }
}

static void find_vm_args()
{
    debug("find_vm_args");
    sprintf(vmargs_path, "%s/vm.args", release_info_dir);
    if (!file_exists(vmargs_path)) {
        warn("%s not found?", vmargs_path);
        *vmargs_path = '\0';
    }
}

static void find_boot_path()
{
    debug("find_boot_path");
    struct dirent **namelist;
    int n = scandir(release_info_dir,
                    &namelist,
                    bootfile_filter,
                    NULL);
    if (n <= 0)
        fatal("No boot file found in %s.", release_info_dir);

    if (n > 1)
        warn("Found more than one boot file. Using %s.", namelist[0]->d_name);

    // Use the first
    sprintf(boot_path, "%s/%s", release_info_dir, namelist[0]->d_name);

    // Strip off the .boot since that's what erl wants.
    char *dot = strrchr(boot_path, '.');
    *dot = '\0';

    // Free everything
    while (--n >= 0)
        free(namelist[n]);
    free(namelist);
}

static int is_directory(const char *path)
{
    struct stat sb;
    return stat(path, &sb) == 0 &&
            S_ISDIR(sb.st_mode);
}

static int find_release_info_dir(const char *releases_dir,
                                 char *info_dir)
{
    struct dirent **namelist;
    int n = scandir(releases_dir,
                    &namelist,
                    dotfile_filter,
                    NULL);
    int i;
    int success = 0;
    for (i = 0; i < n; i++) {
        char dirpath[ERLINIT_PATH_MAX];
        sprintf(dirpath, "%s/%s", releases_dir, namelist[i]->d_name);

        // Pick the first directory. There should only be one directory
        // anyway.
        if (is_directory(dirpath)) {
            strcpy(info_dir, dirpath);
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

static int find_release_dirs(const char *base,
                             int depth,
                             char *root_dir,
                             char *info_dir)
{
    // The "releases" directory could either be in the current folder
    // or one directory immediately below. For example,
    //
    // <base/releases/<release_version>
    //
    // -or-
    //
    // <base>/<release_name>/releases/<release_version>
    //
    // Check for both. On return, root_dir is set to the path containing the
    // releases directory, and info_dir is set to one of the paths above.
    int success = 0;
    struct dirent **namelist;
    int n = scandir(base,
                    &namelist,
                    dotfile_filter,
                    NULL);
    int i;
    for (i = 0; i < n; i++) {
        char dirpath[ERLINIT_PATH_MAX];
        sprintf(dirpath, "%s/%s", base, namelist[i]->d_name);

        if (!is_directory(dirpath))
            continue;

        if (strcmp(namelist[i]->d_name, "releases") == 0 &&
                find_release_info_dir(dirpath, info_dir)) {
            strcpy(root_dir, base);
            success = 1;
            break;
        }

        // Recurse to the next directory down if allowed.
        if (depth && find_release_dirs(dirpath, depth - 1, root_dir, info_dir)) {
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

static void find_release()
{
    debug("find_release");

    if (options.release_search_path == NULL)
        options.release_search_path = strdup(DEFAULT_RELEASE_ROOT_DIR);

    // The user may specify several directories to be searched for
    // releases. Pick the first one.
    const char *search_path = strtok(options.release_search_path, ":");
    while (search_path != NULL) {
        if (find_release_dirs(search_path, 1, release_root_dir, release_info_dir)) {
            debug("Using release in %s.", release_info_dir);

            find_sys_config();
            find_vm_args();
            find_boot_path();

            return;
        }

        warn("No release found in %s.", search_path);
        search_path = strtok(NULL, ":");
    }
    *release_info_dir = '\0';
    *sys_config = '\0';
    *boot_path = '\0';

    strcpy(release_root_dir, ERLANG_ROOT_DIR);
}

static void setup_environment()
{
    debug("setup_environment");
    // Set up the environment for running erlang.
    putenv("HOME=/root");

    // PATH appears to only be needed for user convenience when running os:cmd/1
    // It may be possible to remove in the future.
    putenv("PATH=/usr/sbin:/usr/bin:/sbin:/bin");
    putenv("TERM=vt100");

    // Erlang environment

    // ROOTDIR points to the release unless it wasn't found.
    char *envvar;
    OK_OR_FATAL(asprintf(&envvar, "ROOTDIR=%s", release_root_dir), "asprintf failed");
    putenv(envvar);

    // BINDIR points to the erts bin directory.
    OK_OR_FATAL(asprintf(&envvar, "BINDIR=%s/bin", erts_dir), "asprintf failed");
    putenv(envvar);

    putenv("EMU=beam");
    putenv("PROGNAME=erl");

    // Set any additional environment variables from the user
    if (options.additional_env) {
        char *envstr = strtok(options.additional_env, ";");
        while (envstr) {
            putenv(strdup(envstr));
            envstr = strtok(NULL, ";");
        }
    }
}

static void child()
{
    setup_filesystems();

    // Locate everything needed to configure the environment
    // and pass to erlexec.
    find_erts_directory();
    find_release();

    // Set up the environment for running erlang.
    setup_environment();

    // Set up the minimum networking we need for Erlang.
    setup_networking();

    OK_OR_FATAL(chdir(release_root_dir), "Cannot chdir to release directory (%s)", release_root_dir);

    // Start Erlang up
    char erlexec_path[ERLINIT_PATH_MAX];
    sprintf(erlexec_path, "%s/bin/erlexec", erts_dir);
    char *exec_path = erlexec_path;

    char *exec_argv[32];
    int arg = 0;
    // If there's an alternate exec and it's set properly, then use it.
    char *alternate_exec_path = strtok(options.alternate_exec, " ");
    if (options.alternate_exec && alternate_exec_path && alternate_exec_path[0] != '\0') {
        exec_path = alternate_exec_path;
        exec_argv[arg++] = exec_path;

        while ((exec_argv[arg] = strtok(NULL, " ")) != NULL)
            arg++;

        exec_argv[arg++] = erlexec_path;
    } else
        exec_argv[arg++] = "erlexec";

    if (*sys_config) {
        exec_argv[arg++] = "-config";
        exec_argv[arg++] = sys_config;
    }
    if (*boot_path) {
        exec_argv[arg++] = "-boot";
        exec_argv[arg++] = boot_path;
    }
    if (*vmargs_path) {
        exec_argv[arg++] = "-args_file";
        exec_argv[arg++] = vmargs_path;
    }

    exec_argv[arg] = NULL;

    if (options.verbose) {
        // Dump the environment and commandline
        extern char **environ;
        char** env = environ;
        while (*env != 0)
            debug("Env: '%s'", *env++);

        int i;
        for (i = 0; i < arg; i++)
            debug("Arg: '%s'", exec_argv[i]);
    }

    debug("Launching erl...");
    if (options.print_timing)
        warn("stop");

    execvp(exec_path, exec_argv);

    // execvpe is not supposed to return
    fatal("execvp failed to run %s: %s", exec_path, strerror(errno));
}

static void signal_handler(int signum);

static void register_signal_handlers()
{
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigaction(SIGPWR, &act, NULL);
    sigaction(SIGUSR1, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGUSR2, &act, NULL);
}

static void unregister_signal_handlers()
{
    struct sigaction act;

    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigaction(SIGPWR, &act, NULL);
    sigaction(SIGUSR1, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGUSR2, &act, NULL);
}

void signal_handler(int signum)
{
    switch (signum) {
    case SIGPWR:
    case SIGUSR1:
        desired_reboot_cmd = LINUX_REBOOT_CMD_HALT;
        break;
    case SIGTERM:
        desired_reboot_cmd = LINUX_REBOOT_CMD_RESTART;
        break;
    case SIGUSR2:
        desired_reboot_cmd = LINUX_REBOOT_CMD_POWER_OFF;
        break;
    default:
        warn("received unexpected signal %d", signum);
        desired_reboot_cmd = LINUX_REBOOT_CMD_RESTART;
        break;
    }

    // Handling the signal is a one-time action. Now we're done.
    unregister_signal_handlers();
}

static void kill_all()
{
    debug("kill_all");

#ifndef UNITTEST
    // Kill processes the nice way
    kill(-1, SIGTERM);
    warn("Sending SIGTERM to all processes");
    sync();

    sleep(1);

    // Brutal kill the stragglers
    kill(-1, SIGKILL);
    warn("Sending SIGKILL to all processes");
    sync();
#endif
}

int main(int argc, char *argv[])
{
#ifndef UNITTEST
    if (getpid() != 1)
        fatal("Refusing to run since not pid 1");
#else
    if (getpid() == 1)
        fatal("Trying to run unit test version of erlinit for real. This won't work.");
#endif

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

    // Mount /dev, /proc and /sys
    setup_pseudo_filesystems();

    // Fix the terminal settings so output goes to the right
    // terminal and the CTRL keys work in the shell..
    set_ctty();

    // Do most of the work in a child process so that if it
    // crashes, we can handle the crash.
    pid_t pid = fork();
    if (pid == 0) {
        child();
        exit(1);
    }

    // Register signal handlers to catch requests to exit
    register_signal_handlers();

    // If Erlang exits, then something went wrong, so handle it.
    int is_intentional_exit = 0;
    if (waitpid(pid, 0, 0) < 0) {
        debug("signal or error terminated waitpid. clean up");

        // If waitpid fails and it's not because of a signal, print a warning.
        if (desired_reboot_cmd < 0) {
            warn("unexpected error from waitpid(): %s", strerror(errno));
            desired_reboot_cmd = LINUX_REBOOT_CMD_RESTART;
        } else {
            // A signal is sent from commands like poweroff, reboot, and halt
            // This is usually intentional.
            is_intentional_exit = 1;
        }
    } else {
        debug("Erlang VM exited");

        desired_reboot_cmd = LINUX_REBOOT_CMD_RESTART;
    }

    // Exit everything that's still running.
    kill_all();

    // Unmount almost everything.
    unmount_all();

    // Sync just to be safe.
    sync();

    // See if the user wants us to hang on "unintentional" exit
    if (options.hang_on_exit && !is_intentional_exit) {
        // Sometimes Erlang exits on initialization. Hanging on exit
        // makes it easier to debug these cases since messages don't
        // keep scrolling on the screen.
        fatal("Hanging as requested by the erlinit configuration...");
    }

    // Reboot/poweroff/halt
    reboot(desired_reboot_cmd);

    // If we get here, oops the kernel.
    return 0;
}
