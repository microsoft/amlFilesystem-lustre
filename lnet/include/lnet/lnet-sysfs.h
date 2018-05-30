/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2017, Intel Corporation.
 *
 * Author:
 *   Sonia Sharma <sonia.sharma@intel.com>
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/include/lnet/lnet-sysfs.h
 *
 */

#ifndef __LNET_LNET_SYSFS_H__
#define __LNET_LNET_SYSFS_H__

#ifndef __KERNEL__
# error This include is only for kernel use.
#endif

#include <libcfs/libcfs.h>
#include <lnet/api.h>
#include <lnet/lib-types.h>
#include <uapi/linux/lnet/lnet-dlc.h>
#include <uapi/linux/lnet/lnet-types.h>


extern struct kobject *lnet_kobj;               /* Sysfs lnet kobject */

struct lnet_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			 const char *buf, size_t len);
};

#define LNET_ATTR(name, mode, show, store) \
static struct lnet_attr lnet_attr_##name = __ATTR(name, mode, show, store)

#define LNET_RO_ATTR(name) LNET_ATTR(name, 0444, name##_show, NULL)
#define LNET_RW_ATTR(name) LNET_ATTR(name, 0644, name##_show, name##_store)

ssize_t lnet_attr_show(struct kobject *kobj, struct attribute *attr,
		       char *buf);

ssize_t lnet_attr_store(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t len);

extern const struct sysfs_ops lnet_sysfs_ops;

/*
 * lnet_net_sysfs_setup
 *
 * Setup the sysfs hierarchy for lnet network stats.
 * Create kset for the net.
 *
 * net_id - net_type of the network being added
 * net - network added to the lnet
 */
int lnet_net_sysfs_setup(__u32 net_id, struct lnet_net *net);

/*
 * lnet_ni_sysfs_setup
 *
 * Create kobject for the NI under the kset of the network
 * that this NI is part of. Create the attribute files
 * for this NI as well.
 *
 * ni - NI for which stats are being created
 */
int lnet_ni_sysfs_setup(struct lnet_ni *ni, char *iface);

/*
 * lnet_net_sysfs_cleanup
 *
 * Cleanup the sysfs kset for this network.
 *
 * net - network being cleaned up from lnet
 */
void lnet_net_sysfs_cleanup(struct lnet_net *net);

/*
 * lnet_ni_sysfs_cleanup
 *
 * Cleanup the sysfs hierarchy for this NI stats.
 * Remove the kobject for the NI and the
 * attribute files for the NI.
 *
 * ni - NI for which stats are being cleaned up
 */
void lnet_ni_sysfs_cleanup(struct lnet_ni *ni);

/*
 * lnet_peer_sysfs_setup
 *
 * Setup the sysfs hierarchy for lnet peer stats.
 * Create kset for the prim_nid, kobject for the peer_ni
 * and the attribute files for the peer_ni.
 *
 * peers_kobj - Parent kobject(lnet_peer_kobj) for kset prim_nid
 * lpni - peer_ni for which stats are being created
 */
int lnet_peer_ni_sysfs_setup(struct lnet_peer_ni *lpni, struct lnet_peer *lp);

/*
 * lnet_peer_sysfs_cleanup
 *
 * Cleanup the sysfs hierarchy for lnet peer stats.
 * Remove the kobject for the peer_ni and the
 * attribute files for the peer_ni.
 *
 * spni - sysfs peer structure storing the peer_nid kobject
 */
void lnet_peer_ni_sysfs_cleanup(struct lnet_sysfs_peer *spni);

#endif
