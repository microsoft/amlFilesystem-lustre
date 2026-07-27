// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2017, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Author: Isaac Huang <isaac@clusterfs.com>
 */

#include <lustre_compat/linux/string.h>
#include "selftest.h"

static int brw_srv_workitems = SFW_TEST_WI_MAX;
module_param(brw_srv_workitems, int, 0644);
MODULE_PARM_DESC(brw_srv_workitems, "# BRW server workitems");

static int brw_inject_errors;
module_param(brw_inject_errors, int, 0644);
MODULE_PARM_DESC(brw_inject_errors, "# data errors to inject randomly, zero by default");

#define BRW_POISON	0xbeefbeefbeefbeefULL
#define BRW_MAGIC	0xeeb0eeb1eeb2eeb3ULL
#define BRW_MSIZE	sizeof(__u64)

/* Distance between two markers of the LST_BRW_CHECK_SIMPLE pattern when the
 * session has the LST_FEAT_BULK_STRIDE feature. The two nodes of a test can
 * have different page sizes. Thus the markers are put at a fixed distance in
 * the bulk buffer, and not at page boundaries. The distance is the smallest
 * supported page size, so that each page of each node gets at least one
 * marker.
 */
#define BRW_STRIDE	4096

static void
brw_client_fini(struct sfw_test_instance *tsi)
{
	struct srpc_bulk *bulk;
	struct sfw_test_unit *tsu;

	LASSERT(tsi->tsi_is_client);

	list_for_each_entry(tsu, &tsi->tsi_units, tsu_list) {
		bulk = tsu->tsu_private;
		if (bulk == NULL)
			continue;

		srpc_free_bulk(bulk);
		tsu->tsu_private = NULL;
	}
}

static int
brw_client_init(struct sfw_test_instance *tsi)
{
	struct sfw_session *sn = tsi->tsi_batch->bat_session;
	struct sfw_test_unit *tsu;
	struct srpc_bulk *bulk;
	unsigned int len;
	int flags, off, opc;

	LASSERT(sn != NULL);
	LASSERT(tsi->tsi_is_client);

	if ((sn->sn_features & LST_FEAT_BULK_LEN) == 0) {
		struct test_bulk_req *breq = &tsi->tsi_u.bulk_v0;

		opc   = breq->blk_opc;
		flags = breq->blk_flags;

		/* NB blk_npg is off the wire; an unchecked multiply can
		 * wrap len to a value the checks below accept.
		 */
		if (breq->blk_npg == 0 || breq->blk_npg > LNET_MAX_IOV)
			return -EINVAL;
		/* NB: this is not going to work for variable page size,
		 * but we have to keep it for compatibility
		 */
		len   = breq->blk_npg * PAGE_SIZE;
		off   = 0;

	} else {
		struct test_bulk_req_v1 *breq = &tsi->tsi_u.bulk_v1;

		/* I should never get this step if it's unknown feature
		 * because make_session will reject unknown feature
		 */
		LASSERT((sn->sn_features & ~LST_FEATS_MASK) == 0);

		opc   = breq->blk_opc;
		flags = breq->blk_flags;
		len   = breq->blk_len;
		off   = breq->blk_offset;

		/* Reject rather than mask an offset outside the first
		 * page - masking silently runs a different test than
		 * the one requested.
		 */
		if (breq->blk_offset >= PAGE_SIZE)
			return -EINVAL;
	}

	if (off % BRW_MSIZE != 0)
		return -EINVAL;

	if (len == 0 || len > LNET_MTU)
		return -EINVAL;

	if (off + len > LNET_MTU)
		return -EINVAL;

	if (opc != LST_BRW_READ && opc != LST_BRW_WRITE)
		return -EINVAL;

	if (flags != LST_BRW_CHECK_NONE &&
	    flags != LST_BRW_CHECK_FULL &&
	    flags != LST_BRW_CHECK_SIMPLE &&
	    flags != LST_BRW_CHECK_DISCARD)
		return -EINVAL;

	list_for_each_entry(tsu, &tsi->tsi_units, tsu_list) {
		int cpt = lnet_nid2cpt(&tsu->tsu_dest.nid, NULL);

		bulk = srpc_alloc_bulk(cpt, off + len);
		if (bulk == NULL) {
			brw_client_fini(tsi);
			return -ENOMEM;
		}
		srpc_init_bulk(bulk, off, len, opc == LST_BRW_READ);
		bulk->bk_discard = (flags == LST_BRW_CHECK_DISCARD);

		tsu->tsu_private = bulk;
	}

	return 0;
}

