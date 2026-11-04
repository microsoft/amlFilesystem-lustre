/* SPDX-License-Identifier: LGPL-2.1+ */

/*
 * Copyright (c) 2026, The Lustre Collective.
 */

/*
 * The LFU Object Stream writer, liblustreapi_lfu.c.  Internal: what a
 * producer in this library uses to lay chunks out in a caller's buffer.
 * The reader is public, in lustreapi.h.
 */
#ifndef _LUSTREAPI_LFU_H_
#define _LUSTREAPI_LFU_H_

#include <stddef.h>
#include <linux/types.h>
#include <linux/lustre/lustre_user.h>

struct llapi_scan_inode;

/* the most objects one chunk names in its skipped list */
#define LFU_BUILD_SKIP_MAX	64

struct lfu_skip {
	struct lu_fid	ls_fid;
	int		ls_error;
};

/* One chunk being written.  Records grow from the front, EAs from the back. */
struct lfu_builder {
	__u32		*lb_table;	/* the 8-byte segment table */
	__u64		*lb_seg;	/* the segment, after it */
	__u32		 lb_words;	/* segment size */
	__u32		 lb_front;	/* first free word after the records */
	__u32		 lb_back;	/* first word used at the back */
	__u32		 lb_count;	/* records */
	__u32		 lb_nskip;	/* entries in lb_skip */
	struct lfu_skip	 lb_skip[LFU_BUILD_SKIP_MAX];
};

void lfu_build_begin(struct lfu_builder *b, void *buf, size_t bytes);
int lfu_build_inode(struct lfu_builder *b, const struct llapi_scan_inode *li);
int lfu_build_skipped(struct lfu_builder *b, const struct lu_fid *fid,
		      int error);
void lfu_build_finish(struct lfu_builder *b, __u32 scanned, __u32 errors,
		      const void *cursor, __u32 cursor_len);
/* the words a record takes in a chunk, its EAs included */
__u32 lfu_build_inode_words(const struct llapi_scan_inode *li);
/* and the bytes a chunk holding only that record needs */
size_t lfu_build_inode_bytes(const struct llapi_scan_inode *li);

#endif /* _LUSTREAPI_LFU_H_ */
