/* SPDX-License-Identifier: GPL-2.0 */

/* This file is part of Lustre, http://www.lustre.org/ */

#ifndef __LIBCFS_LINUX_INETDEVICE_H__
#define __LIBCFS_LINUX_INETDEVICE_H__

#include_next <linux/inetdevice.h>

/*
 * kernel 5.3: commit ef11db3310e272d3d8dbe8739e0770820dd20e52
 * kernel 4.18.0-193.el8:
 * added in_dev_for_each_ifa_rtnl and in_dev_for_each_ifa_rcu
 * and removed for_ifa and endfor_ifa.
 */
#ifndef HAVE_IN_DEV_FOR_EACH_IFA_RTNL
#define in_dev_for_each_ifa_rtnl(ifa, in_dev)			\
	for (ifa = (in_dev)->ifa_list; ifa; ifa = ifa->ifa_next)
#define in_dev_for_each_ifa_rcu(ifa, in_dev)			\
	for (ifa = (in_dev)->ifa_list; ifa; ifa = ifa->ifa_next)
#endif

#endif
