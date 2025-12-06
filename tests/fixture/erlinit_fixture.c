// SPDX-FileCopyrightText: 2018 Frank Hunleth
// SPDX-FileCopyrightText: 2021 Connor Rigby
//
// SPDX-License-Identifier: MIT
//
#define _GNU_SOURCE // for RTLD_NEXT
#include <stdio.h>
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <err.h>
#include <sys/mount.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <termios.h>
#include <pwd.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#ifndef __APPLE__
#include <linux/random.h>
#else
#define RNDADDENTROPY _IOW( 'R', 0x03, int [2] )
#endif

#define log(MSG, ...) do { fprintf(stderr, "fixture: " MSG "\n", ## __VA_ARGS__); } while (0)

#ifndef __APPLE__
#define ORIGINAL(name) original_##name
#define REPLACEMENT(name) name
#define OVERRIDE(ret, name, args) \
    static ret (*original_##name) args; \
    __attribute__((constructor)) void init_##name() { ORIGINAL(name) = dlsym(RTLD_NEXT, #name); } \
    ret REPLACEMENT(name) args

#define REPLACE(ret, name, args) \
    ret REPLACEMENT(name) args
#else
#define ORIGINAL(name) name
#define REPLACEMENT(name) new_##name
#define OVERRIDE(ret, name, args) \
    ret REPLACEMENT(name) args; \
    __attribute__((used)) static struct { const void *original; const void *replacement; } _interpose_##name \
    __attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&REPLACEMENT(name), (const void*)(unsigned long)&ORIGINAL(name) }; \
    ret REPLACEMENT(name) args

#define REPLACE(ret, name, args) OVERRIDE(ret, name, args)
#endif

static pid_t starting_pid = 1;
static const char *work = NULL;

__attribute__((constructor)) void fixture_init()
{
    // WARNING: This is hacky!!!
    starting_pid = getpid();
    if (starting_pid == 1)
        errx(EXIT_FAILURE, "getpid() overridden before it should have been");

    work = getenv("WORK");

    // Don't wrap child processes
    unsetenv("LD_PRELOAD");
    unsetenv("DYLD_INSERT_LIBRARIES");
}

static int fixup_path(const char *input, char *output)
{
    // All paths from erlinit should be absolute
    if (input[0] != '/') {
        log("Non-absolute path detected: \"%s\"", input);
        return -1;
    }

    // Prepend the working directory to the path
    sprintf(output, "%s%s", work, input);
    return 0;
}

#ifdef __APPLE__
REPLACE(int, mount, (const char *type, const char *dir, int flags, void *data))
{
    // See compat.h for how we put the filesystemtype in the data so that we
    // can print it out here.
    const char *filesystemtype = data;

    log("mount(\"%s\", \"%s\", \"%s\", %d, data)", type, dir, filesystemtype, flags);
    return 0;
}

REPLACE(int, unmount, (const char *target, int flags))
{
    (void) flags;

    log("umount(\"%s\")", target);
    return 0;
}
#else
REPLACE(int, mount, (const char *source, const char *target,
          const char *filesystemtype, unsigned long mountflags,
          const void *data))
{
    (void) data;

    log("mount(\"%s\", \"%s\", \"%s\", %lu, data)", source, target, filesystemtype, mountflags);
    return 0;
}

REPLACE(int, umount, (const char *target))
{
    log("umount(\"%s\")", target);
    return 0;
}
#endif

OVERRIDE(FILE *, fopen, (const char *pathname, const char *mode))
{
    char new_path[PATH_MAX];
    if (fixup_path(pathname, new_path) < 0)
        return NULL;

    return ORIGINAL(fopen)(new_path, mode);
}

OVERRIDE(pid_t, getpid, ())
{
    pid_t real_pid = ORIGINAL(getpid)();
    if (real_pid == starting_pid)
        return 1;
    else
        return real_pid;
}

REPLACE(int, reboot, (int cmd))
{
    log("reboot(0x%08x)", cmd);
    exit(0);
}

REPLACE(time_t, time, (time_t *tloc))
{
    // Hardcode all timestamps to be the same in the output for unit test purposes
    *tloc = 1764970081;
    return *tloc;
}

