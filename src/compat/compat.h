#ifndef COMPAT_H
#define COMPAT_H

#include <signal.h>
#include <time.h>
#include <sys/mount.h> // must be included before 5-4 arg macro below
#include <unistd.h>

// Patch around differences in signals
#define SIGPWR          30

int sigtimedwait(const sigset_t *set, siginfo_t *info,
                 const struct timespec *timeout);
int sigwaitinfo(const sigset_t *set, siginfo_t *info);

// Make sigprocmask a noop so that our faked sigtimedwait implementation
// works.
#define sigprocmask sigprocmask_noop
int sigprocmask_noop(int how, const sigset_t *restrict set, sigset_t *restrict oset);

/*
 * These are the fs-independent mount-flags: up to 32 flags are supported
 */
#define MS_RDONLY        1      /* Mount read-only */
#define MS_NOSUID        2      /* Ignore suid and sgid bits */
#define MS_NODEV         4      /* Disallow access to device special files */
#define MS_NOEXEC        8      /* Disallow program execution */
#define MS_SYNCHRONOUS  16      /* Writes are synced at once */
#define MS_REMOUNT      32      /* Alter flags of a mounted FS */
#define MS_MANDLOCK     64      /* Allow mandatory locks on an FS */
#define MS_DIRSYNC      128     /* Directory modifications are synchronous */
#define MS_NOATIME      1024    /* Do not update access times. */
#define MS_NODIRATIME   2048    /* Do not update directory access times */
#define MS_BIND         4096
#define MS_MOVE         8192
#define MS_REC          16384
#define MS_VERBOSE      32768   /* War is peace. Verbosity is silence.
                                   MS_VERBOSE is deprecated. */
#define MS_SILENT       32768
#define MS_POSIXACL     (1<<16) /* VFS does not apply the umask */
#define MS_UNBINDABLE   (1<<17) /* change to unbindable */
#define MS_PRIVATE      (1<<18) /* change to private */
#define MS_SLAVE        (1<<19) /* change to slave */
#define MS_SHARED       (1<<20) /* change to shared */
#define MS_RELATIME     (1<<21) /* Update atime relative to mtime/ctime. */
#define MS_KERNMOUNT    (1<<22) /* this is a kern_mount call */
#define MS_I_VERSION    (1<<23) /* Update inode I_version field */
#define MS_STRICTATIME  (1<<24) /* Always perform atime updates */
#define MS_LAZYTIME     (1<<25) /* Update the on-disk [acm]times lazily */

#define mount(a,b,c,d,e) mount(a,b,d, (void*) c)
#define umount(a) unmount(a, 0)

// Missing SOCK_CLOEXEC
#define SOCK_CLOEXEC  02000000

// Netlink
#define PF_NETLINK     16
#define AF_NETLINK     PF_NETLINK

// Fake out the SIOCGIFINDEX ioctl
#define SIOCGIFINDEX SIOCGIFMTU
#define ifr_ifindex         ifr_ifru.ifru_mtu

#endif
