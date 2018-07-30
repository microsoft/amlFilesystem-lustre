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

#include <lnet/lib-lnet.h>
#include <lnet/lnet-sysfs.h>

struct kobject *lnet_kobj;
EXPORT_SYMBOL_GPL(lnet_kobj);

/*
 * Helper functions for sysfs callbacks implementation
 */
ssize_t lnet_attr_show(struct kobject *kobj,
		       struct attribute *attr, char *buf)
{
	struct lnet_attr *a = container_of(attr, struct lnet_attr, attr);

	return a->show ? a->show(kobj, attr, buf) : 0;
}
EXPORT_SYMBOL_GPL(lnet_attr_show);

ssize_t lnet_attr_store(struct kobject *kobj, struct attribute *attr,
			  const char *buf, size_t len)
{
	struct lnet_attr *a = container_of(attr, struct lnet_attr, attr);

	return a->store ? a->store(kobj, attr, buf, len) : len;
}
EXPORT_SYMBOL_GPL(lnet_attr_store);

const struct sysfs_ops lnet_sysfs_ops = {
	.show  = lnet_attr_show,
	.store = lnet_attr_store,
};
EXPORT_SYMBOL_GPL(lnet_sysfs_ops);

/*
 * LNet NI stats attribute callback functions
 */
static ssize_t total_send_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = lnet_sum_stats(&ni->ni_stats, LNET_STATS_TYPE_SEND);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(total_send_count);

static ssize_t total_recv_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = lnet_sum_stats(&ni->ni_stats, LNET_STATS_TYPE_RECV);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(total_recv_count);

static ssize_t total_drop_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = lnet_sum_stats(&ni->ni_stats, LNET_STATS_TYPE_DROP);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(total_drop_count);

static ssize_t get_send_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_send_stats.co_get_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(get_send_count);

static ssize_t put_send_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_send_stats.co_put_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(put_send_count);

static ssize_t reply_send_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_send_stats.co_reply_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(reply_send_count);

static ssize_t ack_send_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_send_stats.co_ack_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ack_send_count);

static ssize_t hello_send_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_send_stats.co_hello_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(hello_send_count);

static ssize_t get_recv_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_recv_stats.co_get_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(get_recv_count);

static ssize_t put_recv_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_recv_stats.co_put_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(put_recv_count);

static ssize_t reply_recv_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_recv_stats.co_reply_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(reply_recv_count);

static ssize_t ack_recv_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_recv_stats.co_ack_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ack_recv_count);

static ssize_t hello_recv_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_recv_stats.co_hello_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(hello_recv_count);

static ssize_t get_drop_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_drop_stats.co_get_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(get_drop_count);

static ssize_t put_drop_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_drop_stats.co_put_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(put_drop_count);

static ssize_t reply_drop_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_drop_stats.co_reply_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(reply_drop_count);

static ssize_t ack_drop_count_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_drop_stats.co_ack_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(ack_drop_count);

static ssize_t hello_drop_count_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_stats.el_drop_stats.co_hello_count);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(hello_drop_count);

static ssize_t local_interrupt_show(struct kobject *kobj,
				    struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_hstats.hlt_local_interrupt);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(local_interrupt);

static ssize_t local_dropped_show(struct kobject *kobj,
				  struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_hstats.hlt_local_dropped);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(local_dropped);

static ssize_t local_aborted_show(struct kobject *kobj,
				  struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_hstats.hlt_local_aborted);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(local_aborted);

static ssize_t local_no_route_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_hstats.hlt_local_no_route);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(local_no_route);

static ssize_t local_timeout_show(struct kobject *kobj,
				  struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_hstats.hlt_local_timeout);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(local_timeout);

static ssize_t local_error_show(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);
	int count = atomic_read(&ni->ni_hstats.hlt_local_error);

	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(local_error);

static ssize_t ni_reset_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);

	if (!lnet_sum_stats(&ni->ni_stats, LNET_STATS_TYPE_SEND) &&
	    !lnet_sum_stats(&ni->ni_stats, LNET_STATS_TYPE_RECV) &&
	    !lnet_sum_stats(&ni->ni_stats, LNET_STATS_TYPE_DROP))
		return sprintf(buf, "%d\n", 0);
	else
		return sprintf(buf, "%d\n", 1);
}

