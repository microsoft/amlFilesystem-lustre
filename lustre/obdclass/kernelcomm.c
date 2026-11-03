// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2015, 2017, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Kernel <-> userspace communication routines.
 * Using pipes for all arches.
 *
 * Author: Nathan Rutman <nathan.rutman@sun.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/file.h>
#include <linux/glob.h>
#include <linux/types.h>
#include <lustre_compat/net/genetlink.h>

#include <obd_class.h>
#include <obd_support.h>
#include <lprocfs_status.h>
#include <lustre_kernelcomm.h>

static struct genl_family lustre_family;

static struct ln_key_list device_list = {
	.lkl_maxattr			= LUSTRE_DEVICE_ATTR_MAX,
	.lkl_list			= {
		[LUSTRE_DEVICE_ATTR_HDR]	= {
			.lkp_value		= "devices",
			.lkp_key_format		= LNKF_SEQUENCE | LNKF_MAPPING,
			.lkp_data_type		= NLA_NUL_STRING,
		},
		[LUSTRE_DEVICE_ATTR_INDEX]	= {
			.lkp_value		= "index",
			.lkp_data_type		= NLA_U16
		},
		[LUSTRE_DEVICE_ATTR_STATUS]	= {
			.lkp_value		= "status",
			.lkp_data_type		= NLA_STRING
		},
		[LUSTRE_DEVICE_ATTR_CLASS]	= {
			.lkp_value		= "type",
			.lkp_data_type		= NLA_STRING
		},
		[LUSTRE_DEVICE_ATTR_NAME]	= {
			.lkp_value		= "name",
			.lkp_data_type		= NLA_STRING
		},
		[LUSTRE_DEVICE_ATTR_UUID]	= {
			.lkp_value		= "uuid",
			.lkp_data_type		= NLA_STRING
		},
		[LUSTRE_DEVICE_ATTR_REFCOUNT]	= {
			.lkp_value		= "refcount",
			.lkp_data_type		= NLA_U32
		},
	},
};

struct genl_dev_list {
	struct obd_device	*gdl_target;
	unsigned int		gdl_start;
};

static inline struct genl_dev_list *
device_dump_ctx(struct netlink_callback *cb)
{
	return (struct genl_dev_list *)cb->args[0];
}

/* For 'value' packet it contains
 *	minor		u16
 *	status		(2 characters)
 *	typ_name	(16 for enough space for things like osd-ldiskfs)
 *	obd_name	MAX_OBD_NAME
 *	obd_uuid	UUID_MAX
 *	refcount	u32
 */
#define DEVICE_VALUE_PACKET_SIZE	(2 + 2 + 16 + MAX_OBD_NAME + UUID_MAX + 4)
#define DEVICE_KEY_TABLE_PACKET_SIZE	(44 + 28 + 28 + 28 + 28 + 28 + 32)

/* generic ->start() handler for GET requests */
static int lustre_device_list_start(struct netlink_callback *cb)
{
	struct genlmsghdr *gnlh = nlmsg_data(cb->nlh);
	struct netlink_ext_ack *extack = cb->extack;
	struct genl_dev_list *glist;
	unsigned long len = 0;
	int msg_len, rc = 0;

	OBD_ALLOC(glist, sizeof(*glist));
	if (!glist)
		return -ENOMEM;

	cb->args[0] = (long)glist;
	glist->gdl_target = NULL;
	glist->gdl_start = 0;

	msg_len = genlmsg_len(gnlh);
	if (msg_len > 0) {
		struct nlattr *params = genlmsg_data(gnlh);
		struct nlattr *dev;
		int rem;

		if (!nla_ok(params, msg_len) ||
		    !(nla_type(params) & LN_SCALAR_ATTR_LIST)) {
			NL_SET_ERR_MSG(extack, "invalid configuration");
			GOTO(report_err, rc = -EINVAL);
		}

		nla_for_each_nested(dev, params, rem) {
			struct nlattr *prop;
			int rem2;

			nla_for_each_nested(prop, dev, rem2) {
				char name[MAX_OBD_NAME];
				struct obd_device *obd;

				if (nla_type(prop) != LN_SCALAR_ATTR_VALUE ||
				    nla_strcmp(prop, "name") != 0)
					continue;

				prop = nla_next(prop, &rem2);
				if (!nla_ok(prop, rem2) ||
				    nla_type(prop) != LN_SCALAR_ATTR_VALUE)
					GOTO(report_err, rc = -EINVAL);

				rc = nla_strscpy(name, prop, sizeof(name));
				if (rc < 0)
					GOTO(report_err, rc);
				rc = 0;

				obd = class_name2obd(name);
				if (obd)
					glist->gdl_target = obd;
			}
		}
		if (!glist->gdl_target) {
			NL_SET_ERR_MSG(extack, "No devices found");
			rc = -ENOENT;
		}
		len = DEVICE_VALUE_PACKET_SIZE;
	} else {
		len = class_obd_devs_count() * DEVICE_VALUE_PACKET_SIZE;
	}

	len += DEVICE_KEY_TABLE_PACKET_SIZE;
	if (len > BIT(sizeof(cb->min_dump_alloc) << 3)) {
		NL_SET_ERR_MSG(extack, "Netlink msg is too large");
		rc = -EMSGSIZE;
	} else {
		cb->min_dump_alloc = len;
	}
report_err:
	if (rc < 0) {
		OBD_FREE(glist, sizeof(*glist));
		cb->args[0] = 0;
	}
	return rc;
}

static int lustre_device_list_dump(struct sk_buff *msg,
				   struct netlink_callback *cb)
{
	struct genl_dev_list *glist = device_dump_ctx(cb);
	struct genlmsghdr *gnlh = nlmsg_data(cb->nlh);
	struct obd_device *filter = glist->gdl_target;
	struct obd_device *obd = NULL;
	struct netlink_ext_ack *extack = cb->extack;
	int portid = NETLINK_CB(cb->skb).portid;
	int seq = cb->nlh->nlmsg_seq;
	unsigned long idx = 0;
	int rc = 0;

	if (glist->gdl_start == 0) {
		const struct ln_key_list *all[] = {
			&device_list, NULL
		};

		rc = lnet_genl_send_scalar_list(msg, portid, seq,
						&lustre_family, gnlh->version,
						NLM_F_CREATE | NLM_F_MULTI,
						LUSTRE_CMD_DEVICES, all);
		if (rc < 0) {
			NL_SET_ERR_MSG(extack, "failed to send key table");
			goto send_err;
		}
	}

	obd_device_lock();
	obd_device_for_each_start(idx, obd, glist->gdl_start) {
		const char *status;
		void *hdr;

		if (filter && filter != obd)
			continue;

		hdr = genlmsg_put(msg, portid, seq, &lustre_family,
				  NLM_F_MULTI, LUSTRE_CMD_DEVICES);
		if (!hdr) {
			NL_SET_ERR_MSG(extack, "failed to send values");
			genlmsg_cancel(msg, hdr);
			rc = -EMSGSIZE;
			break;
		}

		if (idx == 0)
			nla_put_string(msg, LUSTRE_DEVICE_ATTR_HDR, "");

		nla_put_u16(msg, LUSTRE_DEVICE_ATTR_INDEX, obd->obd_minor);

		/* Collect only the index value for a single obd */
		if (filter) {
			genlmsg_end(msg, hdr);
			idx++;
			break;
		}

		if (test_bit(OBDF_STOPPING, obd->obd_flags))
			status = "ST";
		else if (test_bit(OBDF_INACTIVE, obd->obd_flags))
			status = "IN";
		else if (test_bit(OBDF_SET_UP, obd->obd_flags))
			status = "UP";
		else if (test_bit(OBDF_ATTACHED, obd->obd_flags))
			status = "AT";
		else
			status = "--";

		nla_put_string(msg, LUSTRE_DEVICE_ATTR_STATUS, status);

		nla_put_string(msg, LUSTRE_DEVICE_ATTR_CLASS,
			       obd->obd_type->typ_name);

		nla_put_string(msg, LUSTRE_DEVICE_ATTR_NAME,
			       obd->obd_name);

		nla_put_string(msg, LUSTRE_DEVICE_ATTR_UUID,
			       obd->obd_uuid.uuid);

		nla_put_u32(msg, LUSTRE_DEVICE_ATTR_REFCOUNT,
			    kref_read(&obd->obd_refcount));

		genlmsg_end(msg, hdr);
	}
	obd_device_unlock();