static int brw_inject_one_error(void)
{
	struct timespec64 ts;

	if (brw_inject_errors <= 0)
		return 0;

	ktime_get_ts64(&ts);

	if (((ts.tv_nsec / NSEC_PER_USEC) & 1) == 0)
		return 0;

	return brw_inject_errors--;
}

/**
 * brw_fill_page() - Write the check pattern into one page of a bulk buffer.
 * @pg: Page to fill.
 * @off: Offset of the data in @pg.
 * @len: Number of bytes of data in @pg.
 * @pos: Offset of the data in the bulk buffer.
 * @bklen: Total number of bytes in the bulk buffer.
 * @pattern: One of the LST_BRW_CHECK_* patterns.
 * @magic: Value to write.
 * @strided: The session has the LST_FEAT_BULK_STRIDE feature.
 *
 * For LST_BRW_CHECK_SIMPLE with @strided the markers are put at the start of
 * each BRW_STRIDE block of the bulk buffer, and at the end of the buffer. The
 * positions are relative to the buffer and not to the page. Thus the two
 * nodes of a test agree on them even if their page sizes are different.
 *
 * Without @strided the markers are put at the first and at the last __u64 of
 * the page. The two nodes agree on them only if their page sizes and their
 * start offsets in the bulk buffer are the same.
 */
static void
brw_fill_page(struct page *pg, int off, int len, int pos, int bklen,
	      int pattern, __u64 magic, bool strided)
{
	char *addr = page_address(pg) + off;
	int i;

	LASSERT(addr != NULL);
	LASSERT(off % BRW_MSIZE == 0 && len % BRW_MSIZE == 0);

	if (pattern == LST_BRW_CHECK_NONE || pattern == LST_BRW_CHECK_DISCARD)
		return;

	if (magic == BRW_MAGIC)
		magic += brw_inject_one_error();

	if (pattern == LST_BRW_CHECK_SIMPLE) {
		if (!strided) {
			memcpy(addr, &magic, BRW_MSIZE);
			if (len > BRW_MSIZE)
				memcpy(addr + len - BRW_MSIZE, &magic,
				       BRW_MSIZE);
			return;
		}

		for (i = roundup(pos, BRW_STRIDE) - pos; i < len;
		     i += BRW_STRIDE)
			memcpy(addr + i, &magic, BRW_MSIZE);

		i = bklen - BRW_MSIZE - pos;
		if (i >= 0 && i < len)
			memcpy(addr + i, &magic, BRW_MSIZE);
		return;
	}

	if (pattern == LST_BRW_CHECK_FULL) {
		for (i = 0; i < len; i += BRW_MSIZE)
			memcpy(addr + i, &magic, BRW_MSIZE);
		return;
	}
	LBUG();
}

/**
 * brw_check_page() - Verify the check pattern in one page of a bulk buffer.
 * @pg: Page to verify.
 * @off: Offset of the data in @pg.
 * @len: Number of bytes of data in @pg.
 * @pos: Offset of the data in the bulk buffer.
 * @bklen: Total number of bytes in the bulk buffer.
 * @pattern: One of the LST_BRW_CHECK_* patterns.
 * @magic: Value to expect.
 * @strided: The session has the LST_FEAT_BULK_STRIDE feature.
 *
 * The markers are at the positions that brw_fill_page() writes.
 *
 * Return: 0 if the data is good, 1 if the data is bad.
 */
static int
brw_check_page(struct page *pg, int off, int len, int pos, int bklen,
	       int pattern, __u64 magic, bool strided)
{
	char *addr = page_address(pg) + off;
	__u64 data = 0; /* make compiler happy */
	int i;

	LASSERT(addr != NULL);
	LASSERT(off % BRW_MSIZE == 0 && len % BRW_MSIZE == 0);

	if (pattern == LST_BRW_CHECK_NONE || pattern == LST_BRW_CHECK_DISCARD)
		return 0;

	if (pattern == LST_BRW_CHECK_SIMPLE) {
		if (!strided) {
			i = 0;
			data = *(__u64 *)addr;
			if (data != magic)
				goto bad_data;

			if (len > BRW_MSIZE) {
				i = len - BRW_MSIZE;
				data = *(__u64 *)(addr + i);
				if (data != magic)
					goto bad_data;
			}
			return 0;
		}

		for (i = roundup(pos, BRW_STRIDE) - pos; i < len;
		     i += BRW_STRIDE) {
			data = *(__u64 *)(addr + i);
			if (data != magic)
				goto bad_data;
		}

		i = bklen - BRW_MSIZE - pos;
		if (i >= 0 && i < len) {
			data = *(__u64 *)(addr + i);
			if (data != magic)
				goto bad_data;
		}
		return 0;
	}

	if (pattern == LST_BRW_CHECK_FULL) {
		for (i = 0; i < len; i += BRW_MSIZE) {
			data = *(__u64 *)(addr + i);
			if (data != magic)
				goto bad_data;
		}
		return 0;
	}

	LBUG();

bad_data:
	CERROR("Bad data in page %p at bulk offset %d: %#llx, %#llx expected\n",
	       pg, pos + i, data, magic);
	return 1;
}

