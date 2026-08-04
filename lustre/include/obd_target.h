/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2014, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 */

#ifndef __OBD_TARGET_H
#define __OBD_TARGET_H
#include <lprocfs_status.h>
#include <linux/lnet/lnet-idl.h>
#include <obd.h>

/* server-side individual type definitions */

#define OBT_MAGIC       0xBDDECEAE

/* circular buffer depth; recovery_reconnect_top_n caps how many are reported */
#define OBT_RECONNECT_TOP_MAX	64

struct obt_reconnect_entry {
	struct lnet_nid	ore_nid;
	timeout_t	ore_delay;
};

/* hold common fields for "target" device */
struct obd_device_target {
	__u32			obt_magic;
	__u32			obt_instance;
	struct lu_target       *obt_lut;
	__u64			obt_mount_count;
	struct obd_job_stats	obt_jobstats;
	struct nm_config_file	*obt_nodemap_config_file;
	/* client reconnect-delay histogram for current recovery */
	struct obd_histogram	obt_reconnect_hist;
	/* recent reconnects (delays arrive ascending); newest at cursor-1 */
	struct obt_reconnect_entry obt_reconnect_top[OBT_RECONNECT_TOP_MAX];
	unsigned int		obt_reconnect_top_cursor;
	timeout_t		obt_nid_stats_idle_time;
};

#define OBJ_SUBDIR_COUNT 32 /* set to zero for no subdirs */

struct echo_obd {
	struct obd_device_target	eo_obt;
	struct obdo			eo_oa;
	spinlock_t			eo_lock;
	u64				eo_lastino;
	struct lustre_handle		eo_nl_lock;
	atomic_t			eo_prep;
};

struct ost_obd {
	struct ptlrpc_service	*ost_service;
	struct ptlrpc_service	*ost_create_service;
	struct ptlrpc_service	*ost_io_service;
	struct ptlrpc_service	*ost_seq_service;
	struct ptlrpc_service	*ost_out_service;
	struct mutex		 ost_health_mutex;
};

/* tgt_init() zeroes obt_magic when a mount fails while the exports it already
 * created are still being destroyed, so consumers reachable from export
 * teardown must use this instead of obd2obt() (see LU-7430).
 *
 * Only that deliberate zero is tolerated.  obd->u is a union, so on a device
 * that is not a target obt_magic aliases some other type's first field - a
 * pointer, for ost_obd and echo_client_obd - and a corrupted target reads as
 * garbage too.  Both are bugs worth reporting rather than skipping, so they
 * assert here.  Use this only where "no target" means there is nothing to do:
 * paths that answer a request need class_exp2tgt(), which keeps returning NULL
 * so they can fail the request instead of the node.  NULL is still returned
 * here if asserts are compiled out.
 */
static inline struct obd_device_target *obd2obt_or_null(struct obd_device *obd)
{
	struct obd_device_target *obt;

	BUILD_BUG_ON(sizeof(obd->u) < sizeof(*obt));

	if (!obd)
		return NULL;

	obt = (void *)&obd->u;
	if (obt->obt_magic == OBT_MAGIC)
		return obt;

	if (!obt->obt_magic)
		return NULL;

	LASSERTF(0, "%s: bad obt magic %08x, expected %08x or 0\n",
		 obd->obd_name, obt->obt_magic, OBT_MAGIC);

	return NULL;
}

static inline struct obd_device_target *obd2obt(struct obd_device *obd)
{
	struct obd_device_target *obt;

	BUILD_BUG_ON(sizeof(obd->u) < sizeof(*obt));

	if (!obd)
		return NULL;

	obt = (void *)&obd->u;
	LASSERTF(obt->obt_magic == OBT_MAGIC,
		 "%s: bad obt magic %08x, expected %08x\n",
		 obd->obd_name, obt->obt_magic, OBT_MAGIC);

	return obt;
}

#define NID_STATS_IDLE_DEFAULT 3600

/* returns 0 for a device that isn't yet a valid "target" - a device is
 * tagged OBD_DEVICE_TAG_TARGET (making it visible to
 * obd_device_for_each_target()) at class_register_device() (attach) time,
 * which runs before obd_obt_init() sets obt_magic during setup. obt_magic
 * gates whether obd->u may be interpreted as obd_device_target at all.
 */
static inline timeout_t obd_nid_stats_idle_time(struct obd_device *obd)
{
	struct obd_device_target *obt = (void *)&obd->u;

	if (obt->obt_magic != OBT_MAGIC)
		return 0;

	return obt->obt_nid_stats_idle_time;
}

static inline struct obd_device_target *obd_obt_init(struct obd_device *obd)
{
	struct obd_device_target *obt;

	obt = (void *)&obd->u;
	obt->obt_magic = OBT_MAGIC;
	obt->obt_instance = 0;
	spin_lock_init(&obt->obt_reconnect_hist.oh_lock);
	obt->obt_nid_stats_idle_time = NID_STATS_IDLE_DEFAULT;

	return obt;
}

static inline struct echo_obd *obd2echo(struct obd_device *obd)
{
	struct echo_obd *echo;

	BUILD_BUG_ON(sizeof(obd->u) < sizeof(*echo));

	if (!obd)
		return NULL;
	echo = (void *)&obd->u;

	return echo;
}

static inline struct ost_obd *obd2ost(struct obd_device *obd)
{
	struct ost_obd *ost;

	BUILD_BUG_ON(sizeof(obd->u) < sizeof(*ost));

	if (!obd)
		return NULL;
	ost = (void *)&obd->u;

	return ost;
}

#endif /* __OBD_TARGET_H */
