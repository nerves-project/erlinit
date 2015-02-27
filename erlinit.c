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

#include <ctype.h>
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
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>

#define PROGRAM_NAME "erlinit"

#define ERLANG_ROOT_DIR "/usr/lib/erlang"
#define DEFAULT_RELEASE_ROOT_DIR "/srv/erlang"

// This is the maximum number of mounted filesystems that
// is expected in a running system. It is used on shutdown
// when trying to unmount everything gracefully.
#define MAX_MOUNTS 32

#define MAX_ARGC 32

static int merged_argc;
static char *merged_argv[MAX_ARGC];

static int verbose = 0;
static int print_timing = 0;
static int regression_test_mode = 0;
static int desired_reboot_cmd = -1;

static char erts_dir[PATH_MAX];
static char release_info_dir[PATH_MAX];
static char release_root_dir[PATH_MAX];
static char boot_path[PATH_MAX];
static char sys_config[PATH_MAX];
static char vmargs_path[PATH_MAX];
static char *controlling_terminal = NULL;
static char *alternate_exec = NULL;
static char *additional_env = NULL;
static char *release_search_path = NULL;
static char *extra_mounts = NULL;

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

        fprintf(stderr, "\r\n");
    }
}

static void warn(const char *fmt, ...)
{
    print_prefix();

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\r\n");
}

static void fatal(const char *fmt, ...)
{
    fprintf(stderr, "\r\n\r\nFATAL ERROR:\r\n");

    print_prefix();

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\r\n\r\nCANNOT CONTINUE.\r\n");

    if (!regression_test_mode)
        sleep(9999);

    exit(1);
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

static void set_ctty()
{
    debug("set_ctty");
    if (regression_test_mode)
        return;

    // Set up a controlling terminal for Erlang so that
    // it's possible to get to shell management mode.
    // See http://www.busybox.net/FAQ.html#job_control
    setsid();

    char ttypath[32];

    // Check if the user is forcing the controlling terminal
    if (controlling_terminal &&
	strlen(controlling_terminal) < sizeof(ttypath) - 5) {
	sprintf(ttypath, "/dev/%s", controlling_terminal);
    } else {
	// Pick the active console(s)
	strcpy(ttypath, "/dev/");
	readsysfs("/sys/class/tty/console/active", &ttypath[5], sizeof(ttypath) - 5);

	// It's possible that multiple consoles are active, so pick the first one.
	char *sep = strchr(&ttypath[5], ' ');
	if (sep)
	    *sep = 0;
    }

    int fd = open(ttypath, O_RDWR);
    if (fd >= 0) {
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
        char dirpath[PATH_MAX];
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
        char dirpath[PATH_MAX];
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

    if (release_search_path == NULL)
        release_search_path = strdup(DEFAULT_RELEASE_ROOT_DIR);

    // The user may specify several directories to be searched for
    // releases. Pick the first one.
    const char *search_path = strtok(release_search_path, ":");
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
    putenv("HOME=/root");

    // PATH appears to only be needed for user convenience when running os:cmd/1
    // It may be possible to remove in the future.
    putenv("PATH=/usr/sbin:/usr/bin:/sbin:/bin");
    putenv("TERM=vt100");

    // Erlang environment

    // ROOTDIR points to the release unless it wasn't found.
    char envvar[PATH_MAX];
    sprintf(envvar, "ROOTDIR=%s", release_root_dir);
    putenv(strdup(envvar));

    // BINDIR points to the erts bin directory.
    sprintf(envvar, "BINDIR=%s/bin", erts_dir);
    putenv(strdup(envvar));

    putenv("EMU=beam");
    putenv("PROGNAME=erl");

    // Set any additional environment variables from the user
    if (additional_env) {
        char *envstr = strtok(additional_env, ";");
        while (envstr) {
            putenv(envstr);
            envstr = strtok(NULL, ";");
        }
    }
}

static void enable_loopback()
{
    // Set the loopback interface to up
    int fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        warn("socket(PF_INET) failed");
        return;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_name[0] = 'l';
    ifr.ifr_name[1] = 'o';
    ifr.ifr_name[2] = '\0';
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        warn("SIOCGIFFLAGS failed on lo");
        goto cleanup;
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr)) {
        warn("SIOCSIFFLAGS failed on lo");
        goto cleanup;
    }

    // Currently only configuring IPv4.
    struct sockaddr_in *addr = (struct sockaddr_in *) &ifr.ifr_addr;
    addr->sin_family = AF_INET;
    addr->sin_port = 0;
    addr->sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
    if (ioctl(fd, SIOCSIFADDR, &ifr)) {
        warn("SIOCSIFADDR failed on lo");
        goto cleanup;
    }

    addr->sin_addr.s_addr = htonl(0xff000000); // 255.0.0.0
    if (ioctl(fd, SIOCSIFNETMASK, &ifr)) {
        warn("SIOCSIFNETMASK failed on lo");
        goto cleanup;
    }

cleanup:
    close(fd);
}