static ssize_t ni_reset_store(struct kobject *kobj, struct attribute *attr,
			      const char *buffer, size_t len)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);

	memset(&ni->ni_stats.el_send_stats, 0,
		sizeof(ni->ni_stats.el_send_stats));
	memset(&ni->ni_stats.el_recv_stats, 0,
	       sizeof(ni->ni_stats.el_recv_stats));
	memset(&ni->ni_stats.el_drop_stats, 0,
	       sizeof(ni->ni_stats.el_drop_stats));
	memset(&ni->ni_hstats, 0, sizeof(ni->ni_hstats));

	return len;
}
LNET_RW_ATTR(ni_reset);

/*
 * LNet peer stats attribute callback functions
 */
static ssize_t peer_total_send_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = lnet_sum_stats(&lpni->lpni_stats, LNET_STATS_TYPE_SEND);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_total_send_count);

static ssize_t peer_total_recv_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = lnet_sum_stats(&lpni->lpni_stats, LNET_STATS_TYPE_RECV);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_total_recv_count);

static ssize_t peer_total_drop_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = lnet_sum_stats(&lpni->lpni_stats, LNET_STATS_TYPE_DROP);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_total_drop_count);

static ssize_t peer_get_send_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_send_stats.co_get_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_get_send_count);

static ssize_t peer_put_send_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_send_stats.co_put_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_put_send_count);

static ssize_t peer_reply_send_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_send_stats.co_reply_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_reply_send_count);

static ssize_t peer_ack_send_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_send_stats.co_ack_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_ack_send_count);

static ssize_t peer_hello_send_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_send_stats.co_hello_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_hello_send_count);

static ssize_t peer_get_recv_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_recv_stats.co_get_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_get_recv_count);

static ssize_t peer_put_recv_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_recv_stats.co_put_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_put_recv_count);

static ssize_t peer_reply_recv_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_recv_stats.co_reply_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_reply_recv_count);

static ssize_t peer_ack_recv_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_recv_stats.co_ack_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_ack_recv_count);

static ssize_t peer_hello_recv_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_recv_stats.co_hello_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_hello_recv_count);

static ssize_t peer_get_drop_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_drop_stats.co_get_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_get_drop_count);

static ssize_t peer_put_drop_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_drop_stats.co_put_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_put_drop_count);

static ssize_t peer_reply_drop_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_drop_stats.co_reply_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_reply_drop_count);

static ssize_t peer_ack_drop_count_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_drop_stats.co_ack_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_ack_drop_count);

static ssize_t peer_hello_drop_count_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_stats.el_drop_stats.co_hello_count);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_hello_drop_count);

static ssize_t peer_state_show(struct kobject *kobj,
			       struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	unsigned count = lpni->lpni_state;
	return snprintf(buf, PAGE_SIZE, "%u\n", count);
}
LNET_RO_ATTR(peer_state);

static ssize_t peer_min_tx_credits_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = lpni->lpni_mintxcredits;
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_min_tx_credits);

static ssize_t peer_avail_tx_credits_show(struct kobject *kobj,
					  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = lpni->lpni_txcredits;
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_avail_tx_credits);

static ssize_t peer_tx_q_nob_show(struct kobject *kobj,
				  struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	long count = lpni->lpni_txqnob;
	return snprintf(buf, PAGE_SIZE, "%ld\n", count);
}
LNET_RO_ATTR(peer_tx_q_nob);

static ssize_t peer_min_rtr_credits_show(struct kobject *kobj,
					 struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = lpni->lpni_minrtrcredits;
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_min_rtr_credits);

static ssize_t peer_avail_rtr_credits_show(struct kobject *kobj,
					   struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = lpni->lpni_rtrcredits;
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(peer_avail_rtr_credits);

static ssize_t remote_dropped_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_hstats.hlt_remote_dropped);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(remote_dropped);

static ssize_t remote_timeout_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_hstats.hlt_remote_timeout);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(remote_timeout);

static ssize_t remote_error_show(struct kobject *kobj,
				 struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_hstats.hlt_remote_error);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(remote_error);

static ssize_t network_timeout_show(struct kobject *kobj,
				    struct attribute *attr, char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;
	int count = atomic_read(&lpni->lpni_hstats.hlt_network_timeout);
	return snprintf(buf, PAGE_SIZE, "%d\n", count);
}
LNET_RO_ATTR(network_timeout);