	glist->gdl_start = idx + 1;
send_err:
	return rc < 0 ? rc : msg->len;
}

static int lustre_device_done(struct netlink_callback *cb)
{
	struct genl_dev_list *glist;

	glist = device_dump_ctx(cb);
	if (glist) {
		OBD_FREE(glist, sizeof(*glist));
		cb->args[0] = 0;
	}

	return 0;
}

/* Generic obd device Netlink handling */

#define LS_NL_SCAN	LS_BIT_2

int lustre_obd_nl_done(struct netlink_callback *cb)
{
	struct lustre_nl_ctx *ctx = lustre_nl_dump_ctx(cb);

	if (ctx) {
		unsigned long idx;
		void *data;

		if (ctx->lnc_free_keys && ctx->lnc_keys)
			ctx->lnc_free_keys(ctx);

		/* Xas has locking which can collide with
		 * potential locking in release() so cleanup
		 * with the xarray directly.
		 */
		xa_for_each(ctx->lnc_xas.xa, idx, data) {
			/* obd device based */
			if (!test_bit(LS_NL_SCAN, &ctx->lnc_state)) {
				struct obd_device *obd;

				obd = ctx->lnc_get_source(data);
				if (obd)
					class_decref(obd, __func__, ctx);
			}
			if (ctx->lnc_release)
				ctx->lnc_release(ctx, data);
		}
		xa_destroy(ctx->lnc_xas.xa);

		OBD_FREE(ctx, sizeof(*ctx));
		cb->args[0] = 0;
	}

	return 0;
}
EXPORT_SYMBOL(lustre_obd_nl_done);

/* key table and value size for LUSTRE_PARAM_ATTR_HDR and
 * LUSTRE_PARAM_ATTR_SOURCE
 */
#define OBD_GENL_HDR_SIZE		48
#define OBD_GENL_SOURCE_SIZE		(28 + MAX_OBD_NAME * 3)

static int lustre_nl_scan(struct lustre_nl_ctx *ctx,
			  struct netlink_callback *cb,
			  struct nlattr **prop, int *rem2, void *src)
{
	struct netlink_ext_ack *extack = cb->extack;
	int rc;

	rc = ctx->lnc_collect(ctx, src, prop, rem2);
	switch (rc) {
	case -ENOMEM:
		NL_SET_ERR_MSG(extack,
			       "failed to allocate data");
		break;
	case -EINVAL:
	case -E2BIG:
		NL_SET_ERR_MSG(extack,
			       "invalid additional data");
		break;
	case -EOPNOTSUPP:
	case -ENODEV:
	case 0: /* Success */
		break;
	default:
		NL_SET_ERR_MSG(extack,
			       "unknown scan error");
		break;
	}

	return rc;
}

static int lustre_obd_nl_scan(struct lustre_nl_ctx *ctx,
			      struct netlink_callback *cb,
			      struct nlattr **prop, int *rem2,
			      const char *type, const char *name)
{
	struct obd_device *obd, *prev = NULL;
	unsigned long idx;
	int rc = 0;

	obd_device_lock();
	obd_device_for_each_cond(idx, obd,
				 test_bit(OBDF_ATTACHED, obd->obd_flags) &&
				 test_bit(OBDF_SET_UP, obd->obd_flags) &&
				 !test_bit(OBDF_STOPPING, obd->obd_flags)) {
		/* Now filter by obd_type */
		if (!glob_match(type,
				obd->obd_type->typ_name))
			continue;

		/* Filter by obd_name */
		if (!glob_match(name, obd->obd_name))
			continue;

		/* prevent obd from disappearing on us. */
		class_incref(obd, __func__, ctx);
		obd_device_unlock();
		rc = lustre_nl_scan(ctx, cb, prop, rem2, obd);
		obd_device_lock();
		switch (rc) {
		case 0: /* Success */
			if (prev != obd) {
				cb->min_dump_alloc += OBD_GENL_SOURCE_SIZE;
				prev = obd;
			}
			break;
		case -EOPNOTSUPP:
		case -ENODEV:
			rc = 0;
			fallthrough;
		default:
			class_decref(obd, __func__, ctx);
			break;
		}
		if (rc < 0)
			goto unlock;
	}
unlock:
	obd_device_unlock();

	return rc;
}

/* generic ->start() handler for GET requests */
int lustre_obd_nl_start(struct netlink_callback *cb,
			struct lustre_nl_ctx *settings)
{
	struct genlmsghdr *gnlh = nlmsg_data(cb->nlh);
	struct netlink_ext_ack *extack = cb->extack;
	int msg_len = genlmsg_len(gnlh);
	struct lustre_nl_ctx *ctx;
	int rc = 0;

	if (!settings->lnc_collect || !settings->lnc_release ||
	    !settings->lnc_setup_keys || !settings->lnc_pack_msg ||
	    (!test_bit(LS_NL_SCAN, &settings->lnc_state) &&
	     !settings->lnc_get_source)) {
		NL_SET_ERR_MSG(extack, "implementation is invalid");
		return -EOPNOTSUPP;
	}

	OBD_ALLOC(ctx, sizeof(*ctx));
	if (!ctx) {
		NL_SET_ERR_MSG(extack, "failed to setup ctx");
		return -ENOMEM;
	}
	*ctx = *settings;
	xa_init(&ctx->lnc_array);

	cb->args[0] = (long)ctx;
#ifndef HAVE_GENL_INFO_FAMILY
	/* Since we want to build on a newer kernels which is always
	 * struct genl_info to have access to the family but also
	 * for it to work on older kernels a compat_genl_info is used
	 * which contains the needed family.
	 */
	cb->args[1] = (unsigned long)&ctx->lnc_info;
#endif
	cb->min_dump_alloc = OBD_GENL_HDR_SIZE;

