// SPDX-FileCopyrightText: 1992-2024 Free Software Foundation, Inc.
//
// SPDX-License-Identifier: LGPL-2.1-or-later
//

#ifndef __LINUX_IF_ADDR_H
#define __LINUX_IF_ADDR_H

#include <linux/netlink.h>

struct ifaddrmsg {
	uint8_t		ifa_family;
	uint8_t		ifa_prefixlen;	/* The prefix length		*/
	uint8_t		ifa_flags;	/* Flags			*/
	uint8_t		ifa_scope;	/* Address scope		*/
	uint32_t		ifa_index;	/* Link index			*/
};

/*
 * Important comment:
 * IFA_ADDRESS is prefix address, rather than local interface address.
 * It makes no difference for normally configured broadcast interfaces,
 * but for point-to-point IFA_ADDRESS is DESTINATION address,
 * local address is supplied in IFA_LOCAL attribute.
 *
 * IFA_FLAGS is a u32 attribute that extends the u8 field ifa_flags.
 * If present, the value from struct ifaddrmsg will be ignored.
 */
enum {
	IFA_UNSPEC,
	IFA_ADDRESS,
	IFA_LOCAL,
	IFA_LABEL,
	IFA_BROADCAST,
	IFA_ANYCAST,
	IFA_CACHEINFO,
	IFA_MULTICAST,
	IFA_FLAGS,
	__IFA_MAX,
};

#define IFA_MAX (__IFA_MAX - 1)

/* ifa_flags */
#define IFA_F_SECONDARY		0x01
#define IFA_F_TEMPORARY		IFA_F_SECONDARY

#define	IFA_F_NODAD		0x02
#define IFA_F_OPTIMISTIC	0x04
#define IFA_F_DADFAILED		0x08
#define	IFA_F_HOMEADDRESS	0x10
#define IFA_F_DEPRECATED	0x20
#define IFA_F_TENTATIVE		0x40
#define IFA_F_PERMANENT		0x80
#define IFA_F_MANAGETEMPADDR	0x100
#define IFA_F_NOPREFIXROUTE	0x200
#define IFA_F_MCAUTOJOIN	0x400
#define IFA_F_STABLE_PRIVACY	0x800

struct ifa_cacheinfo {
	uint32_t	ifa_prefered;
	uint32_t	ifa_valid;
	uint32_t	cstamp; /* created timestamp, hundredths of seconds */
	uint32_t	tstamp; /* updated timestamp, hundredths of seconds */
};

/* backwards compatibility for userspace */
#define IFA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ifaddrmsg))))
#define IFA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct ifaddrmsg))

#endif