static ssize_t peer_reset_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;

	if (!lnet_sum_stats(&lpni->lpni_stats, LNET_STATS_TYPE_SEND) &&
	    !lnet_sum_stats(&lpni->lpni_stats, LNET_STATS_TYPE_RECV) &&
	    !lnet_sum_stats(&lpni->lpni_stats, LNET_STATS_TYPE_DROP))
		return sprintf(buf, "%d\n", 0);
	else
		return sprintf(buf, "%d\n", 1);
}

static ssize_t peer_reset_store(struct kobject *kobj, struct attribute *attr,
				const char *buffer, size_t len)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	struct lnet_peer_ni *lpni = spni->peer_ni;

	memset(&lpni->lpni_stats.el_send_stats, 0,
	       sizeof(lpni->lpni_stats.el_send_stats));
	memset(&lpni->lpni_stats.el_recv_stats, 0,
	       sizeof(lpni->lpni_stats.el_recv_stats));
	memset(&lpni->lpni_stats.el_drop_stats, 0,
	       sizeof(lpni->lpni_stats.el_drop_stats));
	memset(&lpni->lpni_hstats, 0, sizeof(lpni->lpni_hstats));

	return len;
}
LNET_RW_ATTR(peer_reset);

static struct attribute *lnet_ni_stats_attrs[] = {
	&lnet_attr_total_send_count.attr,
	&lnet_attr_total_recv_count.attr,
	&lnet_attr_total_drop_count.attr,
	&lnet_attr_get_send_count.attr,
	&lnet_attr_put_send_count.attr,
	&lnet_attr_reply_send_count.attr,
	&lnet_attr_ack_send_count.attr,
	&lnet_attr_hello_send_count.attr,
	&lnet_attr_get_recv_count.attr,
	&lnet_attr_put_recv_count.attr,
	&lnet_attr_reply_recv_count.attr,
	&lnet_attr_ack_recv_count.attr,
	&lnet_attr_hello_recv_count.attr,
	&lnet_attr_get_drop_count.attr,
	&lnet_attr_put_drop_count.attr,
	&lnet_attr_reply_drop_count.attr,
	&lnet_attr_ack_drop_count.attr,
	&lnet_attr_hello_drop_count.attr,
	&lnet_attr_local_interrupt.attr,
	&lnet_attr_local_dropped.attr,
	&lnet_attr_local_aborted.attr,
	&lnet_attr_local_no_route.attr,
	&lnet_attr_local_timeout.attr,
	&lnet_attr_local_error.attr,
	&lnet_attr_ni_reset.attr,
	NULL,
};

static struct attribute *lnet_peer_stats_attrs[] = {
	&lnet_attr_peer_total_send_count.attr,
	&lnet_attr_peer_total_recv_count.attr,
	&lnet_attr_peer_total_drop_count.attr,
	&lnet_attr_peer_get_send_count.attr,
	&lnet_attr_peer_put_send_count.attr,
	&lnet_attr_peer_reply_send_count.attr,
	&lnet_attr_peer_ack_send_count.attr,
	&lnet_attr_peer_hello_send_count.attr,
	&lnet_attr_peer_get_recv_count.attr,
	&lnet_attr_peer_put_recv_count.attr,
	&lnet_attr_peer_reply_recv_count.attr,
	&lnet_attr_peer_ack_recv_count.attr,
	&lnet_attr_peer_hello_recv_count.attr,
	&lnet_attr_peer_get_drop_count.attr,
	&lnet_attr_peer_put_drop_count.attr,
	&lnet_attr_peer_reply_drop_count.attr,
	&lnet_attr_peer_ack_drop_count.attr,
	&lnet_attr_peer_hello_drop_count.attr,
	&lnet_attr_peer_state.attr,
	&lnet_attr_peer_min_tx_credits.attr,
	&lnet_attr_peer_avail_tx_credits.attr,
	&lnet_attr_peer_tx_q_nob.attr,
	&lnet_attr_peer_min_rtr_credits.attr,
	&lnet_attr_peer_avail_rtr_credits.attr,
	&lnet_attr_remote_dropped.attr,
	&lnet_attr_remote_timeout.attr,
	&lnet_attr_remote_error.attr,
	&lnet_attr_network_timeout.attr,
	&lnet_attr_peer_reset.attr,
	NULL,
};