	if (msg_len > 0) {
		struct nlattr *params = genlmsg_data(gnlh);
		XA_STATE(xas, &ctx->lnc_array, 0);
		struct nlattr *entry;
		int rem;

		ctx->lnc_xas = xas;

		if (!nla_ok(params, msg_len) ||
		    !(nla_type(params) & LN_SCALAR_ATTR_LIST)) {
			NL_SET_ERR_MSG(extack, "invalid configuration");
			GOTO(report_err, rc = -EINVAL);
		}

		nla_for_each_nested(entry, params, rem) {
			struct nlattr *prop;
			int rem2;

			nla_for_each_nested(prop, entry, rem2) {
				char src[MAX_OBD_NAME * 3], *type;
				ssize_t len;

				if (nla_type(prop) != LN_SCALAR_ATTR_VALUE ||
				    nla_strcmp(prop, "source") != 0)
					continue;

				prop = nla_next(prop, &rem2);
				if (!nla_ok(prop, rem2) ||
				    nla_type(prop) != LN_SCALAR_ATTR_VALUE) {
					NL_SET_ERR_MSG(extack,
						       "source has invalid value");
					GOTO(report_err, rc = -EINVAL);
				}

				memset(src, 0, sizeof(src));
				len = nla_strscpy(src, prop, sizeof(src));
				if (len < 0) {
					NL_SET_ERR_MSG(extack,
						       "invalid source key string");
					GOTO(report_err, rc = (int)len);
				}
				type = strim(src); /* remove any whitespaces */

				if (!test_bit(LS_NL_SCAN, &ctx->lnc_state)) {
					char *name;

					len = strcspn(type, ".");
					if (!len) {
						NL_SET_ERR_MSG(extack,
							       "invalid source format");
						GOTO(report_err, rc = -EINVAL);
					}

					type[len] = '\0';
					name = type + len + 1;

					rc = lustre_obd_nl_scan(ctx, cb, &prop,
								&rem2, type,
								name);
				} else {
					rc = lustre_nl_scan(ctx, cb, &prop,
							    &rem2, type);
					switch (rc) {
					case -EOPNOTSUPP:
					case -ENODEV:
						rc = 0;
						break;
					case 0:
						cb->min_dump_alloc +=
							OBD_GENL_SOURCE_SIZE;
						break;
					}
				}
				if (rc < 0)
					GOTO(report_err, rc);
			}
		}
	} else {
		XA_STATE(xas, &ctx->lnc_array, 0);

		ctx->lnc_xas = xas;
		if (test_bit(LS_NL_SCAN, &ctx->lnc_state)) {
			rc = lustre_nl_scan(ctx, cb, NULL, NULL, NULL);
			switch (rc) {
			case -EOPNOTSUPP:
			case -ENODEV:
				rc = 0;
				break;
			case 0:
				cb->min_dump_alloc +=
					OBD_GENL_SOURCE_SIZE;
				break;
			}
		} else {
			rc = lustre_obd_nl_scan(ctx, cb, NULL, NULL, "*",
						"*");
		}
		if (rc < 0)
			GOTO(report_err, rc);
	}

	if (!xa_empty(ctx->lnc_xas.xa)) {
		ssize_t len;

		xas_set(&ctx->lnc_xas, 0);
		len = ctx->lnc_setup_keys(ctx);
		if (len < 0)
			rc = (int)len;
		else
			cb->min_dump_alloc += len;
		xas_set(&ctx->lnc_xas, 0);
	} else {
		NL_SET_ERR_MSG(extack, "Nothing found");
		rc = -ENOENT;
	}
report_err:
	if (rc < 0)
		lustre_obd_nl_done(cb);

	return rc;
}
EXPORT_SYMBOL(lustre_obd_nl_start);

int lustre_obd_nl_simple_dump(struct sk_buff *msg,
			      struct netlink_callback *cb)
{
	const struct compat_genl_info *info = compat_genl_info_dump(cb);
	struct lustre_nl_ctx *ctx = lustre_nl_dump_ctx(cb);
	struct genlmsghdr *gnlh = nlmsg_data(cb->nlh);
	struct netlink_ext_ack *extack = cb->extack;
	struct obd_device *prev = ctx->lnc_prev;
	int portid = NETLINK_CB(cb->skb).portid;
	int seq = cb->nlh->nlmsg_seq, rc = 0;
	void *data, *hdr;

	if (!test_bit(LS_KEYS_SENT, &ctx->lnc_state)) {
		rc = lnet_genl_send_scalar_list(msg, portid, seq,
						info->family, gnlh->version,
						NLM_F_CREATE | NLM_F_MULTI,
						gnlh->cmd, ctx->lnc_keys);
		if (rc < 0) {
			NL_SET_ERR_MSG(extack, "failed to send key table");
			return rc;
		}
		set_bit(LS_KEYS_SENT, &ctx->lnc_state);
		rc = 0;
	}

	rcu_read_lock();
	xas_for_each(&ctx->lnc_xas, data, OBD_MAX_INDEX) {
		char src[MAX_OBD_NAME * 3];
		struct obd_device *obd;

		obd = ctx->lnc_get_source(data);
		if (!obd)
			continue;

		if (!test_bit(OBDF_ATTACHED, obd->obd_flags) ||
		    !test_bit(OBDF_SET_UP, obd->obd_flags) ||
		     test_bit(OBDF_STOPPING, obd->obd_flags))
			continue;

		hdr = genlmsg_put(msg, portid, seq, info->family, NLM_F_MULTI,
				  gnlh->cmd);
		if (!hdr) {
			NL_SET_ERR_MSG(extack, "failed to send values");
			GOTO(send_error, rc = -EMSGSIZE);
		}

		if (!test_bit(LS_HDR_CLEARED, &ctx->lnc_state))
			if (nla_put_string(msg, LUSTRE_PARAM_ATTR_HDR, "")) {
				NL_SET_ERR_MSG(extack, "failed to clear header");
				GOTO(send_error, rc = -EMSGSIZE);
			}

		if (obd != prev) {
			memset(src, 0, sizeof(src));
			snprintf(src, sizeof(src), "%s.%s",
				 obd->obd_type->typ_name, obd->obd_name);
			if (nla_put_string(msg, LUSTRE_PARAM_ATTR_SOURCE, src)) {
				NL_SET_ERR_MSG(extack, "failed to pack source");
				GOTO(send_error, rc = -EMSGSIZE);
			}
			prev = obd;
		}

		/* If version is zero we just want the source */
		if (gnlh->version) {
			rcu_read_unlock();
			rc = ctx->lnc_pack_msg(ctx, msg, data);
			rcu_read_lock();
			if (rc < 0) {
				NL_SET_ERR_MSG(extack, "failed to pack msg");
				GOTO(send_error, rc);
			}
		}

		set_bit(LS_HDR_CLEARED, &ctx->lnc_state);
		genlmsg_end(msg, hdr);
	}
send_error:
	if (rc < 0) {
		genlmsg_cancel(msg, hdr);
		if (ctx->lnc_xas.xa_index == 0)
			xas_reset(&ctx->lnc_xas);
		else
			xas_prev(&ctx->lnc_xas);
	} else {
		ctx->lnc_prev = prev;
		rc = msg->len;
	}
	rcu_read_unlock();
	return rc;
}
EXPORT_SYMBOL(lustre_obd_nl_simple_dump);

