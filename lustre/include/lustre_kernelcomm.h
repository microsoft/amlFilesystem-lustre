/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2013, 2017, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Author: Nathan Rutman <nathan.rutman@sun.com>
 *
 * Kernel <-> userspace communication routines.
 * The definitions below are used in the kernel and userspace.
 */

#ifndef __LUSTRE_KERNELCOMM_H__
#define __LUSTRE_KERNELCOMM_H__

#include <lustre_compat/linux/xarray.h>
#include <lustre_compat/net/genetlink.h>
#include <net/sock.h>
/* For declarations shared with userspace */
#include <uapi/linux/lustre/lustre_kernelcomm.h>

/**
 * enum lustre_device_attrs	      - Lustre general top-level netlink
 *					attributes that describe lustre
 *					'devices'. These values are used
 *					to piece together messages for
 *					sending and receiving.
 *
 * @LUSTRE_DEVICE_ATTR_UNSPEC:		unspecified attribute to catch errors
 *
 * @LUSTRE_DEVICE_ATTR_HDR:		Netlink group this data is for
 *					(NLA_NUL_STRING)
 * @LUSTRE_DEVICE_ATTR_INDEX:		device number used as an index (NLA_U16)
 * @LUSTRE_DEVICE_ATTR_STATUS:		status of the device (NLA_STRING)
 * @LUSTRE_DEVICE_ATTR_CLASS:		class the device belongs to (NLA_STRING)
 * @LUSTRE_DEVICE_ATTR_NAME:		name of the device (NLA_STRING)
 * @LUSTRE_DEVICE_ATTR_UUID:		UUID of the device (NLA_STRING)
 * @LUSTRE_DEVICE_ATTR_REFCOUNT:	refcount of the device (NLA_U32)
 */
enum lustre_device_attrs {
	LUSTRE_DEVICE_ATTR_UNSPEC = 0,

	LUSTRE_DEVICE_ATTR_HDR,
	LUSTRE_DEVICE_ATTR_INDEX,
	LUSTRE_DEVICE_ATTR_STATUS,
	LUSTRE_DEVICE_ATTR_CLASS,
	LUSTRE_DEVICE_ATTR_NAME,
	LUSTRE_DEVICE_ATTR_UUID,
	LUSTRE_DEVICE_ATTR_REFCOUNT,

	__LUSTRE_DEVICE_ATTR_MAX_PLUS_ONE
};

#define LUSTRE_DEVICE_ATTR_MAX (__LUSTRE_DEVICE_ATTR_MAX_PLUS_ONE - 1)

/**
 * enum lustre_param_list_attrs	      - General header to list all sources
 *					supporting an specific query.
 *
 * @LUSTRE_PARAM_ATTR_UNSPEC:		unspecified attribute to catch errors
 *
 * @LUSTRE_PARAM_ATTR_HDR:		groups params belong to (NLA_NUL_STRING)
 * @LUSTRE_PARAM_ATTR_SOURCE:		source of the params (NLA_STRING)
 * @LUSTRE_PARAM_ATTR_LIST:		This handles the special case of 1 obd
 *					to N items that define the source.
 *					Example is pools.
 */
enum lustre_param_list_attrs {
	LUSTRE_PARAM_ATTR_UNSPEC = 0,

	LUSTRE_PARAM_ATTR_HDR,
	LUSTRE_PARAM_ATTR_SOURCE,
	LUSTRE_PARAM_ATTR_LIST,

	__LUSTRE_PARAM_ATTR_MAX_PLUS_ONE
};

#define LUSTRE_PARAM_ATTR_MAX (__LUSTRE_PARAM_ATTR_MAX_PLUS_ONE - 1)

/**
 * enum lustre_target_attrs	      - Lustre general top-level netlink
 *					attributes that describe lustre
 *					'target_obd'. These values are used
 *					to piece together messages for
 *					sending and receiving.
 *
 * @LUSTRE_TARGET_ATTR_UNSPEC:		unspecified attribute to catch errors
 *
 * @LUSTRE_TARGET_ATTR_HDR:		Netlink group this data is for
 *					(NLA_NUL_STRING)
 * @LUSTRE_TARGET_ATTR_SOURCE:		obd device targets belong too
 *					(NLA_STRING)
 * @LUSTRE_TARGET_ATTR_PROP_LIST:	list of target properties (NLA_NESTED)
 */
enum lustre_target_attrs {
	LUSTRE_TARGET_ATTR_UNSPEC = 0,

	LUSTRE_TARGET_ATTR_HDR,
	LUSTRE_TARGET_ATTR_SOURCE,
	LUSTRE_TARGET_ATTR_PROP_LIST,

	__LUSTRE_TARGET_ATTR_MAX_PLUS_ONE,
};