static void setup_networking()
{
    debug("setup_networking");
    if (regression_test_mode)
        return;

    // Bring up the loopback interface (needed if the erlang distribute protocol code gets run)
    enable_loopback();
    configure_hostname();
}

static unsigned long str_to_mountflags(char *s)
{
    unsigned long flags = 0;

    // These names correspond to ones used by mount(8)
    char *flag = strtok(s, ",");
    while (flag) {
        if (strcmp(flag, "dirsync") == 0)
            flags |= MS_DIRSYNC;
        else if (strcmp(flag, "mand") == 0)
            flags |= MS_MANDLOCK;
        else if (strcmp(flag, "noatime") == 0)
            flags |= MS_NOATIME;
        else if (strcmp(flag, "nodev") == 0)
            flags |= MS_NODEV;
        else if (strcmp(flag, "nodiratime") == 0)
            flags |= MS_NODIRATIME;
        else if (strcmp(flag, "noexec") == 0)
            flags |= MS_NOEXEC;
        else if (strcmp(flag, "nosuid") == 0)
            flags |= MS_NOSUID;
        else if (strcmp(flag, "ro") == 0)
            flags |= MS_RDONLY;
        else if (strcmp(flag, "relatime") == 0)
            flags |= MS_RELATIME;
        else if (strcmp(flag, "silent") == 0)
            flags |= MS_SILENT;
        else if (strcmp(flag, "strictatime") == 0)
            flags |= MS_STRICTATIME;
        else if (strcmp(flag, "sync") == 0)
            flags |= MS_SYNCHRONOUS;
        else
            warn("Unrecognized filesystem mount flag: %s", flag);

        flag = strtok(NULL, ",");
    }
    return flags;
}

static void setup_pseudo_filesystems()
{
    // Since we can't mount anything in this environment, just return.
    if (regression_test_mode)
        return;

    if (mount("", "/proc", "proc", 0, NULL) < 0)
        warn("Cannot mount /proc");
    if (mount("", "/sys", "sysfs", 0, NULL) < 0)
        warn("Cannot mount /sys");

    // /dev should be automatically created/mounted by Linux
    if (mkdir("/dev/pts", 0755) < 0)
        warn("Cannot create /dev/pts");
    if (mkdir("/dev/shm", 0755) < 0)
        warn("Cannot create /dev/shm");
    if (mount("", "/dev/pts", "devpts", 0, "gid=5,mode=620") < 0)
        warn("Cannot mount /dev/pts");
}