int lustre_obd_nl_dump(struct sk_buff *msg, struct netlink_callback *cb)
{
	const struct compat_genl_info *info = compat_genl_info_dump(cb);
	struct lustre_nl_ctx *ctx = lustre_nl_dump_ctx(cb);
	struct genlmsghdr *gnlh = nlmsg_data(cb->nlh);
	struct netlink_ext_ack *extack = cb->extack;
	struct obd_device *prev = ctx->lnc_prev;
	int portid = NETLINK_CB(cb->skb).portid;
	int seq = cb->nlh->nlmsg_seq, rc = 0, i;
	void *hdr = NULL, *data;
	unsigned long idx = 0;

	if (!test_bit(LS_KEYS_SENT, &ctx->lnc_state)) {
		rc = lnet_genl_send_scalar_list(msg, portid, seq,
						info->family, gnlh->version,
						NLM_F_CREATE | NLM_F_MULTI,
						gnlh->cmd, ctx->lnc_keys);
		if (rc < 0) {
			NL_SET_ERR_MSG(extack, "failed to send key table");
			return rc;
		}
		set_bit(LS_KEYS_SENT, &ctx->lnc_state);
		rc = 0;
	}

	rcu_read_lock();
	xas_for_each(&ctx->lnc_xas, data, OBD_MAX_INDEX) {
		struct nlattr *list, *list_attr;
		char src[MAX_OBD_NAME * 3];
		struct obd_device *obd;

		obd = ctx->lnc_get_source(data);
		if (!obd)
			continue;

		if (!test_bit(OBDF_ATTACHED, obd->obd_flags) ||
		    !test_bit(OBDF_SET_UP, obd->obd_flags) ||
		    test_bit(OBDF_STOPPING, obd->obd_flags))
			continue;

		if (obd != prev) {
			struct genlmsghdr *msg_hdr;

			if (hdr) {
				set_bit(LS_HDR_CLEARED, &ctx->lnc_state);
				idx = ctx->lnc_xas.xa_index;
				genlmsg_end(msg, hdr);
			}

			hdr = genlmsg_put(msg, portid, seq, info->family,
					  NLM_F_MULTI, gnlh->cmd);
			if (!hdr) {
				NL_SET_ERR_MSG(extack, "failed to send values");
				GOTO(send_error, rc = -EMSGSIZE);
			}

			/* By default genlmsg_put() sets message version to
			 * the family version. Update the version to match
			 * what was sent to the kernel.
			 */
			msg_hdr = (struct genlmsghdr *)((char *)hdr - GENL_HDRLEN);
			msg_hdr->version = gnlh->version;

			if (!test_bit(LS_HDR_CLEARED, &ctx->lnc_state))
				if (nla_put_string(msg, LUSTRE_PARAM_ATTR_HDR,
						   "")) {
					NL_SET_ERR_MSG(extack,
						       "failed to clear header");
					GOTO(send_error, rc = -EMSGSIZE);
				}

			memset(src, 0, sizeof(src));
			snprintf(src, sizeof(src), "%s.%s",
				 obd->obd_type->typ_name, obd->obd_name);
			if (nla_put_string(msg, LUSTRE_PARAM_ATTR_SOURCE,
					   src)) {
				NL_SET_ERR_MSG(extack,
					       "failed to pack source");
				GOTO(send_error, rc = -EMSGSIZE);
			}
			prev = obd;
			i = 0;

		}

		/* Users can request a list that can be any size and there
		 * are limits to how big the sk buffer can grow. In such a
		 * case if its too large of a message then report -EMSGSIZE.
		 * Since the message is incomplete we cancel this current
		 * message and try again. If we fail the second time then
		 * userland will see the -EMSGSIZE.
		 */
		list = nla_nest_start(msg, LUSTRE_PARAM_ATTR_LIST + i++);
		if (!list) {
			NL_SET_ERR_MSG(extack, "failed to create list");
			GOTO(send_error, rc = -EMSGSIZE);
		}

		list_attr = nla_nest_start(msg, 0);
		if (!list_attr) {
			nla_nest_cancel(msg, list);

			NL_SET_ERR_MSG(extack, "failed to pack list");
			GOTO(send_error, rc = -EMSGSIZE);
		}

		rcu_read_unlock();
		rc = ctx->lnc_pack_msg(ctx, msg, data);
		rcu_read_lock();
		if (rc < 0) {
			nla_nest_cancel(msg, list_attr);
			nla_nest_cancel(msg, list);

			NL_SET_ERR_MSG(extack, "failed to pack msg");
			GOTO(send_error, rc = -EMSGSIZE);
		}

		nla_nest_end(msg, list_attr);
		nla_nest_end(msg, list);
	}

	if (hdr) /* last message */
		genlmsg_end(msg, hdr);
send_error:
	if (rc < 0) {
		genlmsg_cancel(msg, hdr);
		if (ctx->lnc_xas.xa_index == 0)
			xas_reset(&ctx->lnc_xas);
		else
			xas_set(&ctx->lnc_xas, idx);
	} else {
		ctx->lnc_prev = prev;
		rc = msg->len;
	}
	rcu_read_unlock();
	return rc;
}
EXPORT_SYMBOL(lustre_obd_nl_dump);

/* target_obd handling */
struct lu_tgt_list {
	struct lu_tgt_descs	*ltl_desc;
	struct obd_device	*ltl_obd;
	size_t			ltl_tgt_count;
};

static void *lustre_targets_find_obd(void *data)
{
	struct lu_tgt_list *ltl = data;

	return ltl->ltl_obd;
}

static int lustre_targets_collect(struct lustre_nl_ctx *ctx, void *source,
				  struct nlattr **next, int *rem)
{
	struct obd_device *obd = source;
	struct lu_tgt_descs *ltd = NULL;
	struct lu_tgt_list *ltl;
	int rc;

	/* Only look at specific obds */
	if (strncmp(obd->obd_type->typ_name,
		    LUSTRE_LMV_NAME,
		    strlen(LUSTRE_LMV_NAME)) == 0)
		ltd = &obd->u.lmv.lmv_mdt_descs;
	else if (strncmp(obd->obd_type->typ_name,
			 LUSTRE_LOV_NAME,
			 strlen(LUSTRE_LOV_NAME)) == 0)
		ltd = &obd->u.lov.lov_ost_descs;
	if (!ltd)
		return -ENODEV;

	OBD_ALLOC(ltl, sizeof(*ltl));
	if (!ltl)
		return -ENOMEM;

	/* Should we be more forgiving */
	rc = lustre_cache_object(&ctx->lnc_xas, ltl);
	if (rc) {
		OBD_FREE(ltl, sizeof(*ltl));
		return rc;
	}

	ltl->ltl_desc = ltd;
	ltl->ltl_tgt_count = 0;
	ltl->ltl_obd = obd;

	return 0;
}

static void lustre_targets_release(struct lustre_nl_ctx *ctx, void *data)
{
	struct lu_tgt_list *ltl = data;

	OBD_FREE(ltl, sizeof(*ltl));
}

static struct ln_key_list tgt_keys = {
	.lkl_maxattr			= LUSTRE_TARGET_ATTR_MAX,
	.lkl_list			= {
		[LUSTRE_TARGET_ATTR_HDR]	= {
			.lkp_value		= "target_obd",
			.lkp_key_format		= LNKF_SEQUENCE | LNKF_MAPPING,
			.lkp_data_type		= NLA_NUL_STRING,
		},
		[LUSTRE_TARGET_ATTR_SOURCE]	= {
			.lkp_value		= "source",
			.lkp_data_type		= NLA_STRING,
		},
		[LUSTRE_TARGET_ATTR_PROP_LIST]	= {
			.lkp_value		= "targets",
			.lkp_key_format		= LNKF_SEQUENCE | LNKF_MAPPING,
			.lkp_data_type		= NLA_NESTED,
		},
	},
};

static struct ln_key_list tgt_prop_keys = {
	.lkl_maxattr			= LUSTRE_TARGET_PROP_ATTR_MAX,
	.lkl_list			= {
		[LUSTRE_TARGET_PROP_ATTR_INDEX]	= {
			.lkp_value		= "index",
			.lkp_data_type		= NLA_U16
		},
		[LUSTRE_TARGET_PROP_ATTR_UUID]	= {
			.lkp_value		= "uuid",
			.lkp_data_type		= NLA_STRING
		},
		[LUSTRE_TARGET_PROP_ATTR_STATUS] = {
			.lkp_value		= "status",
			.lkp_data_type		= NLA_STRING
		},
	},
};

/* key table + values for lustre target_obd. Key value
 * measurements are collected from lnet_genl_parse_list:
 *
 *	LUSTRE_TARGET_ATTR_PROP_LIST	144
 *	LUSTRE_TARGET_PROP_ATTR_INDEX	36
 *	LUSTRE_TARGET_PROP_ATTR_UUID	28
 *	LUSTRE_TARGET_PROP_ATTR_STATUS	28
 *
 *	value packet size
 *
 *	nested header (4) + data per tgts
 *	LUSTRE_TARGET_PROP_ATTR_INDEX	8
 *	LUSTRE_TARGET_PROP_ATTR_UUID	UUID_MAX
 *	LUSTRE_TARGET_PROP_ATTR_STATUS	16
 *
 */
#define LUSTRE_TARGET_KEY_ENTRY_SIZE	(144 + 36 + 28 + 28)
#define LUSTRE_TARGET_VALUE_SIZE	(4 + 8 + UUID_MAX + 16)