// syscall gives a deprecation warning on MacOS, so handle this is compat.c
#ifndef __APPLE__
REPLACE(long, syscall, (long number, ...))
{
    unsigned int magic1;
    unsigned int magic2;
    unsigned int cmd;
    const char *arg;

    va_list ap;
    va_start(ap, number);
    magic1 = va_arg(ap, int);
    magic2 = va_arg(ap, int);
    cmd = va_arg(ap, int);
    arg = va_arg(ap, const char *);
    va_end(ap);

#define LINUX_REBOOT_MAGIC1     0xfee1dead
#define LINUX_REBOOT_MAGIC2     672274793
#define LINUX_REBOOT_CMD_RESTART2       0xA1B2C3D4

    // Only expecting this one syscall, so hardcode a lot here.
    if (number == SYS_reboot && magic1 == LINUX_REBOOT_MAGIC1 && magic2 == LINUX_REBOOT_MAGIC2 && cmd == LINUX_REBOOT_CMD_RESTART2)
        log("reboot(0x%08x, \"%s\")", cmd, arg);
    else
        log("syscall(0x%08x, 0x%08x, 0x%08x, 0x%08x)", cmd, magic1, magic2, cmd);

    exit(0);
}
#endif

REPLACE(int, clock_settime, (clockid_t clk_id, const struct timespec *tp))
{
    (void) clk_id;
    (void) tp;

    log("clock_settime(CLOCK_REALTIME, tp)");
    return 0;
}

REPLACE(int, setuid, (uid_t uid))
{
    log("setuid(%d)", uid);
    return 0;
}

REPLACE(int, setgid, (gid_t gid))
{
    log("setgid(%d)", gid);
    return 0;
}

REPLACE(int, kill, (pid_t pid, int sig))
{
    log("kill(%d, %d)", pid, sig);
    return 0;
}

REPLACE(int, tcgetattr, (int fd, struct termios *termios_p))
{
    log("tcgetattr");

    memset(termios_p, 0, sizeof(struct termios));

    return 0;
}

REPLACE(int, tcsetattr, (int fd, int optional_actions, const struct termios *termios_p))
{
    int cs8 = termios_p->c_cflag & CS8;
    int parenb = termios_p->c_cflag & PARENB;
    int stopb = termios_p->c_cflag & CSTOPB;

    log("tcsetattr(%d, iflag=%x, oflag=%x, cflag=%s%s%s, lflag=%x",
        optional_actions,
        (unsigned int) termios_p->c_iflag,
        (unsigned int) termios_p->c_oflag,
        cs8 ? "8" : "*",
        parenb ? "*" : "N",
        stopb ? "2" : "1",
        (unsigned int) termios_p->c_lflag);

    return 0;
}

REPLACE(struct passwd *, getpwuid, (uid_t uid))
{
    static struct passwd pwd;
    static char pw_dir[32];

    memset(&pwd, 0, sizeof(pwd));
    pwd.pw_uid = uid;
    pwd.pw_dir = pw_dir;
    sprintf(pw_dir, "/home/user%d", (int) uid);

    // UID == 1 is special and always returns an error
    // so that the error handling code can be tested.
    return uid != 1 ? &pwd : NULL;
}

OVERRIDE(int, open, (const char *pathname, int flags, ...))
{
    int mode;

    va_list ap;
    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = va_arg(ap, int);
    else
        mode = 0;
    va_end(ap);

    if (strcmp(pathname, "/dev/kmsg") == 0) {
        // If /dev/kmsg exists, then force append for test purposes
        // Fake out read requests since those are always mocked.
        if (flags & O_WRONLY)
            flags |= O_APPEND;
        else
            return dup(STDERR_FILENO);
    } else if (strcmp(pathname, "/dev/pmsg0") == 0) {
        // Simulate pmsg0 by forcing it to be opened with the append flag
        if (flags & O_WRONLY)
            flags |= O_APPEND;
    }


    char new_path[PATH_MAX];
    if (fixup_path(pathname, new_path) < 0)
        return -1;

    return ORIGINAL(open)(new_path, flags, mode);
}

OVERRIDE(int, close, (int fd))
{
    // Ignore the default file handles when testing since we lose errors
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
        return 0;

    return ORIGINAL(close)(fd);
}

#ifdef __APPLE__
REPLACE(int, sethostname, (const char *name, int len))
#else
REPLACE(int, sethostname, (const char *name, size_t len))
#endif
{
    log("sethostname(\"%s\", %d)", name, (int) len);
    return 0;
}

REPLACE(pid_t, setsid, ())
{
    log("setsid()");
    return 1;
}

OVERRIDE(int, mkdir, (const char *path, mode_t mode))
{
    log("mkdir(\"%s\", %03o)", path, mode);

    char new_path[PATH_MAX];
    if (fixup_path(path, new_path) < 0)
        return -1;
    return ORIGINAL(mkdir)(new_path, mode);
}

