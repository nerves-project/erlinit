#ifndef LINUX_RANDOM_H
#define LINUX_RANDOM_H

#include <sys/types.h>

/* Flags for use with getrandom.  */
#define GRND_NONBLOCK 0x01
#define GRND_RANDOM 0x02
#define GRND_INSECURE 0x04

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

// Fake out the RNDADDENTROPY ioctl
#define RNDADDENTROPY _IOW( 'R', 0x03, int [2] )

// Hack for seedrng.c
#define CLOCK_BOOTTIME CLOCK_MONOTONIC

#endif