static ssize_t lustre_targets_setup_keys(struct lustre_nl_ctx *ctx)
{
	ssize_t len = LUSTRE_TARGET_KEY_ENTRY_SIZE + 4;
	static const struct ln_key_list *all[] = {
		&tgt_keys, &tgt_prop_keys, NULL
	};
	unsigned long count = 0;
	void *data;

	ctx->lnc_keys = all;

	rcu_read_lock();
	xas_for_each(&ctx->lnc_xas, data, OBD_MAX_INDEX) {
		struct lu_tgt_list *ltl = data;
		struct lu_tgt_desc *tgt;

		rcu_read_unlock();
		down_read(&ltl->ltl_desc->ltd_rw_sem);
		mutex_lock(&ltl->ltl_desc->ltd_mutex);

		ltd_foreach_tgt(ltl->ltl_desc, tgt)
			ltl->ltl_tgt_count++;

		count = max(ltl->ltl_tgt_count, count);

		mutex_unlock(&ltl->ltl_desc->ltd_mutex);
		up_read(&ltl->ltl_desc->ltd_rw_sem);
		rcu_read_lock();
	}
	rcu_read_unlock();

	/* Ensure sk_buf will fit largest message */
	return len + LUSTRE_TARGET_VALUE_SIZE * count;
}

static int lustre_targets_pack_msg(struct lustre_nl_ctx *ctx,
				   struct sk_buff *msg, void *data)
{
	struct lu_tgt_list *ltl = data;
	struct lu_tgt_desc *tgt;
	struct nlattr *tgt_list;
	int j = 1, rc = 0;

	tgt_list = nla_nest_start(msg, LUSTRE_TARGET_ATTR_PROP_LIST);
	if (!tgt_list)
		return -EMSGSIZE;

	/* ensure the target list doesn't change */
	down_read(&ltl->ltl_desc->ltd_rw_sem);
	mutex_lock(&ltl->ltl_desc->ltd_mutex);

	ltd_foreach_tgt(ltl->ltl_desc, tgt) {
		struct nlattr *tgt_attr;

		/* What is calculated in setup_keys() could be different from
		 * what is reported here since there is a window for the
		 * ltl_desc to be modified. With our Netlink taking its own
		 * locks we can't keep the target locking over that time
		 * period. Just report the stale data.
		 */
		if (j > ltl->ltl_tgt_count)
			break;

		tgt_attr = nla_nest_start(msg, j++);
		if (!tgt_attr) {
			nla_nest_cancel(msg, tgt_list);
			rc = -EMSGSIZE;
			goto unlock_desc;
		}

		if (nla_put_u16(msg, LUSTRE_TARGET_PROP_ATTR_INDEX,
				tgt->ltd_index) ||
		    nla_put_string(msg, LUSTRE_TARGET_PROP_ATTR_STATUS,
				   tgt->ltd_active ? "ACTIVE" : "INACTIVE") ||
		    nla_put_string(msg, LUSTRE_TARGET_PROP_ATTR_UUID,
				   obd_uuid2str(&tgt->ltd_uuid))) {
			nla_nest_cancel(msg, tgt_attr);
			nla_nest_cancel(msg, tgt_list);
			rc = -EMSGSIZE;
			goto unlock_desc;
		}
		nla_nest_end(msg, tgt_attr);
	}
	nla_nest_end(msg, tgt_list);
unlock_desc:
	mutex_unlock(&ltl->ltl_desc->ltd_mutex);
	up_read(&ltl->ltl_desc->ltd_rw_sem);

	return rc;
}

static int lustre_targets_start(struct netlink_callback *cb)
{
	struct lustre_nl_ctx settings = {
		.lnc_collect		= lustre_targets_collect,
		.lnc_release		= lustre_targets_release,
		.lnc_setup_keys		= lustre_targets_setup_keys,
		.lnc_get_source		= lustre_targets_find_obd,
		.lnc_pack_msg		= lustre_targets_pack_msg,
	};

	return lustre_obd_nl_start(cb, &settings);
}

/* stats Netlink handling */
static const struct ln_key_list stats_list = {
	.lkl_maxattr			= LUSTRE_STATS_ATTR_MAX,
	.lkl_list			= {
		[LUSTRE_STATS_ATTR_HDR]	= {
			.lkp_value		= "stats",
			.lkp_key_format		= LNKF_SEQUENCE | LNKF_MAPPING,
			.lkp_data_type		= NLA_NUL_STRING,
		},
		[LUSTRE_STATS_ATTR_SOURCE]	= {
			.lkp_value		= "source",
			.lkp_data_type		= NLA_STRING,
		},
		[LUSTRE_STATS_ATTR_TIMESTAMP]	= {
			.lkp_value		= "snapshot_time",
			.lkp_data_type		= NLA_S64,
		},
		[LUSTRE_STATS_ATTR_START_TIME]	= {
			.lkp_value		= "start_time",
			.lkp_data_type		= NLA_S64,
		},
		[LUSTRE_STATS_ATTR_ELAPSE_TIME]	= {
			.lkp_value		= "elapsed_time",
			.lkp_data_type		= NLA_S64,
		},
		[LUSTRE_STATS_ATTR_DATASET]	= {
			.lkp_key_format		= LNKF_FLOW | LNKF_MAPPING,
			.lkp_data_type		= NLA_NESTED,
		},
	},
};

static const struct ln_key_list stats_dataset_list = {
	.lkl_maxattr				= LUSTRE_STATS_ATTR_DATASET_MAX,
	.lkl_list				= {
		[LUSTRE_STATS_ATTR_DATASET_NAME]	= {
			.lkp_data_type			= NLA_NUL_STRING,
		},
		[LUSTRE_STATS_ATTR_DATASET_COUNT]	= {
			.lkp_value			= "samples",
			.lkp_data_type			= NLA_U64,
		},
		[LUSTRE_STATS_ATTR_DATASET_UNITS]	= {
			.lkp_value			= "units",
			.lkp_data_type			= NLA_STRING,
		},
		[LUSTRE_STATS_ATTR_DATASET_MINIMUM]	= {
			.lkp_value			= "min",
			.lkp_data_type			= NLA_U64,
		},
		[LUSTRE_STATS_ATTR_DATASET_MAXIMUM]	= {
			.lkp_value			= "max",
			.lkp_data_type			= NLA_U64,
		},
		[LUSTRE_STATS_ATTR_DATASET_SUM]		= {
			.lkp_value			= "sum",
			.lkp_data_type			= NLA_U64,
		},
		[LUSTRE_STATS_ATTR_DATASET_SUMSQUARE]	= {
			.lkp_value			= "stddev",
			.lkp_data_type			= NLA_U64,
		},
	},
};

static int lustre_stats_collect(struct lustre_nl_ctx *ctx, void *filter,
				 struct nlattr **next, int *rem)
{
	return lprocfs_stats_scan(&ctx->lnc_xas, filter);
}

static void lustre_stats_release(struct lustre_nl_ctx *ctx, void *data)
{
	struct lprocfs_stats *stats = data;

	lprocfs_stats_free(&stats);
}

/* Min size for key table and its matching values. Key value
 * measurements are collected from lnet_genl_parse_list:
 *
 *	timestamp	strlen("snapshot_time") + s64
 *	start time	strlen("start time") + s64
 *	elapsed_time	strlen("elapse time") + s64
 */
#define STATS_MSG_MIN_SIZE	(36 + 32 + 36 + 536)

/* key table + values for each dataset entry. Key value
 * measurements are collected from lnet_genl_parse_list:
 *
 *	dataset name	25
 *	dataset count	strlen("samples") + u64
 *	dataset units	strlen("units") + 5
 *	dataset min	strlen("min") + u64
 *	dataset max	strlen("max") + u64
 *	dataset sum	strlen("sum") + u64
 *	dataset stdev	strlen("stddev") + u64
 */
