#include "compat.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include "sys/syscall.h"
#include "linux/reboot.h"

static int sigtimedwait_signal = 0;
static int sigtimedwait_counter = 0;
static int sigtimedwait_last_reported = 0;

static void sigtimedwait_handler(int sig)
{
    sigtimedwait_signal = sig;
    sigtimedwait_counter++;
}

__attribute__((constructor)) void compat_init()
{
    struct sigaction sa;
    sa.sa_handler = sigtimedwait_handler;
    sa.sa_flags = SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGPWR, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int sigtimedwait(const sigset_t *set, siginfo_t *info,
                 const struct timespec *timeout)
{
    (void) set;
    (void) info;

    if (sigtimedwait_last_reported == sigtimedwait_counter) {
        if (sleep(timeout->tv_sec) == 0) {
            errno = EAGAIN;
            return -1;
        }
    }

    sigtimedwait_last_reported = sigtimedwait_counter;
    return sigtimedwait_signal;
}

int sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
    struct timespec timeout;
    timeout.tv_sec = 86400;
    timeout.tv_nsec = 0;
    return sigtimedwait(set, info, &timeout);
}

int sigprocmask_noop(int how, const sigset_t *restrict set, sigset_t *restrict oset)
{
    (void) how;
    (void) set;
    (void) oset;
    return 0;
}

int pivot_root(const char *new_root, const char *put_old)
{
    (void) new_root;
    (void) put_old;
    return 0;
}

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags)
{
    (void) flags;
    memset(buf, 0xaa, buflen);
    return buflen;
}

// This is only needed for reboot, so hardcode the arguments.
long fake_syscall(long number, unsigned int magic, unsigned int magic2, unsigned int cmd, const void *arg)
{
    if (number != SYS_reboot || magic != LINUX_REBOOT_MAGIC1 || magic2 != LINUX_REBOOT_MAGIC1)
        errx(1, "Unexpected arguments passed to syscall");

    if (cmd != LINUX_REBOOT_CMD_RESTART2)
        errx(1, "Expected LINUX_REBOOT_CMD_RESTART2 to be passed to reboot with arguments");

    fprintf(stderr, "Reboot arguments: %s", (const char *) arg);

    return 0;
}