static void
brw_fill_bulk(struct srpc_bulk *bk, int pattern, __u64 magic, bool strided)
{
	struct page *pg;
	int pos = 0;
	int i;

	for (i = 0; i < bk->bk_niov; i++) {
		int off;
		int len;

		pg = bk->bk_iovs[i].bv_page;
		off = bk->bk_iovs[i].bv_offset;
		len = bk->bk_iovs[i].bv_len;
		brw_fill_page(pg, off, len, pos, bk->bk_len, pattern, magic,
			      strided);
		pos += len;
	}
}

static int
brw_check_bulk(struct srpc_bulk *bk, int pattern, __u64 magic, bool strided)
{
	struct page *pg;
	int pos = 0;
	int i;

	for (i = 0; i < bk->bk_niov; i++) {
		int off;
		int len;

		pg = bk->bk_iovs[i].bv_page;
		off = bk->bk_iovs[i].bv_offset;
		len = bk->bk_iovs[i].bv_len;
		if (brw_check_page(pg, off, len, pos, bk->bk_len, pattern,
				   magic, strided) != 0) {
			CERROR("Bulk page %p (%d/%d) is corrupted!\n",
			       pg, i, bk->bk_niov);
			return 1;
		}
		pos += len;
	}

	return 0;
}

static int
brw_client_prep_rpc(struct sfw_test_unit *tsu, struct lnet_processid *dest,
		    struct srpc_client_rpc **rpcpp)
{
	struct srpc_bulk *bulk = tsu->tsu_private;
	struct sfw_test_instance *tsi = tsu->tsu_instance;
	struct sfw_session *sn = tsi->tsi_batch->bat_session;
	struct srpc_client_rpc *rpc;
	struct srpc_brw_reqst *req;
	bool strided;
	int flags;
	int npg;
	int len;
	int off;
	int opc;
	int rc;

	LASSERT(sn != NULL);
	LASSERT(bulk != NULL);

	if ((sn->sn_features & LST_FEAT_BULK_LEN) == 0) {
		struct test_bulk_req *breq = &tsi->tsi_u.bulk_v0;

		opc   = breq->blk_opc;
		flags = breq->blk_flags;
		len   = breq->blk_npg * PAGE_SIZE;
		off   = 0;

	} else {
		struct test_bulk_req_v1 *breq = &tsi->tsi_u.bulk_v1;

		/* I should never get this step if it's unknown feature
		 * because make_session will reject unknown feature
		 */
		LASSERT((sn->sn_features & ~LST_FEATS_MASK) == 0);

		opc   = breq->blk_opc;
		flags = breq->blk_flags;
		len   = breq->blk_len;
		off   = breq->blk_offset;
	}
	npg   = (off + len + PAGE_SIZE - 1) >> PAGE_SHIFT;

	rc = sfw_create_test_rpc(tsu, dest, sn->sn_features, npg, len, &rpc);
	if (rc != 0)
		return rc;

	unsafe_memcpy(&rpc->crpc_bulk, bulk,
		      offsetof(struct srpc_bulk, bk_iovs[npg]),
		      FLEXIBLE_OBJECT);
	strided = (sn->sn_features & LST_FEAT_BULK_STRIDE) != 0;
	if (opc == LST_BRW_WRITE)
		brw_fill_bulk(&rpc->crpc_bulk, flags, BRW_MAGIC, strided);
	else
		brw_fill_bulk(&rpc->crpc_bulk, flags, BRW_POISON, strided);

	req = &rpc->crpc_reqstmsg.msg_body.brw_reqst;
	req->brw_flags = flags;
	req->brw_rw    = opc;
	req->brw_len   = len;

	*rpcpp = rpc;
	return 0;
}