#define STATS_MSG_DATASET_SIZE	(236 + 25 + 5 + 8 * 5)

static void lustre_stats_free_keys(struct lustre_nl_ctx *ctx)
{
	if (ctx->lnc_keys[0]) {
		int count = ctx->lnc_keys[0]->lkl_maxattr + 1;
		size_t len = sizeof(struct ln_key_list);

		if (count > LUSTRE_STATS_ATTR_MAX + 1)
			OBD_FREE(ctx->lnc_keys[0],
				 len + sizeof(struct ln_key_props) * count);
	}

	OBD_FREE_PTR_ARRAY(ctx->lnc_keys, 3);
	ctx->lnc_keys = NULL;
}

static ssize_t lustre_stats_setup_keys(struct lustre_nl_ctx *ctx)
{
	struct ln_key_list *start = (struct ln_key_list *)&stats_list;
	ssize_t len = STATS_MSG_MIN_SIZE + STATS_MSG_DATASET_SIZE;
	const struct ln_key_list **all;
	unsigned int max_dataset = 1;
	struct lprocfs_stats *stats;
	int rc = 0;

	rcu_read_lock();
	xas_for_each(&ctx->lnc_xas, stats, OBD_MAX_INDEX)
		max_dataset = max(stats->ls_num, max_dataset);
	rcu_read_unlock();

	/* We could allocate our header + stats_count + 1 here but we exploit
	 * lnet_genl_parse_list() behavior. If we have say two datasets the
	 * second nested key looks at data[2]. which is NULL, so
	 * lnet_genl_parse_list() returns 0 without emitting anything and
	 * resets idx to 0. This means the third nested key then re-uses
	 * data[1]. This is true for the N nested key as well. This saves
	 * on the message size sent. It could grow without bounds otherwise.
	 */
	OBD_ALLOC_PTR_ARRAY(all, 3);
	if (!all) {
		rc = -ENOMEM;
		goto free_keys;
	}

	if (max_dataset > 1) {
		unsigned int count = __LUSTRE_STATS_ATTR_MAX_PLUS_ONE + max_dataset;
		ssize_t key_len = sizeof(struct ln_key_props);
		int i;

		OBD_ALLOC(start, sizeof(struct ln_key_list) + key_len * count);
		if (!start) {
			rc = -ENOMEM;
			goto free_keys;
		}

		*start = stats_list; /* Set initial values */
		memcpy(start->lkl_list, stats_list.lkl_list,
		       key_len * __LUSTRE_STATS_ATTR_MAX_PLUS_ONE);
		for (i = start->lkl_maxattr + 1; i < count; i++)
			start->lkl_list[i] =
				stats_list.lkl_list[LUSTRE_STATS_ATTR_DATASET];
		start->lkl_maxattr = count - 1;
	}
	len += STATS_MSG_DATASET_SIZE * max_dataset;

	all[0] = start;
	all[1] = &stats_dataset_list;
	all[2] = NULL;
free_keys:
	ctx->lnc_keys = all;

	return rc < 0 ? rc : len;
}

int lustre_stats_put_dataset(struct lustre_nl_ctx *ctx, struct sk_buff *msg,
			     void *data)
{
	struct lprocfs_stats *stats = data;
	struct nlattr *dataset, *stat_attr;
	int rc = 0, i = 0, cnt;

	for (cnt = 0; cnt < stats->ls_num; cnt++) {
		struct lprocfs_counter_header *hdr;
		struct lprocfs_counter ctr;

		lprocfs_stats_collect(stats, cnt, &ctr);

		if (ctr.lc_count == 0)
			continue;

		hdr = &stats->ls_cnt_header[cnt];
		dataset = nla_nest_start(msg,
					 LUSTRE_STATS_ATTR_DATASET + i++);
		if (!dataset)
			return -EMSGSIZE;

		stat_attr = nla_nest_start(msg, 0);
		if (!stat_attr) {
			nla_nest_cancel(msg, dataset);
			return -EMSGSIZE;
		}

		if (nla_put_string(msg, LUSTRE_STATS_ATTR_DATASET_NAME,
				   hdr->lc_name) ||
		    nla_put_u64_64bit(msg, LUSTRE_STATS_ATTR_DATASET_COUNT,
				      ctr.lc_count,
				      LUSTRE_STATS_ATTR_DATASET_PAD) ||
		    nla_put_string(msg, LUSTRE_STATS_ATTR_DATASET_UNITS,
				   hdr->lc_units)) {
			rc = -EMSGSIZE;
			goto free_nest;
		}

		if (hdr->lc_config & LPROCFS_CNTR_AVGMINMAX) {
			if (nla_put_u64_64bit(msg,
					      LUSTRE_STATS_ATTR_DATASET_MINIMUM,
					      ctr.lc_min,
					      LUSTRE_STATS_ATTR_DATASET_PAD) ||
			    nla_put_u64_64bit(msg,
					      LUSTRE_STATS_ATTR_DATASET_MAXIMUM,
					      ctr.lc_max,
					      LUSTRE_STATS_ATTR_DATASET_PAD) ||
			    nla_put_u64_64bit(msg,
					      LUSTRE_STATS_ATTR_DATASET_SUM,
					      ctr.lc_sum,
					      LUSTRE_STATS_ATTR_DATASET_PAD)) {
				rc = -EMSGSIZE;
				goto free_nest;
			}

			if (hdr->lc_config & LPROCFS_CNTR_STDDEV &&
			    nla_put_u64_64bit(msg,
					      LUSTRE_STATS_ATTR_DATASET_SUMSQUARE,
					      ctr.lc_sumsquare,
					      LUSTRE_STATS_ATTR_DATASET_PAD)) {
				rc = -EMSGSIZE;
				goto free_nest;
			}
		}
		nla_nest_end(msg, stat_attr);
		nla_nest_end(msg, dataset);
	}
free_nest:
	if (rc < 0) {
		nla_nest_cancel(msg, stat_attr);
		nla_nest_cancel(msg, dataset);
	}

	return rc;
}
EXPORT_SYMBOL(lustre_stats_put_dataset);

static int lustre_general_stats_start(struct netlink_callback *cb)
{
	struct lustre_nl_ctx settings = {
		.lnc_collect		= lustre_stats_collect,
		.lnc_release		= lustre_stats_release,
		.lnc_setup_keys		= lustre_stats_setup_keys,
		.lnc_free_keys		= lustre_stats_free_keys,
		.lnc_pack_msg		= lustre_stats_put_dataset,
#ifndef HAVE_GENL_INFO_FAMILY
		.lnc_info.family	= &lustre_family,
#endif
	};

	set_bit(LS_NL_SCAN, &settings.lnc_state);

	return lustre_obd_nl_start(cb, &settings);
}

