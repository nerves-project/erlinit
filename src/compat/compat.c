#include "compat.h"
#include <stdio.h>
#include <errno.h>

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

