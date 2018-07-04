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

#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <net/if.h>
#include <netinet/in.h>

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

void setup_networking()
{
    debug("setup_networking");

    // Bring up the loopback interface (needed if the erlang distribute protocol code gets run)
    enable_loopback();
    configure_hostname();
}