static void ni_sysfs_release(struct kobject *kobj)
{
	struct lnet_ni *ni = container_of(kobj, struct lnet_ni, stats_kobj);

	complete(&ni->stats_kobj_unregister);
}

static struct kobj_type ni_stats_ktype = {
	.sysfs_ops      = &lnet_sysfs_ops,
	.release        = ni_sysfs_release,
	.default_attrs  = lnet_ni_stats_attrs,
};

static void peer_ni_sysfs_release(struct kobject *kobj)
{
	struct lnet_sysfs_peer *spni = container_of(kobj, struct lnet_sysfs_peer,
						    stats_kobj);
	complete(&spni->stats_kobj_unregister);
}

static struct kobj_type peer_ni_stats_ktype = {
	.sysfs_ops      = &lnet_sysfs_ops,
	.release        = peer_ni_sysfs_release,
	.default_attrs  = lnet_peer_stats_attrs,
};

int lnet_net_sysfs_setup(__u32 net_id, struct lnet_net *net)
{
	struct lnet_net *net_l = NULL;
	const char *nw = libcfs_net2str(net_id);
	int rc = 0;

	if (LNET_NETTYP(net_id) == LOLND)
		return rc;

	if (lnet_net_unique(net_id, &the_lnet.ln_nets, &net_l)) {
		net->nwid_kset = kset_create_and_add(nw, NULL,
						     the_lnet.ln_net_kobj);
		if (!net->nwid_kset) {
			CERROR("Cannot add new kset for %s\n", nw);
			return -ENOMEM;
		}
	} else
		net->nwid_kset = kset_get(net_l->nwid_kset);

	return rc;
}

int lnet_ni_sysfs_setup(struct lnet_ni *ni, char *iface)
{
	struct lnet_net *net = ni->ni_net;
	struct lnet_net *net_l;
	int rc = 0;

	if (LNET_NETTYP(net->net_id) == LOLND)
		return rc;

	if (lnet_net_unique(net->net_id, &the_lnet.ln_nets, &net_l))
		net_l = net;

	if (!lnet_ni_unique_net(&net_l->net_ni_list, ni->ni_interfaces[0]))
		return rc;

	ni->ni_kobj = kobject_create_and_add(iface, &net_l->nwid_kset->kobj);
	if (!ni->ni_kobj) {
		CERROR("Cannot create kobject for %s\n", iface);
		return -ENOMEM;
	}

	ni->ni_kobj->kset = kset_get(net_l->nwid_kset);

	init_completion(&ni->stats_kobj_unregister);

	rc = kobject_init_and_add(&ni->stats_kobj, &ni_stats_ktype,
				  ni->ni_kobj, "%s", "stats");
	if (rc) {
		CERROR("Cannot create kobject for stats under %s\n", iface);
		kobject_put(ni->ni_kobj);
	}

	return rc;
}

