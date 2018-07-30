/*
 * LGPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * LGPL HEADER END
 *
 * Copyright (c) 2014, 2016, Intel Corporation.
 *
 * Author:
 *   Sonia Sharma <sonia.sharma@intel.com>
 */

#include "o2iblnd.h"

/*
 * LND peer stats attribute callback functions
 */
static struct kib_peer_ni *get_kib_peer(struct kobject *kobj)
{
	struct sysfs_lnd_peer *peer = container_of(kobj, struct sysfs_lnd_peer,
						   stats_kobj);
	return container_of(peer, struct kib_peer_ni, ibp_sysfs);
}

static ssize_t ibp_conns_show(struct kobject *kobj, struct attribute *attr,
			      char *buf) {
	struct kib_peer_ni *peer_ni = get_kib_peer(kobj);
	struct list_head  *ctmp;
	int count = 0;

	if (list_empty(&peer_ni->ibp_conns))
		return snprintf(buf, PAGE_SIZE, "%d\n", count);

	list_for_each(ctmp, &peer_ni->ibp_conns)
		count++;

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ibp_conns);

static ssize_t ibp_tx_queue_show(struct kobject *kobj, struct attribute *attr,
				 char *buf) {
	struct kib_peer_ni *peer_ni = get_kib_peer(kobj);
	struct list_head  *ctmp;
	int count = 0;

	if (list_empty(&peer_ni->ibp_tx_queue))
		return snprintf(buf, PAGE_SIZE, "%d\n", count);

	list_for_each(ctmp, &peer_ni->ibp_tx_queue)
		count++;

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ibp_tx_queue);

static ssize_t ibp_accepting_show(struct kobject *kobj, struct attribute *attr,
				  char *buf) {
	struct kib_peer_ni *peer_ni = get_kib_peer(kobj);
	unsigned short count = peer_ni->ibp_accepting;

	return snprintf(buf, PAGE_SIZE, "%hu\n", count);
}
LNET_RO_ATTR(ibp_accepting);

static ssize_t ibp_connecting_show(struct kobject *kobj, struct attribute *attr,
				   char *buf) {
	struct kib_peer_ni *peer_ni = get_kib_peer(kobj);
	unsigned short count = peer_ni->ibp_connecting;

	return snprintf(buf, PAGE_SIZE, "%hu\n", count);
}
LNET_RO_ATTR(ibp_connecting);

static ssize_t ibp_races_show(struct kobject *kobj, struct attribute *attr,
			      char *buf) {
	struct kib_peer_ni *peer_ni = get_kib_peer(kobj);
	unsigned char count = peer_ni->ibp_races;

	return snprintf(buf, PAGE_SIZE, "%u\n", count);
}
LNET_RO_ATTR(ibp_races);

static ssize_t ibp_last_alive_show(struct kobject *kobj, struct attribute *attr,
				   char *buf) {
	struct kib_peer_ni *peer_ni = get_kib_peer(kobj);
	time64_t count = peer_ni->ibp_last_alive;

	return snprintf(buf, PAGE_SIZE, "%lld\n", count ? count : -1);
}
LNET_RO_ATTR(ibp_last_alive);

static ssize_t ibp_refcount_show(struct kobject *kobj, struct attribute *attr,
				 char *buf) {
	struct kib_peer_ni *peer_ni = get_kib_peer(kobj);
	int count = atomic_read(&peer_ni->ibp_refcount);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ibp_refcount);

/*
 * LND connection stats attribute callback functions
 */
static struct kib_conn *get_kib_conn(struct kobject *kobj)
{
	struct sysfs_lnd_conn *conn = container_of(kobj, struct sysfs_lnd_conn,
						   stats_kobj);
	return container_of(conn, struct kib_conn, ibc_sysfs);
}

static ssize_t ibc_refcount_show(struct kobject *kobj, struct attribute *attr,
				 char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);
	int count = atomic_read(&conn->ibc_refcount);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ibc_refcount);

static ssize_t ibc_state_show(struct kobject *kobj, struct attribute *attr,
			      char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);

	return snprintf(buf, PAGE_SIZE, "%d\n", conn->ibc_state);
}
LNET_RO_ATTR(ibc_state);

static ssize_t ibc_nsends_posted_show(struct kobject *kobj,
				      struct attribute *attr, char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);

	return snprintf(buf, PAGE_SIZE, "%d\n", conn->ibc_nsends_posted);
}
LNET_RO_ATTR(ibc_nsends_posted);

static ssize_t ibc_noops_posted_show(struct kobject *kobj,
				     struct attribute *attr, char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);

	return snprintf(buf, PAGE_SIZE, "%d\n", conn->ibc_noops_posted);
}
LNET_RO_ATTR(ibc_noops_posted);

static ssize_t ibc_credits_show(struct kobject *kobj, struct attribute *attr,
				char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);

	return snprintf(buf, PAGE_SIZE, "%d\n", conn->ibc_credits);
}
LNET_RO_ATTR(ibc_credits);

static ssize_t ibc_outstanding_credits_show(struct kobject *kobj,
					    struct attribute *attr, char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);

	return snprintf(buf, PAGE_SIZE, "%d\n", conn->ibc_outstanding_credits);
}
LNET_RO_ATTR(ibc_outstanding_credits);