#define LUSTRE_TARGET_ATTR_MAX	(__LUSTRE_TARGET_ATTR_MAX_PLUS_ONE - 1)

/**
 * enum lustre_target_props_attrs     - Lustre target netlink attributes used
 *					to compose message for sending or
 *					receiving.
 *
 * @LUSTRE_TARGET_PROP_ATTR_UNSPEC:	unspecified attribute to catch errors
 * @LUSTRE_TARGET_PROP_ATTR_INDEX:	target number used as an index (NLA_U16)
 * @LUSTRE_TARGET_PROP_ATTR_UUID:	UUID of the target (NLA_STRING)
 * @LUSTRE_TARGET_PROP_ATTR_STATUS:	status of the target (NLA_STRING)
 */
enum lustre_target_prop_attrs {
	LUSTRE_TARGET_PROP_ATTR_UNSPEC = 0,

	LUSTRE_TARGET_PROP_ATTR_INDEX,
	LUSTRE_TARGET_PROP_ATTR_UUID,
	LUSTRE_TARGET_PROP_ATTR_STATUS,

	__LUSTRE_TARGET_PROP_ATTR_MAX_PLUS_ONE,
};

#define LUSTRE_TARGET_PROP_ATTR_MAX	(__LUSTRE_TARGET_PROP_ATTR_MAX_PLUS_ONE - 1)

/**
 * enum lustre_stats_attrs	     - Lustre stats netlink attributes used
 *				       to compose messages for sending or
 *				       receiving.
 *
 * @LUSTRE_STATS_ATTR_UNSPEC:	       unspecified attribute to catch errors
 * @LUSTRE_STATS_ATTR_PAD:	       padding for 64-bit attributes, ignore
 *
 * @LUSTRE_STATS_ATTR_HDR:	       groups stats belong to (NLA_NUL_STRING)
 * @LUSTRE_STATS_ATTR_SOURCE:	       source of the stats (NLA_STRING)
 * @LUSTRE_STATS_ATTR_TIMESTAMP:       time of collection in nanoseconds
 *				       (NLA_S64)
 * @LUSTRE_STATS_ATTR_START_TIME:      start time of collection (NLA_S64)
 * @LUSTRE_STATS_ATTR_ELAPSE_TIME:     elapsed time of collection (NLA_S64)
 * @LUSTRE_STATS_ATTR_DATASET:	       bookmarks for that stats data
 *				       (NLA_NESTED)
 */
enum lustre_stats_attrs {
	LUSTRE_STATS_ATTR_UNSPEC = 0,
	LUSTRE_STATS_ATTR_PAD = LUSTRE_STATS_ATTR_UNSPEC,

	LUSTRE_STATS_ATTR_HDR,
	LUSTRE_STATS_ATTR_SOURCE,
	LUSTRE_STATS_ATTR_TIMESTAMP,
	LUSTRE_STATS_ATTR_START_TIME,
	LUSTRE_STATS_ATTR_ELAPSE_TIME,
	LUSTRE_STATS_ATTR_DATASET,

	__LUSTRE_STATS_ATTR_MAX_PLUS_ONE,
};

#define LUSTRE_STATS_ATTR_MAX	(__LUSTRE_STATS_ATTR_MAX_PLUS_ONE - 1)

/**
 * enum lustre_stats_dataset_attrs    - Lustre stats counter's netlink
 *					attributes used to compose messages
 *					for sending or receiving.
 *
 * @LUSTRE_STATS_ATTR_DATASET_UNSPEC:	unspecified attribute to catch errors
 * @LUSTRE_STATS_ATTR_DATASET_PAD:	padding for 64-bit attributes, ignore
 *
 * @LUSTRE_STATS_ATTR_DATASET_NAME:	name of counter (NLA_NUL_STRING)
 * @LUSTRE_STATS_ATTR_DATASET_COUNT:	counter interation (NLA_U64)
 * @LUSTRE_STATS_ATTR_DATASET_UNITS:	units of counter values (NLA_STRING)
 * @LUSTRE_STATS_ATTR_DATASET_MINIMUM:	smallest counter value collected
 *					(NLA_U64)
 * @LUSTRE_STATS_ATTR_DATASET_MAXIMUM:	largest count value collected (NLA_U64)
 * @LUSTRE_STATS_ATTR_DATASET_SUM:	total of all values of the counter
 *					(NLA_U64)
 * @LUSTRE_STATS_ATTR_DATASET_SUMSQUARE: Sum of the square of all values.
 *					 Allows user land apps to calculate
 *					 standard deviation. (NLA_U64)
 */
enum lustre_stats_dataset_attrs {
	LUSTRE_STATS_ATTR_DATASET_UNSPEC = 0,
	LUSTRE_STATS_ATTR_DATASET_PAD = LUSTRE_STATS_ATTR_DATASET_UNSPEC,