int lustre_stats_dump(struct sk_buff *msg, struct netlink_callback *cb)
{
	const struct compat_genl_info *info = compat_genl_info_dump(cb);
	struct lustre_nl_ctx *ctx = lustre_nl_dump_ctx(cb);
	struct genlmsghdr *gnlh = nlmsg_data(cb->nlh);
	struct netlink_ext_ack *extack = cb->extack;
	int portid = NETLINK_CB(cb->skb).portid;
	int seq = cb->nlh->nlmsg_seq;
	void *data;
	int rc = 0;

	if (!test_bit(LS_KEYS_SENT, &ctx->lnc_state)) {
		rc = lnet_genl_send_scalar_list(msg, portid, seq,
						info->family, gnlh->version,
						NLM_F_CREATE | NLM_F_MULTI,
						gnlh->cmd, ctx->lnc_keys);
		if (rc < 0) {
			NL_SET_ERR_MSG(extack, "failed to send key table");
			return rc;
		}
		set_bit(LS_KEYS_SENT, &ctx->lnc_state);
		rc = 0;
	}

	rcu_read_lock();
	xas_for_each(&ctx->lnc_xas, data, OBD_MAX_INDEX) {
		struct lprocfs_stats *stats = data;
		void *ghdr = NULL;
		char *src;

		ghdr = genlmsg_put(msg, portid, seq, info->family, NLM_F_MULTI,
				   gnlh->cmd);
		if (!ghdr) {
			NL_SET_ERR_MSG(extack, "failed to send values");
			GOTO(out_cancel, rc = -EMSGSIZE);
		}

		if (!test_bit(LS_HDR_CLEARED, &ctx->lnc_state))
			if (nla_put_string(msg, LUSTRE_PARAM_ATTR_HDR,
					   "")) {
				NL_SET_ERR_MSG(extack,
					       "failed to clear header");
				GOTO(out_cancel, rc = -EMSGSIZE);
			}

		src = stats->ls_source;
		if (strstarts(stats->ls_source, ".fs.lustre."))
			src += strlen(".fs.lustre.");
		rc = nla_put_string(msg, LUSTRE_STATS_ATTR_SOURCE, src);
		if (rc < 0) {
			NL_SET_ERR_MSG(extack, "failed to pack source");
			GOTO(out_cancel, rc = -EMSGSIZE);
		}

		/* We just want the source of the stats */
		if (!gnlh->version) {
			set_bit(LS_HDR_CLEARED, &ctx->lnc_state);
			genlmsg_end(msg, ghdr);
			continue;
		}

		/* All types of stats have the same header */
		rc = nla_put_s64(msg, LUSTRE_STATS_ATTR_TIMESTAMP,
				 ktime_get_real_ns(), LUSTRE_STATS_ATTR_PAD);
		if (rc < 0)
			GOTO(out_cancel, rc);

		if (gnlh->version > 1) {
			rc = nla_put_s64(msg, LUSTRE_STATS_ATTR_START_TIME,
					 ktime_to_ns(stats->ls_init),
					 LUSTRE_STATS_ATTR_PAD);
			if (rc < 0)
				GOTO(out_cancel, rc);

			rc = nla_put_s64(msg, LUSTRE_STATS_ATTR_ELAPSE_TIME,
					 ktime_to_ns(ktime_sub(ktime_get_real(),
							       stats->ls_init)),
					 LUSTRE_STATS_ATTR_PAD);
			if (rc < 0)
				GOTO(out_cancel, rc);
		}

		rcu_read_unlock();
		rc = ctx->lnc_pack_msg(ctx, msg, data);
		rcu_read_lock();
out_cancel:
		if (rc < 0) {
			genlmsg_cancel(msg, ghdr);
			if (ctx->lnc_xas.xa_index == 0)
				xas_reset(&ctx->lnc_xas);
			else
				xas_prev(&ctx->lnc_xas);
			rcu_read_unlock();
			return rc;
		}

		set_bit(LS_HDR_CLEARED, &ctx->lnc_state);
		genlmsg_end(msg, ghdr);
	}
	rcu_read_unlock();

	return rc < 0 ? rc : msg->len;
}
EXPORT_SYMBOL(lustre_stats_dump);

static int lustre_stats_cmd(struct sk_buff *skb, struct genl_info *info)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	struct genlmsghdr *gnlh = nlmsg_data(nlh);
	struct nlattr *params = genlmsg_data(gnlh);
	struct xarray stats_list;
	XA_STATE(xas, &stats_list, 0);
	int msg_len, rem, rc = 0;
	struct nlattr *attr;

	msg_len = genlmsg_len(gnlh);
	if (!msg_len) {
		GENL_SET_ERR_MSG(info, "no configuration");
		return -ENOMSG;
	}

	if (!nla_ok(params, msg_len) ||
	    !(nla_type(params) & LN_SCALAR_ATTR_LIST)) {
		GENL_SET_ERR_MSG(info, "invalid configuration");
		return -EINVAL;
	}

	xa_init(&stats_list);

	nla_for_each_nested(attr, params, rem) {
		struct nlattr *prop;
		int rem2;

		if (nla_type(attr) != LN_SCALAR_ATTR_LIST)
			continue;

		nla_for_each_nested(prop, attr, rem2) {
			char source[MAX_OBD_NAME * 4];

			if (nla_type(prop) != LN_SCALAR_ATTR_VALUE ||
			    nla_strcmp(prop, "source") != 0)
				continue;

			prop = nla_next(prop, &rem2);
			if (!nla_ok(prop, rem2) ||
			    nla_type(prop) != LN_SCALAR_ATTR_VALUE) {
				GENL_SET_ERR_MSG(info,
						 "source has invalid value");
				GOTO(report_err, rc = -EINVAL);
			}

			memset(source, 0, sizeof(source));
			rc = nla_strscpy(source, prop, sizeof(source));
			if (rc < 0) {
				GENL_SET_ERR_MSG(info,
						 "invalid source key string");
				GOTO(report_err, rc);
			}

			rc = lprocfs_stats_scan(&xas, source);
			if (rc < 0) {
				GENL_SET_ERR_MSG(info,
						 "stat scan failure");
				GOTO(report_err, rc);
			}
			rc = 0;
		}
	}

report_err:
	if (!xa_empty(xas.xa)) {
		void *data;

		xa_lock(xas.xa);
		xas_set(&xas, 0);
		xas_for_each(&xas, data, OBD_MAX_INDEX) {
			struct lprocfs_stats *stats = data;

			if (rc == 0)
				lprocfs_stats_clear(stats);

			xas_store(&xas, NULL);

			xa_unlock(xas.xa);
			lprocfs_stats_free(&stats);
			xa_lock(xas.xa);
		}
		xa_unlock(xas.xa);
		xa_destroy(xas.xa);
	} else if (rc == 0) {
		GENL_SET_ERR_MSG(info, "Nothing found");
		rc = -ENOENT;
	}

	return rc;
}

static const struct genl_multicast_group lustre_mcast_grps[] = {
	{ .name		= "devices",		},
	{ .name		= "target_obd",		},
	{ .name		= "stats",		},
};

static const struct genl_ops lustre_genl_ops[] = {
	{
		.cmd		= LUSTRE_CMD_DEVICES,
		.start		= lustre_device_list_start,
		.dumpit		= lustre_device_list_dump,
		.done		= lustre_device_done,
	},
	{
		.cmd		= LUSTRE_CMD_TARGETS,
		.start		= lustre_targets_start,
		.dumpit		= lustre_obd_nl_simple_dump,
		.done		= lustre_obd_nl_done,
	},
	{
		.cmd		= LUSTRE_CMD_STATS,
		.start		= lustre_general_stats_start,
		.dumpit		= lustre_stats_dump,
		.done		= lustre_obd_nl_done,
		.doit		= lustre_stats_cmd,
	},
};

static struct genl_family lustre_family = {
	.name		= LUSTRE_GENL_NAME,
	.version	= LUSTRE_GENL_VERSION,
	.module		= THIS_MODULE,
	.ops		= lustre_genl_ops,
	.n_ops		= ARRAY_SIZE(lustre_genl_ops),
	.mcgrps		= lustre_mcast_grps,
	.n_mcgrps	= ARRAY_SIZE(lustre_mcast_grps),
#ifdef GENL_FAMILY_HAS_RESV_START_OP
	.resv_start_op	= __LUSTRE_CMD_MAX_PLUS_ONE,
#endif
};

/**
 * libcfs_kkuc_msg_put() - send an message from kernel to userspace
 * @filp: file pointer to send the message to
 * @payload: Payload data.
 *
 * First field of payload is always struct kuc_hdr
 *
 * Return:
 * * %0 on success
 * * %negative on error
 */