static void
brw_client_done_rpc(struct sfw_test_unit *tsu, struct srpc_client_rpc *rpc)
{
	__u64 magic = BRW_MAGIC;
	struct sfw_test_instance *tsi = tsu->tsu_instance;
	struct sfw_session *sn = tsi->tsi_batch->bat_session;
	struct srpc_msg *msg = &rpc->crpc_replymsg;
	struct srpc_brw_reply *reply = &msg->msg_body.brw_reply;
	struct srpc_brw_reqst *reqst = &rpc->crpc_reqstmsg.msg_body.brw_reqst;
	bool strided;

	LASSERT(sn != NULL);

	if (rpc->crpc_status != 0) {
		CERROR("BRW RPC to %s failed with %d\n",
		       libcfs_idstr(&rpc->crpc_dest), rpc->crpc_status);
		if (!tsi->tsi_stopping) /* rpc could have been aborted */
			atomic_inc(&sn->sn_brw_errors);
		return;
	}

	if (msg->msg_magic != SRPC_MSG_MAGIC) {
		__swab64s(&magic);
		__swab32s(&reply->brw_status);
	}

	CDEBUG(reply->brw_status ? D_WARNING : D_NET,
	       "BRW RPC to %s finished with brw_status: %d\n",
	       libcfs_idstr(&rpc->crpc_dest), reply->brw_status);

	if (reply->brw_status != 0) {
		atomic_inc(&sn->sn_brw_errors);
		rpc->crpc_status = -(int)reply->brw_status;
		return;
	}

	if (reqst->brw_rw == LST_BRW_WRITE)
		return;

	strided = (sn->sn_features & LST_FEAT_BULK_STRIDE) != 0;
	if (brw_check_bulk(&rpc->crpc_bulk, reqst->brw_flags, magic,
			   strided) != 0) {
		CERROR("Bulk data from %s is corrupted!\n",
		       libcfs_idstr(&rpc->crpc_dest));
		atomic_inc(&sn->sn_brw_errors);
		rpc->crpc_status = -EBADMSG;
	}
}

static void
brw_server_rpc_done(struct srpc_server_rpc *rpc)
{
	struct srpc_bulk *blk = rpc->srpc_bulk;

	if (blk == NULL)
		return;

	if (rpc->srpc_status != 0)
		CERROR("Bulk transfer %s %s has failed: %d\n",
		       blk->bk_sink ? "from" : "to",
		       libcfs_idstr(&rpc->srpc_peer), rpc->srpc_status);
	else
		CDEBUG(D_NET, "Transferred %d pages bulk data %s %s\n",
		       blk->bk_niov, blk->bk_sink ? "from" : "to",
		       libcfs_idstr(&rpc->srpc_peer));
}

static int
brw_bulk_ready(struct srpc_server_rpc *rpc, int status)
{
	__u64 magic = BRW_MAGIC;
	struct srpc_brw_reply *reply = &rpc->srpc_replymsg.msg_body.brw_reply;
	struct srpc_brw_reqst *reqst;
	struct srpc_msg *reqstmsg;
	bool strided;

	LASSERT(rpc->srpc_bulk != NULL);
	LASSERT(rpc->srpc_reqstbuf != NULL);

	reqstmsg = &rpc->srpc_reqstbuf->buf_msg;
	reqst = &reqstmsg->msg_body.brw_reqst;

	if (status != 0) {
		CERROR("BRW bulk %s failed for RPC from %s: %d\n",
		       reqst->brw_rw == LST_BRW_READ ? "READ" : "WRITE",
		       libcfs_idstr(&rpc->srpc_peer), status);
		return -EIO;
	}

	if (reqst->brw_rw == LST_BRW_READ)
		return 0;

	if (reqstmsg->msg_magic != SRPC_MSG_MAGIC)
		__swab64s(&magic);

	strided = (reqstmsg->msg_ses_feats & LST_FEAT_BULK_STRIDE) != 0;
	if (brw_check_bulk(rpc->srpc_bulk, reqst->brw_flags, magic,
			   strided) != 0) {
		CERROR("Bulk data from %s is corrupted!\n",
		       libcfs_idstr(&rpc->srpc_peer));
		reply->brw_status = EBADMSG;
	}

	return 0;
}