OVERRIDE(unsigned int, sleep, (unsigned int seconds))
{
    if (seconds >= 2) {
        // This is from the emulated sigtimedwait
        return ORIGINAL(sleep)(seconds);
    } else {
        log("sleep(%u)", seconds);
        return 0;
    }
}

OVERRIDE(int, scandir, (const char *dirp, struct dirent ***namelist, int (*filter)(const struct dirent *), int (*compar)(const struct dirent **, const struct dirent **)))
{
    char new_path[PATH_MAX];
    if (fixup_path(dirp, new_path) < 0)
        return -1;

    return ORIGINAL(scandir)(new_path, namelist, filter, compar);
}

OVERRIDE(int, chdir, (const char * path))
{
    char new_path[PATH_MAX];
    if (fixup_path(path, new_path) < 0)
        return -1;

    return ORIGINAL(chdir)(new_path);
}

OVERRIDE(int, execvp, (const char *file, char *const argv[]))
{
    char new_path[PATH_MAX];
    if (fixup_path(file, new_path) < 0)
        return -1;

    return ORIGINAL(execvp)(new_path, argv);
}

OVERRIDE(int, dup2, (int oldfd, int newfd))
{
    if (REPLACEMENT(getpid)() == 1)
        return oldfd;

    return ORIGINAL(dup2)(oldfd, newfd);
}

OVERRIDE(int, symlink, (const char *target, const char *linkpath))
{
    log("symlink(\"%s\",\"%s\")", target, linkpath);

    char new_target[PATH_MAX];
    if (fixup_path(target, new_target) < 0)
        return -1;

    char new_linkpath[PATH_MAX];
    if (fixup_path(linkpath, new_linkpath) < 0)
        return -1;

    return ORIGINAL(symlink)(new_target, new_linkpath);
}

OVERRIDE(int, link, (const char *target, const char *linkpath))
{
    char new_target[PATH_MAX];
    if (fixup_path(target, new_target) < 0)
        return -1;

    char new_linkpath[PATH_MAX];
    if (fixup_path(linkpath, new_linkpath) < 0)
        return -1;

    return ORIGINAL(link)(new_target, new_linkpath);
}

#ifdef __APPLE__
#define OVERRIDE_STAT 1
#else
#if __GLIBC_PREREQ(2, 33)
#define OVERRIDE_STAT 1
#else
#define OVERRIDE_XSTAT 1
#endif
#endif

#ifdef OVERRIDE_STAT
OVERRIDE(int, stat, (const char *pathname, struct stat *st))
{
    memset(st, 0, sizeof(struct stat));
    if (strcmp(pathname, "/") == 0) {
        // erlinit stat's "/" to figure out the root device
        st->st_dev = 0xb301;
        st->st_mode = S_IFDIR;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0") == 0) {
        st->st_rdev = 0xb300;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0p1") == 0) {
        st->st_rdev = 0xb301;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0p2") == 0) {
        st->st_rdev = 0xb302;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0p3") == 0) {
        st->st_rdev = 0xb303;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0p4") == 0) {
        st->st_rdev = 0xb304;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/sda") == 0) {
        st->st_rdev = 0x800;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/sda1") == 0) {
        st->st_rdev = 0x801;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/sda2") == 0) {
        st->st_rdev = 0x802;
        st->st_mode = S_IFBLK;
        return 0;
    } else {
        char new_path[PATH_MAX];
        if (fixup_path(pathname, new_path) < 0)
            return -1;
        return ORIGINAL(stat)(new_path, st);
   }
}
OVERRIDE(int, lstat, (const char *pathname, struct stat *st))
{
    memset(st, 0, sizeof(struct stat));
    char new_path[PATH_MAX];
    if (fixup_path(pathname, new_path) < 0)
        return -1;
    return ORIGINAL(lstat)(new_path, st);
}
#else
OVERRIDE(int, __xstat, (int ver, const char *pathname, struct stat *st))
{
    memset(st, 0, sizeof(struct stat));
    if (strcmp(pathname, "/") == 0) {
        // erlinit stat's "/" to figure out the root device
        st->st_dev = 0xb301;
        st->st_mode = S_IFDIR;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0") == 0) {
        st->st_rdev = 0xb300;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0p1") == 0) {
        st->st_rdev = 0xb301;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0p2") == 0) {
        st->st_rdev = 0xb302;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0p3") == 0) {
        st->st_rdev = 0xb303;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/mmcblk0p4") == 0) {
        st->st_rdev = 0xb304;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/sda") == 0) {
        st->st_rdev = 0x800;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/sda1") == 0) {
        st->st_rdev = 0x801;
        st->st_mode = S_IFBLK;
        return 0;
    } else if (strcmp(pathname, "/dev/sda2") == 0) {
        st->st_rdev = 0x802;
        st->st_mode = S_IFBLK;
        return 0;
    } else {
        char new_path[PATH_MAX];
        if (fixup_path(pathname, new_path) < 0)
            return -1;
        return ORIGINAL(__xstat)(ver, new_path, st);
   }
}
OVERRIDE(int, __lxstat, (int ver, const char *pathname, struct stat *st))
{
    memset(st, 0, sizeof(struct stat));
    char new_path[PATH_MAX];
    if (fixup_path(pathname, new_path) < 0)
        return -1;
    return ORIGINAL(__lxstat)(ver, new_path, st);
}
#endif