static void setup_filesystems()
{
    // Since we can't mount anything in this environment, just return.
    if (regression_test_mode)
        return;

    // Mount /tmp and /run since they're almost always needed and it's
    // not easy to do it at the right time in Erlang.
    if (mount("", "/tmp", "tmpfs", 0, "mode=1777,size=10%") < 0)
        warn("Could not mount tmpfs in /tmp: %s\r\n"
              "Check that tmpfs support is enabled in the kernel config.", strerror(errno));

    if (mount("", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755,size=5%") < 0)
        warn("Could not mount tmpfs in /run: %s", strerror(errno));

    // Mount any filesystems specified by the user. This is best effort.
    // The user is required to figure out if anything went wrong in their
    // applications. For example, the filesystem might not be formatted
    // yet, and erlinit is not smart enough to figure that out.
    //
    // An example mount specification looks like:
    //    /dev/mmcblk0p4:/mnt:vfat::utf8
    if (extra_mounts) {
        char *temp = extra_mounts;
        const char *source = strsep(&temp, ":");
        const char *target = strsep(&temp, ":");
        const char *filesystemtype = strsep(&temp, ":");
        char *mountflags = strsep(&temp, ":");
        const char *data = strsep(&temp, ":");

        if (source && target && filesystemtype && mountflags && data) {
            unsigned long imountflags =
                str_to_mountflags(mountflags);
            if (mount(source, target, filesystemtype, imountflags, data) < 0)
                warn("Cannot mount %s at %s: %s", source, target, strerror(errno));
        } else {
            warn("Invalid parameter to -m. Expecting 5 colon-separated fields");
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

    chdir(release_root_dir);

    // Start Erlang up
    char erlexec_path[PATH_MAX];
    sprintf(erlexec_path, "%s/bin/erlexec", erts_dir);
    char *exec_path = erlexec_path;

    char *exec_argv[32];
    int arg = 0;
    // If there's an alternate exec and it's set properly, then use it.
    char *alternate_exec_path = strtok(alternate_exec, " ");
    if (alternate_exec && alternate_exec_path && alternate_exec_path[0] != '\0') {
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

    if (verbose) {
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
    if (print_timing)
        warn("stop");

    execvp(exec_path, exec_argv);

    // execvpe is not supposed to return
    fatal("execvp failed to run %s: %s", exec_path, strerror(errno));
}

static void unmount_all()
{
    debug("unmount_all");
    if (regression_test_mode)
        return;

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
           fscanf(fp, "%255s %255s %31s %127s %d %d", mounts[i].source, mounts[i].target, mounts[i].fstype, options, &freq, &passno) >= 3) {
        i++;
    }
    fclose(fp);

    // Unmount as much as we can in reverse order.
    int num_mounts = i;
    for (i = num_mounts - 1; i >= 0; i--) {
        // Whitelist directories that don't unmount or
        // remount immediately (rootfs)
        if (strcmp(mounts[i].source, "devtmpfs") == 0 ||
            strcmp(mounts[i].source, "/dev/root") == 0 ||
            strcmp(mounts[i].source, "rootfs") == 0)
            continue;

        debug("unmounting %s(%s)...", mounts[i].source, mounts[i].target);
        if (umount(mounts[i].target) < 0 && umount(mounts[i].source) < 0)
            warn("umount %s(%s) failed: %s", mounts[i].source, mounts[i].target, strerror(errno));
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
    debug("kill_all");
    if (regression_test_mode)
        return;

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

int parse_config_line(char *line, char **argv, int max_args)
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
int load_config(const char *filename,
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

void merge_config(int argc, char *argv[])
{
    // When merging, argv[0] is first, then the
    // arguments from erlinit.config and then any
    // additional arguments from the commandline.
    // This way, the commandline takes precedence.
    merged_argc = 1;
    merged_argv[0] = argv[0];

    merged_argc += load_config("/etc/erlinit.config",
			       &merged_argv[1],
			       MAX_ARGC - argc);

    if (merged_argc + argc - 1 > MAX_ARGC) {
        warn("Too many arguments specified between the config file and commandline. Dropping some.");
        argc = MAX_ARGC - merged_argc + 1;
    }

    memcpy(&merged_argv[merged_argc], &argv[1], (argc - 1) * sizeof(char**));
    merged_argc += argc - 1;
}

int main(int argc, char *argv[])
{
    // erlinit should be pid 1. Anything else is not expected, so run in
    // regression test mode.
    if (getpid() != 1)
        regression_test_mode = 1;

    // Merge the config file and the command line arguments
    merge_config(argc, argv);

    int hang_on_exit = 0;
    int opt;

    while ((opt = getopt(merged_argc, merged_argv, "c:e:hm:r:s:tv")) != -1) {
        switch (opt) {
	case 'c':
	    controlling_terminal = strdup(optarg);
	    break;
        case 'e':
            additional_env = strdup(optarg);
            break;
        case 'h':
            hang_on_exit = 1;
            break;
        case 'm':
            extra_mounts = strdup(optarg);
            break;
        case 'r':
            release_search_path = strdup(optarg);
            break;
        case 's':
            alternate_exec = strdup(optarg);
            break;
        case 't':
            print_timing = 1;
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
    if (hang_on_exit && !is_intentional_exit) {
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