	LUSTRE_STATS_ATTR_DATASET_NAME,
	LUSTRE_STATS_ATTR_DATASET_COUNT,
	LUSTRE_STATS_ATTR_DATASET_UNITS,
	LUSTRE_STATS_ATTR_DATASET_MINIMUM,
	LUSTRE_STATS_ATTR_DATASET_MAXIMUM,
	LUSTRE_STATS_ATTR_DATASET_SUM,
	LUSTRE_STATS_ATTR_DATASET_SUMSQUARE,

	__LUSTRE_STATS_ATTR_DATASET_MAX_PLUS_ONE,
};

#define LUSTRE_STATS_ATTR_DATASET_MAX	(__LUSTRE_STATS_ATTR_DATASET_MAX_PLUS_ONE - 1)

/*
 * Table-driven netlink handler framework.
 *
 * Commands that iterate OBD devices share a common start/dump/done
 * pattern.  Each command provides callbacks for the parts that differ.
 *
 * @lnc_collect: append entries for @ctx context. The number of entries
 *		 added to the XAS is dependent on the implementation.
 *		 The passed in nlattr and rem are used to allow far
 *		 more complex filtering beyond just the 'source. This
 *		 ensure we are in the proper place when returning from
 *		 this function.
 * @lnc_release: release what @lnc_collect stored in an entry
 * @lnc_setup_keys: setup the key table for the first message sent.
 *		    Additional calculate the largest single message
 *		    size to ensure every message sent fits the sk_buff
 * @lnc_free_keys: optional depending on if keytables was allocated.
 *		   Free the key table if allocated.
 * @lnc_get_source: Required when LS_NL_SCAN is not set. Return the
 *		    'source' that the data entry belongs to. This is
 *		    used to check if the source is still available
 *		    since time has passed between Netlink start and dump.
 * @lnc_pack_msg: put the attributes into @msg;  Return -EMSGSIZE when
 *		  @msg is full and the entry is retried in the next skb.
 */
struct lustre_nl_ctx {
	/* For obd devices all but free_keys is mandatory. */
	int			(*lnc_collect)(struct lustre_nl_ctx *ctx,
					      void *source,
					      struct nlattr **next, int *rem);
	void			(*lnc_release)(struct lustre_nl_ctx *ctx,
					       void *data);
	ssize_t			(*lnc_setup_keys)(struct lustre_nl_ctx *ctx);
	void			(*lnc_free_keys)(struct lustre_nl_ctx *ctx);
	void*			(*lnc_get_source)(void *data);
	int			(*lnc_pack_msg)(struct lustre_nl_ctx *ctx,
						struct sk_buff *msg,
						void *data);
	struct xa_state			lnc_xas;
	struct xarray			lnc_array;
	const struct ln_key_list	**lnc_keys;
	void				*lnc_prev;
#ifndef HAVE_GENL_INFO_FAMILY
	struct compat_genl_info		lnc_info;
#endif
	unsigned long			lnc_state;
};

static inline struct lustre_nl_ctx *
lustre_nl_dump_ctx(struct netlink_callback *cb)
{
	return (struct lustre_nl_ctx *)cb->args[0];
}

/* Special obd device handling */
int lustre_obd_nl_start(struct netlink_callback *cb,
			  struct lustre_nl_ctx *settings);
int lustre_obd_nl_simple_dump(struct sk_buff *msg,
				struct netlink_callback *cb);
int lustre_obd_nl_dump(struct sk_buff *msg,
			 struct netlink_callback *cb);
int lustre_obd_nl_done(struct netlink_callback *cb);

/* Special stats handling */
int lustre_stats_dump(struct sk_buff *msg, struct netlink_callback *cb);
int lustre_stats_put_dataset(struct lustre_nl_ctx *ctx, struct sk_buff *msg,
			     void *data);

/* prototype for callback function on kuc groups */
typedef int (*libcfs_kkuc_cb_t)(void *data, void *cb_arg);

/* Kernel methods */
int __init libcfs_kkuc_init(void);
void libcfs_kkuc_fini(void);
int libcfs_kkuc_msg_put(struct file *fp, void *payload);
int libcfs_kkuc_group_put(const struct obd_uuid *uuid, int group, void *data);
int libcfs_kkuc_group_add(struct file *fp, const struct obd_uuid *uuid, int uid,
			  int group, void *data, size_t data_len);
int libcfs_kkuc_group_rem(const struct obd_uuid *uuid, int uid, int group);
int libcfs_kkuc_group_foreach(const struct obd_uuid *uuid, int group,
			      libcfs_kkuc_cb_t cb_func, void *cb_arg);

#endif /* __LUSTRE_KERNELCOMM_H__ */
