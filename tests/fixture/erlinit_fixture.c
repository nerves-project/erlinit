#define _GNU_SOURCE // for RTLD_NEXT
#include <stdio.h>
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
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
#include <glob.h>
#include <termios.h>

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

static void unfixup_path(char *path)
{
    size_t work_len = strlen(work);
    size_t path_len = strlen(path);

    if (path_len >= work_len && memcmp(path, work, work_len) == 0)
        memmove(path, path + work_len, path_len - work_len + 1);
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
    log("tcsetattr(%d, iflag=%x, oflag=%x, cflag=%x, lflag=%x",
        optional_actions,
        (unsigned int) termios_p->c_iflag,
        (unsigned int) termios_p->c_oflag,
        (unsigned int) termios_p->c_cflag,
        (unsigned int) termios_p->c_lflag);

    return 0;
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

    // Log to stderr
    if (strcmp(pathname, "/dev/kmsg") == 0)
        return dup(STDERR_FILENO);

    char new_path[PATH_MAX];
    if (fixup_path(pathname, new_path) < 0)
        return -1;

    return ORIGINAL(open)(new_path, flags, mode);
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

    default:
        log("unknown ioctl(0x%08lx)", request);
        req = "unknown";
        break;
    }
    log("ioctl(%s)", req);
    return 0;
}

OVERRIDE(int, glob, (const char *pattern, int flags, int (*errfunc)(const char *epath, int errno), glob_t *pglob))
{
    if (pattern[0] == '/') {
        char new_pattern[PATH_MAX];
        if (fixup_path(pattern, new_pattern) < 0)
            return -1;
        int rc = ORIGINAL(glob)(new_pattern, flags, errfunc, pglob);
        if (rc == 0) {
            size_t i;
            for (i = 0; i < pglob->gl_pathc; i++)
                unfixup_path(pglob->gl_pathv[i]);
        }
        return rc;
    } else {
        return ORIGINAL(glob)(pattern, flags, errfunc, pglob);
    }
}