static int
brw_server_handle(struct srpc_server_rpc *rpc)
{
	struct srpc_service *sv = rpc->srpc_scd->scd_svc;
	struct srpc_msg *replymsg = &rpc->srpc_replymsg;
	struct srpc_msg *reqstmsg = &rpc->srpc_reqstbuf->buf_msg;
	struct srpc_brw_reply *reply = &replymsg->msg_body.brw_reply;
	struct srpc_brw_reqst *reqst = &reqstmsg->msg_body.brw_reqst;
	bool strided;

	LASSERT(sv->sv_id == SRPC_SERVICE_BRW);

	if (reqstmsg->msg_magic != SRPC_MSG_MAGIC) {
		LASSERT(reqstmsg->msg_magic == __swab32(SRPC_MSG_MAGIC));

		__swab32s(&reqst->brw_rw);
		__swab32s(&reqst->brw_len);
		__swab32s(&reqst->brw_flags);
		__swab64s(&reqst->brw_rpyid);
		__swab64s(&reqst->brw_bulkid);
	}
	LASSERT(reqstmsg->msg_type == (__u32)srpc_service2request(sv->sv_id));

	reply->brw_status = 0;
	rpc->srpc_done = brw_server_rpc_done;

	if ((reqst->brw_rw != LST_BRW_READ && reqst->brw_rw != LST_BRW_WRITE) ||
	    (reqst->brw_flags != LST_BRW_CHECK_NONE &&
	     reqst->brw_flags != LST_BRW_CHECK_FULL &&
	     reqst->brw_flags != LST_BRW_CHECK_SIMPLE &&
	     reqst->brw_flags != LST_BRW_CHECK_DISCARD)) {
		reply->brw_status = EINVAL;
		return 0;
	}

	if ((reqstmsg->msg_ses_feats & ~LST_FEATS_MASK) != 0) {
		replymsg->msg_ses_feats = LST_FEATS_MASK;
		reply->brw_status = EPROTO;
		return 0;
	}

	if ((reqstmsg->msg_ses_feats & LST_FEAT_BULK_LEN) == 0) {
		/* compat with old version */
		if ((reqst->brw_len & ~PAGE_MASK) != 0) {
			reply->brw_status = EINVAL;
			return 0;
		}
	}

	replymsg->msg_ses_feats = reqstmsg->msg_ses_feats;

	if (reqst->brw_len == 0 || reqst->brw_len > LNET_MTU) {
		reply->brw_status = EINVAL;
		return 0;
	}

	srpc_init_bulk(rpc->srpc_bulk, 0, reqst->brw_len,
		       reqst->brw_rw == LST_BRW_WRITE);
	rpc->srpc_bulk->bk_discard =
		(reqst->brw_flags == LST_BRW_CHECK_DISCARD);

	strided = (reqstmsg->msg_ses_feats & LST_FEAT_BULK_STRIDE) != 0;
	if (reqst->brw_rw == LST_BRW_READ)
		brw_fill_bulk(rpc->srpc_bulk, reqst->brw_flags, BRW_MAGIC,
			      strided);
	else
		brw_fill_bulk(rpc->srpc_bulk, reqst->brw_flags, BRW_POISON,
			      strided);

	return 0;
}

static int
brw_srpc_init(struct srpc_server_rpc *rpc, int cpt)
{
	/* just alloc a maximal size - actual values will be adjusted later */
	rpc->srpc_bulk = srpc_alloc_bulk(cpt, LNET_MTU);
	if (rpc->srpc_bulk == NULL)
		return -ENOMEM;

	srpc_init_bulk(rpc->srpc_bulk, 0, 0, 0);

	return 0;
}

static void
brw_srpc_fini(struct srpc_server_rpc *rpc)
{
	srpc_free_bulk(rpc->srpc_bulk);
	rpc->srpc_bulk = NULL;
}

struct sfw_test_client_ops brw_test_client = {
	.tso_init       = brw_client_init,
	.tso_fini       = brw_client_fini,
	.tso_prep_rpc   = brw_client_prep_rpc,
	.tso_done_rpc   = brw_client_done_rpc,
};

struct srpc_service brw_test_service = {
	.sv_id         = SRPC_SERVICE_BRW,
	.sv_name       = "brw_test",
	.sv_handler    = brw_server_handle,
	.sv_bulk_ready = brw_bulk_ready,

	.sv_srpc_init  = brw_srpc_init,
	.sv_srpc_fini  = brw_srpc_fini,
};

void brw_init_test_service(void)
{
	unsigned long cache_size = totalram_pages() >> 4;

	/* brw prealloc cache should don't eat more than half memory */
	cache_size /= ((LNET_MTU >> PAGE_SHIFT) + 1);

	brw_test_service.sv_wi_total   = brw_srv_workitems;

	if (brw_test_service.sv_wi_total > cache_size)
		brw_test_service.sv_wi_total = cache_size;
}