int libcfs_kkuc_msg_put(struct file *filp, void *payload)
{
	struct kuc_hdr *kuch = (struct kuc_hdr *)payload;
	ssize_t count = kuch->kuc_msglen;
	loff_t offset = 0;
	int rc = 0;

	if (IS_ERR_OR_NULL(filp))
		return -EBADF;

	if (kuch->kuc_magic != KUC_MAGIC) {
		CERROR("KernelComm: bad magic %x\n", kuch->kuc_magic);
		return -ENOSYS;
	}

	while (count > 0) {
		rc = kernel_write(filp, payload, count, &offset);
		if (rc < 0)
			break;
		count -= rc;
		payload += rc;
		rc = 0;
	}

	if (rc < 0)
		CWARN("message send failed (%d)\n", rc);
	else
		CDEBUG(D_HSM, "Sent message rc=%d, fp=%p\n", rc, filp);

	return rc;
}
EXPORT_SYMBOL(libcfs_kkuc_msg_put);

/* Broadcast groups are global across all mounted filesystems;
 * i.e. registering for a group on 1 fs will get messages for that
 * group from any fs */
/** A single group registration has a uid and a file pointer */
struct kkuc_reg {
	struct list_head kr_chain;
	struct obd_uuid	 kr_uuid;
	int		 kr_uid;
	struct file	*kr_fp;
	char		 kr_data[];
};

static struct list_head kkuc_groups[KUC_GRP_MAX + 1];
/* Protect message sending against remove and adds */
static DECLARE_RWSEM(kg_sem);

static inline bool libcfs_kkuc_group_is_valid(int group)
{
	return 0 <= group && group < ARRAY_SIZE(kkuc_groups);
}

int __init libcfs_kkuc_init(void)
{
	int group;

	for (group = 0; group < ARRAY_SIZE(kkuc_groups); group++)
		INIT_LIST_HEAD(&kkuc_groups[group]);

	return genl_register_family(&lustre_family);
}

void libcfs_kkuc_fini(void)
{
	genl_unregister_family(&lustre_family);
}

/**
 * libcfs_kkuc_group_add() - Add a receiver to a broadcast group
 * @filp: pipe to write into
 * @uuid: uuid of the device
 * @uid: identifier for this receiver
 * @group: group number
 * @data: user data
 * @data_len: length of @data
 *
 * Return:
 * * %0 on success
 * * %negative on error
 */
int libcfs_kkuc_group_add(struct file *filp, const struct obd_uuid *uuid,
			  int uid, int group, void *data, size_t data_len)
{
	struct kkuc_reg *reg;

	if (!libcfs_kkuc_group_is_valid(group)) {
		CDEBUG(D_WARNING, "Kernelcomm: bad group %d\n", group);
		return -EINVAL;
	}

	/* fput in group_rem */
	if (filp == NULL)
		return -EBADF;

	/* freed in group_rem */
	reg = kzalloc(sizeof(*reg) + data_len, 0);
	if (reg == NULL)
		return -ENOMEM;

	reg->kr_uuid = *uuid;
	reg->kr_fp = get_file(filp);
	reg->kr_uid = uid;
	memcpy(reg->kr_data, data, data_len);

	down_write(&kg_sem);
	list_add(&reg->kr_chain, &kkuc_groups[group]);
	up_write(&kg_sem);

	CDEBUG(D_HSM, "Added uid=%d fp=%p to group %d\n", uid, filp, group);

	return 0;
}
EXPORT_SYMBOL(libcfs_kkuc_group_add);

int libcfs_kkuc_group_rem(const struct obd_uuid *uuid, int uid, int group)
{
	struct kkuc_reg *reg, *next;

	ENTRY;

	if (!libcfs_kkuc_group_is_valid(group)) {
		CDEBUG(D_WARNING, "Kernelcomm: bad group %d\n", group);
		return -EINVAL;
	}

	if (uid == 0) {
		/* Broadcast a shutdown message */
		struct kuc_hdr lh;

		lh.kuc_magic = KUC_MAGIC;
		lh.kuc_transport = KUC_TRANSPORT_GENERIC;
		lh.kuc_msgtype = KUC_MSG_SHUTDOWN;
		lh.kuc_msglen = sizeof(lh);
		libcfs_kkuc_group_put(uuid, group, &lh);
	}

	down_write(&kg_sem);
	list_for_each_entry_safe(reg, next, &kkuc_groups[group], kr_chain) {
		if (obd_uuid_equals(uuid, &reg->kr_uuid) &&
		    (uid == 0 || uid == reg->kr_uid)) {
			list_del(&reg->kr_chain);
			CDEBUG(D_HSM, "Removed uid=%d fp=%p from group %d\n",
				reg->kr_uid, reg->kr_fp, group);
			if (reg->kr_fp != NULL)
				fput(reg->kr_fp);
			kfree(reg);
		}
	}
	up_write(&kg_sem);

	RETURN(0);
}
EXPORT_SYMBOL(libcfs_kkuc_group_rem);

int libcfs_kkuc_group_put(const struct obd_uuid *uuid, int group, void *payload)
{
	struct kkuc_reg	*reg;
	int		 rc = 0;
	int one_success = 0;

	ENTRY;

	if (!libcfs_kkuc_group_is_valid(group)) {
		CDEBUG(D_WARNING, "Kernelcomm: bad group %d\n", group);
		return -EINVAL;
	}

	down_write(&kg_sem);

	if (unlikely(list_empty(&kkuc_groups[group])) ||
	    unlikely(CFS_FAIL_CHECK(OBD_FAIL_MDS_HSM_CT_REGISTER_NET))) {
		/* no agent have fully registered, CDT will retry */
		up_write(&kg_sem);
		RETURN(-EAGAIN);
	}

	list_for_each_entry(reg, &kkuc_groups[group], kr_chain) {
		if (obd_uuid_equals(uuid, &reg->kr_uuid) &&
		    reg->kr_fp != NULL) {
			rc = libcfs_kkuc_msg_put(reg->kr_fp, payload);
			if (rc == 0)
				one_success = 1;
			else if (rc == -EPIPE) {
				fput(reg->kr_fp);
				reg->kr_fp = NULL;
			}
		}
	}
	up_write(&kg_sem);

	/* don't return an error if the message has been delivered
	 * at least to one agent */
	if (one_success)
		rc = 0;

	RETURN(rc);
}
EXPORT_SYMBOL(libcfs_kkuc_group_put);

/**
 * libcfs_kkuc_group_foreach() - Calls a callback function for each link of the
 * given kuc group.
 * @uuid: uuid of the device
 * @group: the group to call the function on.
 * @cb_func: the function to be called.
 * @cb_arg: extra argument to be passed to the callback function.
 *
 * Return:
 * * %0 on success
 * * %negative on error
 */
int libcfs_kkuc_group_foreach(const struct obd_uuid *uuid, int group,
			      libcfs_kkuc_cb_t cb_func, void *cb_arg)
{
	struct kkuc_reg	*reg;
	int		 rc = 0;

	ENTRY;

	if (!libcfs_kkuc_group_is_valid(group)) {
		CDEBUG(D_WARNING, "Kernelcomm: bad group %d\n", group);
		RETURN(-EINVAL);
	}

	down_read(&kg_sem);
	list_for_each_entry(reg, &kkuc_groups[group], kr_chain) {
		if (obd_uuid_equals(uuid, &reg->kr_uuid) && reg->kr_fp != NULL)
			rc = cb_func(reg->kr_data, cb_arg);
	}
	up_read(&kg_sem);

	RETURN(rc);
}
EXPORT_SYMBOL(libcfs_kkuc_group_foreach);