static ssize_t ibc_reserved_credits_show(struct kobject *kobj,
					 struct attribute *attr, char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);

	return snprintf(buf, PAGE_SIZE, "%d\n", conn->ibc_reserved_credits);
}
LNET_RO_ATTR(ibc_reserved_credits);

static ssize_t ibc_comms_error_show(struct kobject *kobj,
				    struct attribute *attr, char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);

	return snprintf(buf, PAGE_SIZE, "%d\n", conn->ibc_comms_error);
}
LNET_RO_ATTR(ibc_comms_error);

static ssize_t ibc_tx_queue_show(struct kobject *kobj, struct attribute *attr,
				 char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);
	struct list_head  *ctmp;
	int count = 0;

	if (list_empty(&conn->ibc_tx_queue))
		return snprintf(buf, PAGE_SIZE, "%d\n", count);

	list_for_each(ctmp, &conn->ibc_tx_queue)
		count++;

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ibc_tx_queue);

static ssize_t ibc_tx_queue_nocred_show(struct kobject *kobj,
					struct attribute *attr, char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);
	struct list_head  *ctmp;
	int count = 0;

	if (list_empty(&conn->ibc_tx_queue_nocred))
		return snprintf(buf, PAGE_SIZE, "%d\n", count);

	list_for_each(ctmp, &conn->ibc_tx_queue_nocred)
		count++;

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ibc_tx_queue_nocred);

static ssize_t ibc_tx_queue_rsrvd_show(struct kobject *kobj,
				       struct attribute *attr, char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);
	struct list_head  *ctmp;
	int count = 0;

	if (list_empty(&conn->ibc_tx_queue_rsrvd))
		return snprintf(buf, PAGE_SIZE, "%d\n", count);

	list_for_each(ctmp, &conn->ibc_tx_queue_rsrvd)
		count++;

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ibc_tx_queue_rsrvd);

static ssize_t ibc_active_txs_show(struct kobject *kobj, struct attribute *attr,
				   char *buf) {
	struct kib_conn *conn = get_kib_conn(kobj);
	struct list_head  *ctmp;
	int count = 0;

	if (list_empty(&conn->ibc_active_txs))
		return snprintf(buf, PAGE_SIZE, "%d\n", count);

	list_for_each(ctmp, &conn->ibc_active_txs)
		count++;

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ibc_active_txs);

/*
 * LND peer stats attributes
 */
static struct attribute *o2iblnd_peer_attrs[] = {
	&lnet_attr_ibp_conns.attr,
	&lnet_attr_ibp_tx_queue.attr,
	&lnet_attr_ibp_accepting.attr,
	&lnet_attr_ibp_connecting.attr,
	&lnet_attr_ibp_races.attr,
	&lnet_attr_ibp_last_alive.attr,
	&lnet_attr_ibp_refcount.attr,
	NULL,
};

static void lnd_peer_sysfs_release(struct kobject *kobj)
{
	struct sysfs_lnd_peer *peer_ni = container_of(kobj,
						      struct sysfs_lnd_peer,
						      stats_kobj);
	complete(&peer_ni->stats_kobj_unregister);
}

static struct kobj_type kib_peer_ni_ktype = {
	.sysfs_ops      = &lnet_sysfs_ops,
	.release        = lnd_peer_sysfs_release,
	.default_attrs  = o2iblnd_peer_attrs,
};

int set_sysfs_peer(struct lnet_ni *ni, lnet_nid_t nid,
		   struct sysfs_lnd_peer *kib_peer)
{
	kib_peer->stats_ktype = &kib_peer_ni_ktype;

	/* setup the sysfs kobjects for o2iblnd */
	return lnd_peer_sysfs_setup(nid, ni, kib_peer);
}

/*
 * LND conn stats attributes
 */
static struct attribute *o2iblnd_conn_attrs[] = {
	&lnet_attr_ibc_refcount.attr,
	&lnet_attr_ibc_state.attr,
	&lnet_attr_ibc_nsends_posted.attr,
	&lnet_attr_ibc_noops_posted.attr,
	&lnet_attr_ibc_credits.attr,
	&lnet_attr_ibc_outstanding_credits.attr,
	&lnet_attr_ibc_reserved_credits.attr,
	&lnet_attr_ibc_comms_error.attr,
	&lnet_attr_ibc_tx_queue.attr,
	&lnet_attr_ibc_tx_queue_nocred.attr,
	&lnet_attr_ibc_tx_queue_rsrvd.attr,
	&lnet_attr_ibc_active_txs.attr,
	NULL,
};

static void lnd_conn_sysfs_release(struct kobject *kobj)
{
	struct sysfs_lnd_conn *conn = container_of(kobj, struct sysfs_lnd_conn,
						   stats_kobj);
	complete(&conn->stats_kobj_unregister);
}

static struct kobj_type kib_conn_ktype = {
	.sysfs_ops      = &lnet_sysfs_ops,
	.release        = lnd_conn_sysfs_release,
	.default_attrs  = o2iblnd_conn_attrs,
};

int set_sysfs_conn(struct sysfs_lnd_peer *kib_peer,
		   struct sysfs_lnd_conn *kib_conn)
{
	kib_conn->stats_ktype = &kib_conn_ktype;

	/* setup the lnd conn sysfs kobjects */
	return lnd_conn_sysfs_setup(kib_peer, kib_conn);
}
