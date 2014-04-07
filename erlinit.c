/*
The MIT License (MIT)

Copyright (c) 2013-14 Frank Hunleth

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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/reboot.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define PROGRAM_NAME "erlinit"

#define ERLANG_ROOT_DIR "/usr/lib/erlang"
#define RELEASE_ROOT_DIR "/srv/erlang"
#define RELEASE_RELEASES_DIR  RELEASE_ROOT_DIR "/releases"

#define MAX_MOUNTS 32

static int verbose = 0;
static int run_strace = 0;
static int print_timing = 0;
static int desired_reboot_cmd = -1;

static char erts_dir[PATH_MAX];
static char release_dir[PATH_MAX];
static char root_dir[PATH_MAX];
static char boot_path[PATH_MAX];
static char sys_config[PATH_MAX];
static char vmargs_path[PATH_MAX];

static void print_prefix()
{
    fprintf(stderr, PROGRAM_NAME ": ");
}

static void debug(const char *fmt, ...)
{
    if (verbose) {
        print_prefix();

        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);

        fprintf(stderr, "\n");
    }
}

static void warn(const char *fmt, ...)
{
    print_prefix();

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
}

static void fatal(const char *fmt, ...)
{
    fprintf(stderr, "\n\nFATAL ERROR:\n");

    print_prefix();

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n\nCANNOT CONTINUE.\n");

    sleep(9999);
}

static int readsysfs(const char *path, char *buffer, int maxlen)
{
    debug("readsysfs %s", path);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    int count = read(fd, buffer, maxlen - 1);

    close(fd);
    if (count <= 0)
        return 0;

    // Trim trailing \n
    count--;
    buffer[count] = 0;
    return count;
}

static void fix_ctty()
{
    debug("fix_ctty");
    // Set up a controlling terminal for Erlang so that
    // it's possible to get to shell management mode.
    // See http://www.busybox.net/FAQ.html#job_control
    setsid();

    char ttypath[32];
    strcpy(ttypath, "/dev/");
    readsysfs("/sys/class/tty/console/active", &ttypath[5], sizeof(ttypath) - 5);

    int fd = open(ttypath, O_RDWR);
    if (fd > 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    } else {
        warn("error setting controlling terminal: %s", ttypath);
    }
}

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
        fatal("scandir failed: %s", strerror(errno));
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

static int release_filter(const struct dirent *d)
{
    // Releases are directories usually with names of
    // the form NAME-VERSION or VERSION.
    //
    // Sadly, it looks like the robust way of doing this
    // is by using process of elimination.
    return strcmp(d->d_name, ".") != 0 &&
           strcmp(d->d_name, "..") != 0 &&
           strcmp(d->d_name, "RELEASES") != 0;
}

static int bootfile_filter(const struct dirent *d)
{
    // Look for files that end with .boot
    return strstr(d->d_name, ".boot") != NULL;
}

static void find_sys_config()
{
    debug("find_sys_config");
    sprintf(sys_config, "%s/sys.config", release_dir);
    if (!file_exists(sys_config)) {
        warn("%s not found?", sys_config);
        *sys_config = '\0';
    }
}

static void find_vm_args()
{
    debug("find_vm_args");
    sprintf(vmargs_path, "%s/vm.args", release_dir);
    if (!file_exists(vmargs_path)) {
        warn("%s not found?", vmargs_path);
        *vmargs_path = '\0';
    }
}

static void find_boot_path()
{
    debug("find_boot_path");
    struct dirent **namelist;
    int n = scandir(release_dir,
                    &namelist,
                    bootfile_filter,
                    NULL);
    if (n <= 0) {
        fatal("No boot file found in %s.", release_dir);
    } else if (n == 1) {
        sprintf(boot_path, "%s/%s", release_dir, namelist[0]->d_name);

        // Strip off the .boot since that's what erl wants.
        char *dot = strrchr(boot_path, '.');
        *dot = '\0';

        free(namelist[0]);
        free(namelist);
    } else {
        warn("Found more than one boot file:");
        int i;
        for (i = 0; i < n; i++)
            warn("  %s\n", namelist[i]->d_name);
        fatal("Not sure which one to use.");
    }
}

static void find_release()
{
    debug("find_release");
    struct dirent **namelist;
    int n = scandir(RELEASE_RELEASES_DIR,
                    &namelist,
                    release_filter,
                    NULL);
    if (n <= 0) {
        warn("No release found in %s.", RELEASE_RELEASES_DIR);
        *release_dir = '\0';
        *sys_config = '\0';
        *boot_path = '\0';

        strcpy(root_dir, ERLANG_ROOT_DIR);
    } else if (n == 1) {
        sprintf(release_dir, "%s/%s", RELEASE_RELEASES_DIR, namelist[0]->d_name);
        strcpy(root_dir, RELEASE_ROOT_DIR);
        free(namelist[0]);
        free(namelist);

        find_sys_config();
        find_vm_args();
        find_boot_path();
    } else {
        warn("Found more than one release:");
        int i;
        for (i = 0; i < n; i++)
            warn("         %s", namelist[i]->d_name);
        fatal("Not sure which to run.");
    }
}

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

static void configure_hostname()
{
    debug("configure_hostname");
    FILE *fp = fopen("/etc/hostname", "r");
    if (!fp) {
        warn("/etc/hostname not found");
        return;
    }

    char line[128];
    if (fgets(line, sizeof(line), fp)) {
        trim_whitespace(line);

        if (*line == 0)
            warn("Empty hostname");
        else if (sethostname(line, strlen(line)) < 0)
            warn("Error setting hostname: %s", strerror(errno));
    }
    fclose(fp);
}

static void setup_environment()
{
    debug("setup_environment");
    // Set up the environment for running erlang.
    putenv("HOME=/");

    // PATH appears to only be needed for user convenience when running os:cmd/1
    // It may be possible to remove in the future.
    putenv("PATH=/usr/sbin:/usr/bin:/sbin:/bin");
    putenv("TERM=vt100");

    // Erlang environment

    // ROOTDIR points to the release unless it wasn't found.
    char envvar[PATH_MAX];
    sprintf(envvar, "ROOTDIR=%s", root_dir);
    putenv(strdup(envvar));

    // BINDIR points to the erts bin directory.
    sprintf(envvar, "BINDIR=%s/bin", erts_dir);
    putenv(strdup(envvar));

    putenv("EMU=beam");
    putenv("PROGNAME=erl");
}

static void forkexec(const char *path, ...)
{
    debug("forkexec %s", path);
    va_list ap;
#define MAX_ARGS 32
    char *argv[MAX_ARGS];
    int i;

    va_start(ap, path);
    argv[0] = strdup(path);
    for (i = 1; i < MAX_ARGS - 1; i++)
    {
        argv[i] = va_arg(ap, char *);
        if (argv[i] == NULL)
            break;
    }
    argv[i] = 0;
    va_end(ap);

    pid_t pid = fork();
    if (pid == 0) {
        execv(path, argv);
        exit(127);
    } else {
        waitpid(pid, 0, 0);
        free(argv[0]);
    }
    debug("forkexec %s done", path);
}

static void child()
{
    // Mount the virtual file systems.
    mount("", "/proc", "proc", 0, NULL);
    mount("", "/sys", "sysfs", 0, NULL);

    // Locate everything needed to configure the environment
    // and pass to erlexec.
    find_erts_directory();
    find_release();

    // Set up the environment for running erlang.
    setup_environment();

    // Bring up the loopback interface (needed if erlang is a node)
    forkexec("/sbin/ip", "link", "set", "lo", "up", NULL);
    forkexec("/sbin/ip", "addr", "add", "127.0.0.1", "dev", "lo", NULL);
    configure_hostname();

    // Fix the terminal settings so that CTRL keys work.
    fix_ctty();

    chdir(root_dir);

    // Start Erlang up
    char erlexec_path[PATH_MAX];
    sprintf(erlexec_path, "%s/bin/erlexec", erts_dir);

    char *erlargv[32];
    int arg = 0;
    if (run_strace) {
        erlargv[arg++] = "strace";
        erlargv[arg++] = "-f";
        erlargv[arg++] = erlexec_path;
    } else
        erlargv[arg++] = "erlexec";

    if (*sys_config) {
        erlargv[arg++] = "-config";
        erlargv[arg++] = sys_config;
    }
    if (*boot_path) {
        erlargv[arg++] = "-boot";
        erlargv[arg++] = boot_path;
    }
    if (*vmargs_path) {
        erlargv[arg++] = "-args_file";
        erlargv[arg++] = vmargs_path;
    }

    erlargv[arg] = NULL;

    if (verbose) {
        // Dump the environment and commandline
        extern char **environ;
        char** env = environ;
        while (*env != 0)
            debug("Env: '%s'", *env++);

        int i;
        for (i = 0; i < arg; i++)
            debug("Arg: '%s'", erlargv[i]);
    }

    debug("Launching erl...");
    if (print_timing)
        warn("stop");

    execvp(run_strace ? "/usr/bin/strace" : erlexec_path, erlargv);

    // execvpe is not supposed to return
    fatal("execvp failed to run %s: %s", erlexec_path, strerror(errno));
}

static void mount_unionfs()
{
    debug("mount_unionfs");

    // Setup a union filesystem for the rootfs so that the official
    // contents are protected in a read-only fs, but we can still update
    // files when debugging.
    if (mount("", "/mnt/.overlayfs", "tmpfs", 0, "size=10%") < 0) {
        warn("Could not mount tmpfs in /mnt/.overlayfs: %s\n"
             "Check that tmpfs support is enabled in the kernel config.", strerror(errno));
        return;
    }

#if 0
    if (mount("", "/mnt/.unionfs", "unionfs", 0, "dirs=/mnt/.overlayfs=rw:/=ro") < 0) {
        warn("Could not mount unionfs: %s\n"
             "Check that kernel has unionfs patches from http://unionfs.filesystems.org/\n"
             "and that unionfs is enabled in the kernel config.", strerror(errno));
        return;
    }

    if (chdir("/mnt/.unionfs") < 0) {
        warn("Could not change directory to /mnt/.unionfs: %s", strerror(errno));
        return;
    }

    if (mkdir(".oldrootfs", 0755) < 0) {
        warn("Could not create directory .oldrootfs: %s", strerror(errno));
        return;
    }

    if (pivot_root(".", ".oldrootfs") < 0) {
        warn("pivot_root failed: %s", strerror(errno));
        return;
    }
#else
    if (mount("", "/srv", "unionfs", 0, "dirs=/mnt/.overlayfs=rw:/srv=ro") < 0) {
        warn("Could not mount unionfs: %s\n"
             "Check that kernel has unionfs patches from http://unionfs.filesystems.org/\n"
             "and that unionfs is enabled in the kernel config.", strerror(errno));
        return;
    }
#endif
}

static void unmount_all()
{
    debug("unmount_all");
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        warn("/proc/mounts not found");
        return;
    }

    struct mount_info {
        char source[256];
        char target[256];
        char fstype[32];
    } mounts[MAX_MOUNTS];

    char options[128];
    int freq;
    int passno;
    int i = 0;
    while (i < MAX_MOUNTS &&
           fscanf(fp, "%s %s %s %s %d %d", mounts[i].source, mounts[i].target, mounts[i].fstype, options, &freq, &passno) == 6) {
        debug("%s->%s\n", mounts[i].source, mounts[i].target);
        i++;
    }
    fclose(fp);

    // For now, unmount everything with physical storage behind it for now
    // TODO: iterate multiple times until everything unmounts?
    int num_mounts = i;
    for (i = 0; i < num_mounts; i++) {
        if (starts_with(mounts[i].source, "/dev") &&
            strcmp(mounts[i].source, "/dev/root") != 0) {
            if (umount(mounts[i].target) < 0)
                warn("umount %s failed: %s", mounts[i].target, strerror(errno));
        }
    }
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
    // Kill processes the nice way
    kill(-1, SIGTERM);
    warn("Sending SIGTERM to all processes");
    sync();

    sleep(1);

    // Brutal kill the stragglers
    kill(-1, SIGKILL);
    warn("Sending SIGKILL to all processes");
    sync();
}

int main(int argc, char *argv[])
{
    int unionfs = 0;
    int hang_on_exit = 0;
    int opt;
    while ((opt = getopt(argc, argv, "hstuv")) != -1) {
        switch (opt) {
        case 'h':
            hang_on_exit = 1;
            break;
        case 's':
            run_strace = 1;
            break;
        case 't':
            print_timing = 1;
            break;
        case 'u':
            unionfs = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            warn("ignoring command line argument '%c'", opt);
            break;
        }
    }

    if (print_timing)
        warn("start");

    debug("Starting erlinit...");

    debug("argc=%d", argc);
    int i;
    for (i = 0; i < argc; i++)
	debug("argv[%d]=%s", i, argv[i]);

    // If the user wants a unionfs, remount the rootfs first
    if (unionfs)
        mount_unionfs();

    // Mount /tmp since it currently is challenging to do it at the
    // right time in Erlang.
    // NOTE: try to clean this up when the unionfs errors are resolved.
    if (mount("", "/tmp", "tmpfs", 0, "size=10%") < 0) {
        warn("Could not mount tmpfs in /tmp: %s\n"
             "Check that tmpfs support is enabled in the kernel config.", strerror(errno));
        return;
    }

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
    if (waitpid(pid, 0, 0) < 0) {
        // If waitpid fails and it's not because of a signal, print a warning.
        if (desired_reboot_cmd < 0) {
            warn("unexpected error from waitpid(): %s", strerror(errno));
            desired_reboot_cmd = LINUX_REBOOT_CMD_RESTART;
        }
    } else {
        // This is an exit from Erlang.
        if (hang_on_exit) {
            sync();
            // Sometimes Erlang exits on initialization. Hanging on exit
            // makes it easier to debug these cases since messages don't
            // keep scrolling on the screen.
            fatal("unexpected exit. Hanging as requested...");
        }
        desired_reboot_cmd = LINUX_REBOOT_CMD_RESTART;
    }

    // Exit everything that's still running.
    kill_all();

    // Unmount almost everything.
    unmount_all();

    // Sync just to be safe.
    sync();

    // Reboot/poweroff/halt
    reboot(desired_reboot_cmd);

    // If we get here, oops the kernel.
    return 0;
}