int lnet_peer_ni_sysfs_setup(struct lnet_peer_ni *lpni, struct lnet_peer *lp)
{
	struct lnet_sysfs_peer *spni;
	char *prim_nid = libcfs_nid2str(lp->lp_primary_nid);
	char *lpni_nid = libcfs_nid2str(lpni->lpni_nid);
	int rc = 0;

	if (LNET_NETTYP(LNET_NIDNET(lpni->lpni_nid)) == LOLND)
		return rc;

	if (!lp->prim_nid_kobj) {
		lp->prim_nid_kobj = kobject_create_and_add(prim_nid,
							   the_lnet.ln_peers_kobj);
		if (!lp->prim_nid_kobj) {
			CERROR("Cannot add new kobject for %s\n", prim_nid);
			return -ENOMEM;
		}
	}
	if (!lp->peer_nis_kset) {
		lp->peer_nis_kset = kset_create_and_add("peer_nis", NULL,
							lp->prim_nid_kobj);
		if (!lp->peer_nis_kset) {
			CERROR("Cannot add kset peer_nis for prim_nid %s\n",
			       prim_nid);
			kobject_put(lp->prim_nid_kobj);
			return -ENOMEM;
		}
	}

	LIBCFS_ALLOC(spni, sizeof(*spni));
	if (!spni) {
		if (lp->lp_nnis == 1) {
			kset_unregister(lp->peer_nis_kset);
			kobject_put(lp->prim_nid_kobj);
		}
		return -ENOMEM;
	}

	lpni->lpni_sysfs_peer = spni;
	spni->peer_ni = lpni;
	spni->peer = lp;
	spni->peer_nid_kobj = kobject_create_and_add(lpni_nid,
						     &lp->peer_nis_kset->kobj);
	if (!spni->peer_nid_kobj) {
		if (lp->lp_nnis == 1) {
			kset_unregister(lp->peer_nis_kset);
			kobject_put(lp->prim_nid_kobj);
		}
		CERROR("Cannot create kobject for %s\n", lpni_nid);
		LIBCFS_FREE(spni, sizeof(*spni));
		return -ENOMEM;
	}

	spni->peer_nid_kobj->kset = lp->peer_nis_kset;

	init_completion(&spni->stats_kobj_unregister);

	rc = kobject_init_and_add(&spni->stats_kobj, &peer_ni_stats_ktype,
				  spni->peer_nid_kobj, "%s", "stats");
	if (rc) {
		CERROR("Cannot create koject for stats under %s\n", lpni_nid);
		kobject_put(spni->peer_nid_kobj);
		if (lp->lp_nnis == 1) {
			kset_unregister(lp->peer_nis_kset);
			kobject_put(lp->prim_nid_kobj);
		}
		LIBCFS_FREE(spni, sizeof(*spni));
	}

	return rc;
}

void lnet_ni_sysfs_cleanup(struct lnet_ni *ni)
{
	struct lnet_net *net = ni->ni_net;

	if (LNET_NETTYP(net->net_id) == LOLND)
		return;

	kobject_put(&ni->stats_kobj);
	wait_for_completion(&ni->stats_kobj_unregister);
	kobject_put(ni->ni_kobj);
}

void lnet_net_sysfs_cleanup(struct lnet_net *net)
{
	struct lnet_net *net_l;

	if (LNET_NETTYP(net->net_id) == LOLND)
		return;

	if (!lnet_net_unique(net->net_id, &the_lnet.ln_nets, &net_l)) {
		kset_put(net_l->nwid_kset);
		return;
	}

	kset_unregister(net->nwid_kset);
}

void lnet_peer_ni_sysfs_cleanup(struct lnet_sysfs_peer *spni)
{
	struct lnet_peer *lp = spni->peer;
	struct kset *peer_nis_kset;
	struct kobject *prim_nid_kobj;

	list_del_init(&spni->spni_zombie);

	if (!spni->peer_nid_kobj) {
		LIBCFS_FREE(spni, sizeof(*spni));
		return;
	}

	peer_nis_kset = spni->peer_nid_kobj->kset;
	prim_nid_kobj = peer_nis_kset->kobj.parent;

	kobject_put(&spni->stats_kobj);
	wait_for_completion(&spni->stats_kobj_unregister);
	kobject_put(spni->peer_nid_kobj);

	if (!lp || (lp && lp->lp_nnis <= 0)) {
		kset_unregister(peer_nis_kset);
		kobject_put(prim_nid_kobj);
	}

	LIBCFS_FREE(spni, sizeof(*spni));
}

/*
 * LND Peer sysfs structure APIs
 */

int lnd_peer_sysfs_setup(lnet_nid_t peer_nid, struct lnet_ni *ni,
			 struct sysfs_lnd_peer *lnd_peer)
{
	struct kset *peers_kset = ni->ni_net->net_lnd->lnd_peers_kset;
	struct kobject *peers_kobj = &peers_kset->kobj;
	struct kobject *local_ni_dir;
	char *pnid = libcfs_nid2str(peer_nid);
	char *local_nid = libcfs_nid2str(ni->ni_nid);
	int rc = 0;

	/* Before creating the peer_ni kobject, check if it already exists.
	 * If it exists then compensate for a ref up due to
	 * kset_find_obj() */
	lnd_peer->peer_ni_kobj = kset_find_obj(peers_kset, pnid);
	if (lnd_peer->peer_ni_kobj) {
		kobject_put(lnd_peer->peer_ni_kobj);
		return rc;
	}

