// SPDX-FileCopyrightText: 2015 Frank Hunleth
//
// SPDX-License-Identifier: MIT
//

#include "erlinit.h"

#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int enable_loopback()
{
    int ifindex = -1;

    // Set the loopback interface to up
    int fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        warn("socket(PF_INET) failed");
        return -1;
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

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        warn("SIOCGIFINDEX failed on lo");
        goto cleanup;
    }

    ifindex = ifr.ifr_ifindex;

cleanup:
    close(fd);
    return ifindex;
}

struct netlink_ifa_message {
    struct nlmsghdr  nh;
    struct ifaddrmsg ifa;
    char attrbuf[512];
};

static void add_rta_attr(struct netlink_ifa_message *msg, unsigned short rta_type, char *value,
                         unsigned short value_len)
{
    struct rtattr *rta = (struct rtattr *)(((char *) msg) + NLMSG_ALIGN(msg->nh.nlmsg_len));
    rta->rta_type = rta_type;
    rta->rta_len = RTA_LENGTH(value_len);
    memcpy(RTA_DATA(rta), value, value_len);
    msg->nh.nlmsg_len = NLMSG_ALIGN(msg->nh.nlmsg_len) + RTA_LENGTH(value_len);
}

static void configure_loopback(int ifindex)
{
    // Make two RT Netlink requests to set the IPv4 and IPv6 addresses
    // for the localhost interface. This is best effort and we don't
    // verify that they were set successfully.
    int s = socket(PF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (s < 0)
        return;

    struct netlink_ifa_message req[2];
    memset(&req[0], 0, sizeof(struct netlink_ifa_message));
    req[0].nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req[0].nh.nlmsg_flags = NLM_F_EXCL | NLM_F_CREATE | NLM_F_REQUEST; // NLM_F_ACK
    req[0].nh.nlmsg_type = RTM_NEWADDR;

    req[0].ifa.ifa_family = AF_INET;
    req[0].ifa.ifa_prefixlen = 8;
    req[0].ifa.ifa_flags = 0;
    req[0].ifa.ifa_scope = RT_SCOPE_HOST;
    req[0].ifa.ifa_index = ifindex;

    char ipv4_localhost_addr[4];
    inet_pton(AF_INET, "127.0.0.1", ipv4_localhost_addr);

    add_rta_attr(&req[0], IFA_LOCAL, ipv4_localhost_addr, sizeof(ipv4_localhost_addr));
    add_rta_attr(&req[0], IFA_ADDRESS, ipv4_localhost_addr, sizeof(ipv4_localhost_addr));

    memset(&req[1], 0, sizeof(struct netlink_ifa_message));
    req[1].nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req[1].nh.nlmsg_flags = NLM_F_EXCL | NLM_F_CREATE | NLM_F_REQUEST; // NLM_F_ACK
    req[1].nh.nlmsg_type = RTM_NEWADDR;
    req[1].ifa.ifa_family = AF_INET6;
    req[1].ifa.ifa_prefixlen = 128;
    req[1].ifa.ifa_flags = 0;
    req[1].ifa.ifa_scope = RT_SCOPE_HOST;
    req[1].ifa.ifa_index = ifindex;

    char ipv6_localhost_addr[16];
    inet_pton(AF_INET6, "::1", ipv6_localhost_addr);

    add_rta_attr(&req[1], IFA_LOCAL, ipv6_localhost_addr, sizeof(ipv6_localhost_addr));
    add_rta_attr(&req[1], IFA_ADDRESS, ipv6_localhost_addr, sizeof(ipv6_localhost_addr));

    struct sockaddr_nl dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;
    dest_addr.nl_groups = 0;

    struct iovec iov[2];
    struct msghdr msg;
    memset(&msg, 0, sizeof(struct msghdr));
    iov[0].iov_base = (void *)&req[0];
    iov[0].iov_len = req[0].nh.nlmsg_len;
    iov[1].iov_base = (void *)&req[1];
    iov[1].iov_len = req[1].nh.nlmsg_len;

    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    if (sendmsg(s, &msg, 0) < 0)
        warn("Netlink sendmsg failed to send localhost IP address request");

    close(s);
}

void setup_networking()
{
    debug("setup_networking");

    // Bring up the loopback interface (needed if the erlang distribute protocol code gets run)
    int ifindex = enable_loopback();
    if (ifindex >= 0)
        configure_loopback(ifindex);

    configure_hostname();
}