REPLACE(int, ioctl, (int fd, unsigned long request, ...))
{
    (void) fd;
    const char *req;
    switch (request) {
    case SIOCGIFFLAGS:
        req = "SIOCGIFFLAGS";
        break;
    case SIOCSIFFLAGS:
        req = "SIOCSIFFLAGS";
        break;
    case SIOCSIFADDR:
        req = "SIOCSIFADDR";
        break;
    case SIOCSIFNETMASK:
        req = "SIOCSIFNETMASK";
        break;

#ifdef __APPLE__
// Fake out the SIOCGIFINDEX ioctl (see compat.h)
#define SIOCGIFINDEX SIOCGIFMTU
#define ifr_ifindex         ifr_ifru.ifru_mtu
#endif
    case SIOCGIFINDEX:
    {
        req = "SIOCGIFINDEX";
        va_list ap;
        va_start(ap, request);
        struct ifreq *ifr = va_arg(ap, struct ifreq *);

        ifr->ifr_ifindex = 1;

        va_end(ap);
        break;
    }
#ifdef __APPLE__
    case FIODTYPE:
        // Ignore FIODTYPE ioctls on OSX.
        return 0;
#endif

    case RNDADDENTROPY:
        req = "RNDADDENTROPY";
        break;

    default:
        log("unknown ioctl(0x%08lx)", request);
        req = "unknown";
        break;
    }
    log("ioctl(%s)", req);
    return 0;
}

#ifndef RLIMIT_NICE
// Check that these match compat.h
#define RLIMIT_NICE       100
#define RLIMIT_SIGPENDING 101
#define RLIMIT_RTPRIO     102
#define RLIMIT_LOCKS      103
#define RLIMIT_RTTIME     104
#define RLIMIT_MSGQUEUE   105
#endif

static const char *resource_to_string(int resource)
{
    switch (resource) {
    case RLIMIT_CORE: return "core";
    case RLIMIT_DATA: return "data";
    case RLIMIT_NICE: return "nice";
    case RLIMIT_FSIZE: return "fsize";
    case RLIMIT_SIGPENDING: return "sigpending";
    case RLIMIT_MEMLOCK: return "memlock";
    case RLIMIT_RSS: return "rss";
    case RLIMIT_NOFILE: return "nofile";
    case RLIMIT_MSGQUEUE: return "msgqueue";
    case RLIMIT_RTPRIO: return "rtprio";
    case RLIMIT_STACK: return "stack";
    case RLIMIT_CPU: return "cpu";
    case RLIMIT_NPROC: return "nproc";
    case RLIMIT_LOCKS: return "locks";
    case RLIMIT_RTTIME: return "rttime";
    default: return "invalid";
    }
}

#ifdef __APPLE__
REPLACE(int, setrlimit, (int resource, const struct rlimit *new_limit))
#else
REPLACE(int, setrlimit, (__rlimit_resource_t resource, const struct rlimit *new_limit))
#endif
{
    char cur[255] = {0};
    char max[255] = {0};
    if(new_limit->rlim_cur == RLIM_INFINITY)
        strcpy(cur, "unlimited");
    else
        snprintf(cur, 255, "%lu", (unsigned long) new_limit->rlim_cur);

    if(new_limit->rlim_max == RLIM_INFINITY)
        strcpy(max, "unlimited");
    else
        snprintf(max, 255, "%lu", (unsigned long) new_limit->rlim_max);

    log("setrlimit(%s, %s, %s)", resource_to_string(resource), cur, max);
    return 0;
}