	lnd_peer->peer_ni_kobj = kobject_create_and_add(pnid, peers_kobj);
	if (!lnd_peer->peer_ni_kobj) {
		CERROR("Cannot add new kobject for %s\n", pnid);
		return -ENOMEM;
	}

	local_ni_dir = kobject_create_and_add("local_ni",
					      lnd_peer->peer_ni_kobj);
	if (!local_ni_dir) {
		CERROR("Cannot create directory 'local_ni'\n");
		rc = -ENOMEM;
		goto deref_peer_ni;
	}

	lnd_peer->local_ni_kobj = kobject_create_and_add(local_nid,
							 local_ni_dir);
	if (!lnd_peer->local_ni_kobj) {
		CERROR("Cannot add new kobject for %s\n", local_nid);
		rc = -ENOMEM;
		goto deref_local_ni_dir;
	}

	init_completion(&lnd_peer->stats_kobj_unregister);

	rc = kobject_init_and_add(&lnd_peer->stats_kobj, lnd_peer->stats_ktype,
				  lnd_peer->local_ni_kobj, "%s", "stats");
	if (rc) {
		CERROR("Cannot add new kobject for stats under %s\n", local_nid);
		goto deref_local_ni;
	}

	lnd_peer->peer_conns_kobj = kobject_create_and_add("conns",
							   lnd_peer->local_ni_kobj);
	if (!lnd_peer->peer_conns_kobj) {
		CERROR("Could not add kobject for conns under %s\n", local_nid);
		rc = -ENOMEM;
		goto deref_stats;
	}

	return rc;

deref_stats:
	kobject_put(&lnd_peer->stats_kobj);
deref_local_ni:
	kobject_put(lnd_peer->local_ni_kobj);
deref_local_ni_dir:
	kobject_put(local_ni_dir);
deref_peer_ni:
	kobject_put(lnd_peer->peer_ni_kobj);

	return rc;
}
EXPORT_SYMBOL(lnd_peer_sysfs_setup);

void lnd_peer_sysfs_cleanup(struct sysfs_lnd_peer *peer_ni)
{
	struct kobject *local_ni_dir = peer_ni->local_ni_kobj->parent;

	kobject_put(peer_ni->peer_conns_kobj);
	kobject_put(&peer_ni->stats_kobj);
	wait_for_completion(&peer_ni->stats_kobj_unregister);
	kobject_put(peer_ni->local_ni_kobj);
	kobject_put(local_ni_dir);
	kobject_put(peer_ni->peer_ni_kobj);
}
EXPORT_SYMBOL(lnd_peer_sysfs_cleanup);

/*
 * LND Connection sysfs setup/cleanup APIs
 */

int lnd_conn_sysfs_setup(struct sysfs_lnd_peer *peer_ni,
			 struct sysfs_lnd_conn *lnd_conn)
{
	struct kobject *conn_id_kobj;
	char conn_id[LNET_MAX_STR_LEN];
	int rc = 0;
	s64 timens = ktime_get_ns();

	snprintf(conn_id, LNET_MAX_STR_LEN, "%lld", timens);
	conn_id_kobj = kobject_create_and_add(conn_id,
					      peer_ni->peer_conns_kobj);
	if (!conn_id_kobj) {
		CERROR("Cannot create koject for the connection of %s",
		       peer_ni->peer_ni_kobj->name);
		return rc;
	}

	init_completion(&lnd_conn->stats_kobj_unregister);

	rc = kobject_init_and_add(&lnd_conn->stats_kobj, lnd_conn->stats_ktype,
				  conn_id_kobj, "%s", "stats");
	if (rc) {
		CERROR("Cannot create koject for stats under connection  %s",
		       conn_id);
		kobject_put(conn_id_kobj);
		return rc;
	}

	return rc;
}
EXPORT_SYMBOL(lnd_conn_sysfs_setup);

void lnd_conn_sysfs_cleanup(struct sysfs_lnd_conn *lnd_conn)
{
	struct kobject *conn_id_kobj = lnd_conn->stats_kobj.parent;

	kobject_put(&lnd_conn->stats_kobj);
	wait_for_completion(&lnd_conn->stats_kobj_unregister);
	kobject_put(conn_id_kobj);
}
EXPORT_SYMBOL(lnd_conn_sysfs_cleanup);
