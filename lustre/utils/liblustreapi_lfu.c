// SPDX-License-Identifier: LGPL-2.1+

/*
 * Copyright (c) 2026, The Lustre Collective.
 */

/*
 * The LFU Object Stream codec: LfuChunk messages of lfu.capnp, written
 * and read without a Cap'n Proto library.
 *
 * Only the subset the Lustre producers send is handled: an LfuChunk whose
 * union is empty or a list of LfuInode, each with its LfuEa, and the
 * chunk's skipped list.  The writer lays a chunk out as the schema's notes
 * suggest, so that no batch has to be staged: the cursor at a fixed place
 * near the front, the records after it, each record's EA values from the
 * back of the slot, and the list headers last, once the counts are known.
 * What is left between the two ends is zero, which a Cap'n Proto reader
 * accepts as words that nothing points at.
 *
 * The reader trusts nothing in the bytes: a chunk may come from another
 * process or another node.  Every pointer is checked against the segment
 * before it is followed, and a struct shorter than this schema's reads as
 * zero past its end, as Cap'n Proto requires of a reader.
 *
 * This file uses nothing else of liblustreapi, so that a test can build it
 * on its own.
 *
 * Author: Artem Blagodarenko <ablagodarenko@thelustrecollective.com>
 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <asm/byteorder.h>
#include <linux/lustre/lustre_lfu_capnp.h>
#include <linux/lustre/lustre_user.h>

#include <lustre/lustreapi.h>

#include "lustreapi_lfu.h"

/* a word is 8 bytes; a segment is an array of them, little-endian */
#define LFU_WORD	8U

static inline __u32 lfu_words(size_t bytes)
{
	return (bytes + LFU_WORD - 1) / LFU_WORD;
}

static inline __u64 lfu_word_get(const __u64 *seg, __u32 w)
{
	return __le64_to_cpu(seg[w]);
}

static inline void lfu_word_put(__u64 *seg, __u32 w, __u64 v)
{
	seg[w] = __cpu_to_le64(v);
}

/*
 * Fields of a struct's data section, by byte offset.  The reader passes
 * the section's length, and a field at or past it reads as zero: the
 * struct was written by an older schema that did not have it.
 */
static __u64 lfu_get64(const __u64 *seg, __u32 start, __u32 dwords,
		       __u32 off)
{
	__u64 v;

	if (off + sizeof(v) > dwords * LFU_WORD)
		return 0;
	memcpy(&v, (const char *)(seg + start) + off, sizeof(v));
	return __le64_to_cpu(v);
}

static __u32 lfu_get32(const __u64 *seg, __u32 start, __u32 dwords,
		       __u32 off)
{
	__u32 v;

	if (off + sizeof(v) > dwords * LFU_WORD)
		return 0;
	memcpy(&v, (const char *)(seg + start) + off, sizeof(v));
	return __le32_to_cpu(v);
}

static __u16 lfu_get16(const __u64 *seg, __u32 start, __u32 dwords,
		       __u32 off)
{
	__u16 v;

	if (off + sizeof(v) > dwords * LFU_WORD)
		return 0;
	memcpy(&v, (const char *)(seg + start) + off, sizeof(v));
	return __le16_to_cpu(v);
}

static void lfu_put64(__u64 *seg, __u32 start, __u32 off, __u64 v)
{
	v = __cpu_to_le64(v);
	memcpy((char *)(seg + start) + off, &v, sizeof(v));
}

static void lfu_put32(__u64 *seg, __u32 start, __u32 off, __u32 v)
{
	v = __cpu_to_le32(v);
	memcpy((char *)(seg + start) + off, &v, sizeof(v));
}

static void lfu_put16(__u64 *seg, __u32 start, __u32 off, __u16 v)
{
	v = __cpu_to_le16(v);
	memcpy((char *)(seg + start) + off, &v, sizeof(v));
}

/*
 * Pointers.  The low two bits say what a pointer is; the next 30 are the
 * signed distance in words from the end of the pointer to what it points
 * at.  A struct pointer then gives the data and pointer section sizes, a
 * list pointer the element size and count.
 */
#define LFU_PTR_STRUCT		0
#define LFU_PTR_LIST		1
#define LFU_PTR_FAR		2

#define LFU_LIST_BYTE		2	/* List(UInt8), so Data */
#define LFU_LIST_COMPOSITE	7	/* a list of structs, with a tag */

static __u64 lfu_struct_ptr(__u32 ptr_w, __u32 target_w, __u16 dwords,
			    __u16 ptrs)
{
	__s32 off = (__s32)target_w - (__s32)ptr_w - 1;

	return ((__u64)((__u32)off << 2) | LFU_PTR_STRUCT) |
	       ((__u64)dwords << 32) | ((__u64)ptrs << 48);
}

static __u64 lfu_list_ptr(__u32 ptr_w, __u32 target_w, __u32 elsize,
			  __u32 count)
{
	__s32 off = (__s32)target_w - (__s32)ptr_w - 1;

	return ((__u64)((__u32)off << 2) | LFU_PTR_LIST) |
	       ((__u64)elsize << 32) | ((__u64)count << 35);
}

/* a composite list's tag: a struct pointer whose offset is the count */
static __u64 lfu_list_tag(__u32 count, __u16 dwords, __u16 ptrs)
{
	return ((__u64)count << 2) | ((__u64)dwords << 32) |
	       ((__u64)ptrs << 48);
}

/*
 * Follow the pointer at word @ptr_w.  Return 1 for a null pointer, 0 with
 * *@target set, or -EINVAL for a pointer this reader does not follow: a
 * far pointer, which a single-segment chunk never needs, or one that
 * leaves the segment.
 */
static int lfu_ptr_follow(const __u64 *seg, __u32 words, __u32 ptr_w,
			  __u64 *ptr, __u32 *target)
{
	__s64 t;
	__s32 off;

	*ptr = lfu_word_get(seg, ptr_w);
	if (*ptr == 0)
		return 1;
	if ((*ptr & 3) >= LFU_PTR_FAR)
		return -EINVAL;

	off = (__s32)(__u32)*ptr >> 2;
	t = (__s64)ptr_w + 1 + off;
	/* at the very end is legal for something of no size: callers check */
	if (t < 0 || t > words)
		return -EINVAL;
	*target = t;
	return 0;
}

static int lfu_struct_get(const __u64 *seg, __u32 words, __u32 ptr_w,
			  __u32 *start, __u32 *dwords, __u32 *ptrs)
{
	__u64 ptr;
	int rc;

	rc = lfu_ptr_follow(seg, words, ptr_w, &ptr, start);
	if (rc != 0)
		return rc;
	if ((ptr & 3) != LFU_PTR_STRUCT)
		return -EINVAL;
	*dwords = (ptr >> 32) & 0xffff;
	*ptrs = ptr >> 48;
	if ((__u64)*start + *dwords + *ptrs > words)
		return -EINVAL;
	return 0;
}

/* Data, which Cap'n Proto encodes as List(UInt8) */
static int lfu_data_get(const __u64 *seg, __u32 words, __u32 ptr_w,
			struct llapi_scan_data *ld)
{
	__u32 start;
	__u64 ptr;
	__u32 len;
	int rc;

	ld->ld_buf = NULL;
	ld->ld_len = 0;
	rc = lfu_ptr_follow(seg, words, ptr_w, &ptr, &start);
	if (rc != 0)
		return rc;
	if ((ptr & 3) != LFU_PTR_LIST || ((ptr >> 32) & 7) != LFU_LIST_BYTE)
		return -EINVAL;
	len = ptr >> 35;
	if ((__u64)start + lfu_words(len) > words)
		return -EINVAL;
	ld->ld_buf = seg + start;
	ld->ld_len = len;
	return 0;
}

/*
 * A list of structs.  *@first is the first element's word, and each
 * element is *@dwords + *@ptrs words: a newer writer's elements may be
 * larger than this schema's, and are read through the tag's sizes.
 */
static int lfu_struct_list_get(const __u64 *seg, __u32 words, __u32 ptr_w,
			       __u32 *first, __u32 *count, __u32 *dwords,
			       __u32 *ptrs)
{
	__u32 start;
	__u32 total;
	__u64 tag;
	__u64 ptr;
	int rc;

	*count = 0;
	rc = lfu_ptr_follow(seg, words, ptr_w, &ptr, &start);
	if (rc != 0)
		return rc;
	if ((ptr & 3) != LFU_PTR_LIST ||
	    ((ptr >> 32) & 7) != LFU_LIST_COMPOSITE)
		return -EINVAL;

	total = ptr >> 35;	/* words, the tag not counted */
	if ((__u64)start + 1 + total > words)
		return -EINVAL;
	tag = lfu_word_get(seg, start);
	if ((tag & 3) != LFU_PTR_STRUCT)
		return -EINVAL;
	*count = (__u32)tag >> 2;
	*dwords = (tag >> 32) & 0xffff;
	*ptrs = tag >> 48;
	if ((__u64)*count * (*dwords + *ptrs) > total)
		return -EINVAL;
	/*
	 * Elements of no size would let a few bytes claim a billion of them,
	 * and no struct this reader decodes is empty.
	 */
	if (*count != 0 && *dwords + *ptrs == 0)
		return -EINVAL;
	*first = start + 1;
	return 0;
}

/* What the reader keeps of a chunk, in llapi_scan_chunk.lc_private. */
struct lfu_chunk_priv {
	const __u64	*cp_seg;
	__u32		 cp_words;
	__u32		 cp_first;	/* of the inodes list */
	__u32		 cp_dwords;
	__u32		 cp_ptrs;
	__u32		 cp_skip_first;	/* of the skipped list */
	__u32		 cp_skip_dwords;
	__u32		 cp_skip_ptrs;
};

static_assert(sizeof(struct lfu_chunk_priv) <=
	      sizeof(((struct llapi_scan_chunk *)0)->lc_private),
	      "the reader's state fits the room the chunk keeps for it");

/* copied in and out, so lc_private's type is never aliased */
static void lfu_priv_get(const struct llapi_scan_chunk *ch,
			 struct lfu_chunk_priv *cp)
{
	memcpy(cp, ch->lc_private, sizeof(*cp));
}

/**
 * llapi_scan_chunk_parse() - read the header of one chunk of a bulk
 * @buf:	the chunk's first slot, 8-byte aligned
 * @len:	bytes valid from @buf to the end of the bulk
 * @chunk_size:	the slot size of the stream
 * @ch:		filled in
 *
 * Checks that @buf holds one single-segment message that ends on a slot
 * boundary within @len, and that the pointers of its root are inside it.
 * The records are checked as they are read, by llapi_scan_chunk_inode().
 *
 * Return: 0, or -EINVAL for bytes that are not a chunk of this format.
 */
int llapi_scan_chunk_parse(const void *buf, size_t len, __u32 chunk_size,
			   struct llapi_scan_chunk *ch)
{
	struct lfu_chunk_priv cp = { 0 };
	const __u32 *table = buf;
	const __u64 *seg;
	__u32 words;
	__u32 root;
	__u32 dwords;
	__u32 ptrs;
	__u64 bytes;
	int rc;

	memset(ch, 0, sizeof(*ch));
	if (buf == NULL || ((unsigned long)buf & (LFU_WORD - 1)) != 0 ||
	    chunk_size < LFU_CHUNK_SIZE_MIN ||
	    chunk_size > LFU_CHUNK_SIZE_MAX ||
	    (chunk_size & (chunk_size - 1)) != 0 || len < chunk_size)
		return -EINVAL;

	/* one segment: a count of 0 and the segment's size in words */
	if (__le32_to_cpu(table[0]) != 0)
		return -EINVAL;
	words = __le32_to_cpu(table[1]);
	bytes = ((__u64)words + 1) * LFU_WORD;
	if (words == 0 || bytes % chunk_size != 0 || bytes > len)
		return -EINVAL;

	seg = (const __u64 *)buf + 1;
	rc = lfu_struct_get(seg, words, 0, &root, &dwords, &ptrs);
	if (rc != 0)
		return -EINVAL;

	ch->lc_slots = bytes / chunk_size;
	ch->lc_scanned = lfu_get32(seg, root, dwords, LFU_CHUNK_SCANNED);
	ch->lc_errors = lfu_get32(seg, root, dwords, LFU_CHUNK_ERRORS);
	ch->lc_which = lfu_get16(seg, root, dwords, LFU_CHUNK_WHICH);
	cp.cp_seg = seg;
	cp.cp_words = words;

	if (ptrs > LFU_CHUNK_P_CURSOR &&
	    lfu_data_get(seg, words, root + dwords + LFU_CHUNK_P_CURSOR,
			 &ch->lc_cursor) < 0)
		return -EINVAL;

	if (ptrs > LFU_CHUNK_P_SKIPPED &&
	    lfu_struct_list_get(seg, words,
				root + dwords + LFU_CHUNK_P_SKIPPED,
				&cp.cp_skip_first, &ch->lc_skipped,
				&cp.cp_skip_dwords, &cp.cp_skip_ptrs) < 0)
		return -EINVAL;

	/* the union's other members are not this reader's to decode */
	if (ch->lc_which == LFU_CHUNK_INODES && ptrs > LFU_CHUNK_P_UNION &&
	    lfu_struct_list_get(seg, words, root + dwords + LFU_CHUNK_P_UNION,
				&cp.cp_first, &ch->lc_count,
				&cp.cp_dwords, &cp.cp_ptrs) < 0)
		return -EINVAL;

	memcpy(ch->lc_private, &cp, sizeof(cp));
	return 0;
}

static_assert(LFU_EA_DATA_MAX <= LLAPI_SCAN_EA_SLOTS,
	      "struct llapi_scan_inode has a slot for every Data field of LfuEa");

/**
 * llapi_scan_chunk_inode() - read one record of an inodes chunk
 * @ch:		from llapi_scan_chunk_parse()
 * @idx:	below ch->lc_count
 * @li:		filled in; its EA pointers point into the chunk
 *
 * Return: 0, -ERANGE for an @idx past the list, or -EINVAL for a record
 * whose EAs point outside the chunk.
 */
int llapi_scan_chunk_inode(const struct llapi_scan_chunk *ch, __u32 idx,
			   struct llapi_scan_inode *li)
{
	struct lfu_chunk_priv cp;
	const __u64 *seg;
	__u32 words;
	__u32 d;
	__u32 w;
	__u32 ea;
	__u32 ea_dwords;
	__u32 ea_ptrs;
	unsigned int i;
	int rc;

	memset(li, 0, sizeof(*li));
	if (ch->lc_which != LFU_CHUNK_INODES || idx >= ch->lc_count)
		return -ERANGE;

	lfu_priv_get(ch, &cp);
	seg = cp.cp_seg;
	words = cp.cp_words;
	d = cp.cp_dwords;
	w = cp.cp_first + idx * (cp.cp_dwords + cp.cp_ptrs);
	li->li_fid.f_seq = lfu_get64(seg, w, d, LFU_INODE_FID_SEQ);
	li->li_fid.f_oid = lfu_get32(seg, w, d, LFU_INODE_FID_OID);
	li->li_fid.f_ver = lfu_get32(seg, w, d, LFU_INODE_FID_VER);
	li->li_size = lfu_get64(seg, w, d, LFU_INODE_SIZE);
	li->li_blocks = lfu_get64(seg, w, d, LFU_INODE_BLOCKS);
	li->li_atime = lfu_get64(seg, w, d, LFU_INODE_ATIME);
	li->li_mtime = lfu_get64(seg, w, d, LFU_INODE_MTIME);
	li->li_ctime = lfu_get64(seg, w, d, LFU_INODE_CTIME);
	li->li_btime = lfu_get64(seg, w, d, LFU_INODE_BTIME);
	li->li_mode = lfu_get32(seg, w, d, LFU_INODE_MODE);
	li->li_uid = lfu_get32(seg, w, d, LFU_INODE_UID);
	li->li_gid = lfu_get32(seg, w, d, LFU_INODE_GID);
	li->li_projid = lfu_get32(seg, w, d, LFU_INODE_PROJID);
	li->li_nlink = lfu_get32(seg, w, d, LFU_INODE_NLINK);
	li->li_flags = lfu_get32(seg, w, d, LFU_INODE_FLAGS);
	li->li_rdev = lfu_get32(seg, w, d, LFU_INODE_RDEV);
	li->li_valid = lfu_get32(seg, w, d, LFU_INODE_VALID);
	li->li_ino = lfu_get64(seg, w, d, LFU_INODE_INO);
	li->li_atime_nsec = lfu_get32(seg, w, d, LFU_INODE_ATIME_NSEC);
	li->li_mtime_nsec = lfu_get32(seg, w, d, LFU_INODE_MTIME_NSEC);
	li->li_ctime_nsec = lfu_get32(seg, w, d, LFU_INODE_CTIME_NSEC);
	li->li_btime_nsec = lfu_get32(seg, w, d, LFU_INODE_BTIME_NSEC);

	if (cp.cp_ptrs <= LFU_INODE_P_EA)
		return 0;
	rc = lfu_struct_get(seg, words, w + d + LFU_INODE_P_EA, &ea,
			    &ea_dwords, &ea_ptrs);
	if (rc > 0)
		return 0;
	if (rc < 0)
		return rc;

	for (i = 0; i < LFU_EA_DATA_MAX && i < ea_ptrs; i++) {
		if (i == LFU_EA_XATTRS)
			continue;
		rc = lfu_data_get(seg, words, ea + ea_dwords + i,
				  &li->li_ea[i]);
		if (rc < 0)
			return rc;
	}
	return 0;
}

/**
 * llapi_scan_chunk_skipped() - read one entry of a chunk's skipped list
 * @ch:		from llapi_scan_chunk_parse()
 * @idx:	below ch->lc_skipped
 * @fid:	the object
 * @error:	a negative errno, why it was not sent
 *
 * Return: 0, or -ERANGE for an @idx past the list.
 */
int llapi_scan_chunk_skipped(const struct llapi_scan_chunk *ch, __u32 idx,
			     struct lu_fid *fid, int *error)
{
	struct lfu_chunk_priv cp;
	__u32 d;
	__u32 w;

	if (idx >= ch->lc_skipped)
		return -ERANGE;

	lfu_priv_get(ch, &cp);
	d = cp.cp_skip_dwords;
	w = cp.cp_skip_first + idx * (cp.cp_skip_dwords + cp.cp_skip_ptrs);
	fid->f_seq = lfu_get64(cp.cp_seg, w, d, LFU_SKIPPED_FID_SEQ);
	fid->f_oid = lfu_get32(cp.cp_seg, w, d, LFU_SKIPPED_FID_OID);
	fid->f_ver = lfu_get32(cp.cp_seg, w, d, LFU_SKIPPED_FID_VER);
	*error = (__s32)lfu_get32(cp.cp_seg, w, d, LFU_SKIPPED_ERROR);
	return 0;
}

/*
 * The writer.  A chunk's words, from the front:
 *
 *   0       root pointer to the LfuChunk at word 1
 *   1-2     LfuChunk data: scanned, errors, the union's discriminant
 *   3-5     LfuChunk pointers: cursor, union, skipped
 *   6-10    the cursor's bytes, room for LLAPI_SCAN_CURSOR_MAX
 *   11      the inodes list's tag
 *   12...   the LfuInode records
 *   ...     the skipped list, placed by lfu_build_finish()
 *   ...     zero
 *   ...     each record's LfuEa and its values, from the back
 */
#define LFU_B_ROOT	1
#define LFU_B_PTRS	(LFU_B_ROOT + LFU_CHUNK_DATA_WORDS)
#define LFU_B_CURSOR	(LFU_B_PTRS + LFU_CHUNK_PTRS)
#define LFU_B_TAG	(LFU_B_CURSOR + LLAPI_SCAN_CURSOR_MAX / LFU_WORD)
#define LFU_B_REC0	(LFU_B_TAG + 1)
#define LFU_REC_WORDS	(LFU_INODE_DATA_WORDS + LFU_INODE_PTRS)

static_assert(LLAPI_SCAN_CURSOR_MAX % LFU_WORD == 0,
	      "the cursor is a whole number of words");
static_assert(LFU_INODE_BTIME_NSEC + 4 == LFU_INODE_DATA_WORDS * LFU_WORD,
	      "btimeNsec is the last field of LfuInode's data");

/* what the skipped list costs, its tag included */
static __u32 lfu_skip_words(__u32 nskip)
{
	return nskip == 0 ? 0 : 1 + nskip * LFU_SKIPPED_DATA_WORDS;
}

/* The words a record's EAs take: the LfuEa, cut short, and its values. */
static __u32 lfu_ea_words(const struct llapi_scan_inode *li, __u32 *nptrs)
{
	__u32 words = 0;
	unsigned int i;

	*nptrs = 0;
	for (i = 0; i < LFU_EA_DATA_MAX; i++) {
		if (i == LFU_EA_XATTRS || li->li_ea[i].ld_buf == NULL)
			continue;
		*nptrs = i + 1;
		words += lfu_words(li->li_ea[i].ld_len);
	}
	return words + *nptrs;
}

__u32 lfu_build_inode_words(const struct llapi_scan_inode *li)
{
	__u32 nptrs;

	return LFU_REC_WORDS + lfu_ea_words(li, &nptrs);
}

/* What a chunk of one record needs, its header and all, in bytes. */
size_t lfu_build_inode_bytes(const struct llapi_scan_inode *li)
{
	return ((size_t)LFU_B_REC0 + 1 + lfu_build_inode_words(li)) *
	       LFU_WORD;
}

/**
 * lfu_build_begin() - start a chunk
 * @b:		the builder
 * @buf:	the chunk's first slot, 8-byte aligned
 * @bytes:	slots times the slot size: what the chunk will span
 */
void lfu_build_begin(struct lfu_builder *b, void *buf, size_t bytes)
{
	memset(b, 0, sizeof(*b));
	b->lb_table = buf;
	b->lb_seg = (__u64 *)buf + 1;
	b->lb_words = bytes / LFU_WORD - 1;
	b->lb_front = LFU_B_REC0;
	b->lb_back = b->lb_words;
}

/* the words still free, once the skipped list has its room */
static __u32 lfu_build_room(const struct lfu_builder *b, __u32 nskip)
{
	__u32 need = lfu_skip_words(nskip);
	__u32 gap = b->lb_back - b->lb_front;

	return gap > need ? gap - need : 0;
}

/**
 * lfu_build_inode() - append a record to the chunk
 * @b:		the builder
 * @li:		the record; its EA values are copied
 *
 * Return: 0, or -ENOSPC if the record does not fit what is left of the
 * chunk, which is then unchanged.
 */
int lfu_build_inode(struct lfu_builder *b, const struct llapi_scan_inode *li)
{
	__u64 *seg = b->lb_seg;
	__u32 nptrs;
	__u32 ea_words = lfu_ea_words(li, &nptrs);
	__u32 w = b->lb_front;
	__u32 ea;
	__u32 d;
	unsigned int i;

	if (lfu_build_room(b, b->lb_nskip) < LFU_REC_WORDS + ea_words)
		return -ENOSPC;

	memset(seg + w, 0, LFU_REC_WORDS * LFU_WORD);
	lfu_put64(seg, w, LFU_INODE_FID_SEQ, li->li_fid.f_seq);
	lfu_put32(seg, w, LFU_INODE_FID_OID, li->li_fid.f_oid);
	lfu_put32(seg, w, LFU_INODE_FID_VER, li->li_fid.f_ver);
	lfu_put64(seg, w, LFU_INODE_SIZE, li->li_size);
	lfu_put64(seg, w, LFU_INODE_BLOCKS, li->li_blocks);
	lfu_put64(seg, w, LFU_INODE_ATIME, li->li_atime);
	lfu_put64(seg, w, LFU_INODE_MTIME, li->li_mtime);
	lfu_put64(seg, w, LFU_INODE_CTIME, li->li_ctime);
	lfu_put64(seg, w, LFU_INODE_BTIME, li->li_btime);
	lfu_put32(seg, w, LFU_INODE_MODE, li->li_mode);
	lfu_put32(seg, w, LFU_INODE_UID, li->li_uid);
	lfu_put32(seg, w, LFU_INODE_GID, li->li_gid);
	lfu_put32(seg, w, LFU_INODE_PROJID, li->li_projid);
	lfu_put32(seg, w, LFU_INODE_NLINK, li->li_nlink);
	lfu_put32(seg, w, LFU_INODE_FLAGS, li->li_flags);
	lfu_put32(seg, w, LFU_INODE_RDEV, li->li_rdev);
	lfu_put32(seg, w, LFU_INODE_VALID, li->li_valid);
	lfu_put64(seg, w, LFU_INODE_INO, li->li_ino);
	lfu_put32(seg, w, LFU_INODE_ATIME_NSEC, li->li_atime_nsec);
	lfu_put32(seg, w, LFU_INODE_MTIME_NSEC, li->li_mtime_nsec);
	lfu_put32(seg, w, LFU_INODE_CTIME_NSEC, li->li_ctime_nsec);
	lfu_put32(seg, w, LFU_INODE_BTIME_NSEC, li->li_btime_nsec);

	if (nptrs != 0) {
		/* the LfuEa first, then its values, all from the back */
		b->lb_back -= ea_words;
		ea = b->lb_back;
		memset(seg + ea, 0, (size_t)ea_words * LFU_WORD);
		lfu_word_put(seg, w + LFU_INODE_DATA_WORDS + LFU_INODE_P_EA,
			     lfu_struct_ptr(w + LFU_INODE_DATA_WORDS +
					    LFU_INODE_P_EA, ea, 0, nptrs));
		d = ea + nptrs;
		for (i = 0; i < nptrs; i++) {
			const struct llapi_scan_data *ld = &li->li_ea[i];

			if (i == LFU_EA_XATTRS || ld->ld_buf == NULL)
				continue;
			/* an empty value is not null: point it at itself */
			if (ld->ld_len == 0) {
				lfu_word_put(seg, ea + i,
					     lfu_list_ptr(ea + i, ea + i,
							  LFU_LIST_BYTE, 0));
				continue;
			}
			lfu_word_put(seg, ea + i,
				     lfu_list_ptr(ea + i, d, LFU_LIST_BYTE,
						  ld->ld_len));
			memcpy(seg + d, ld->ld_buf, ld->ld_len);
			d += lfu_words(ld->ld_len);
		}
	}

	b->lb_front += LFU_REC_WORDS;
	b->lb_count++;
	return 0;
}

/**
 * lfu_build_skipped() - name an object the chunk could not carry
 * @b:		the builder
 * @fid:	the object
 * @error:	a negative errno
 *
 * Return: 0, or -ENOSPC.
 */
int lfu_build_skipped(struct lfu_builder *b, const struct lu_fid *fid,
		      int error)
{
	if (b->lb_nskip >= LFU_BUILD_SKIP_MAX ||
	    b->lb_back - b->lb_front < lfu_skip_words(b->lb_nskip + 1))
		return -ENOSPC;

	b->lb_skip[b->lb_nskip].ls_fid = *fid;
	b->lb_skip[b->lb_nskip].ls_error = error;
	b->lb_nskip++;
	return 0;
}

/**
 * lfu_build_finish() - close the chunk
 * @b:		the builder
 * @scanned:	LfuChunk.scanned
 * @errors:	LfuChunk.errors
 * @cursor:	where a read resumes after this chunk
 * @cursor_len:	at most LLAPI_SCAN_CURSOR_MAX bytes
 *
 * Writes the segment table, the root and the list headers, and zeroes
 * what lies unused between the records and the EA values.
 */
void lfu_build_finish(struct lfu_builder *b, __u32 scanned, __u32 errors,
		      const void *cursor, __u32 cursor_len)
{
	__u64 *seg = b->lb_seg;
	__u32 skip = b->lb_count != 0 ? b->lb_front : LFU_B_TAG;
	__u32 end;
	__u32 i;

	b->lb_table[0] = 0;
	b->lb_table[1] = __cpu_to_le32(b->lb_words);

	memset(seg, 0, LFU_B_TAG * LFU_WORD);
	lfu_word_put(seg, 0, lfu_struct_ptr(0, LFU_B_ROOT,
					    LFU_CHUNK_DATA_WORDS,
					    LFU_CHUNK_PTRS));
	lfu_put32(seg, LFU_B_ROOT, LFU_CHUNK_SCANNED, scanned);
	lfu_put32(seg, LFU_B_ROOT, LFU_CHUNK_ERRORS, errors);

	if (cursor_len > LLAPI_SCAN_CURSOR_MAX)
		cursor_len = LLAPI_SCAN_CURSOR_MAX;
	if (cursor_len != 0) {
		memcpy(seg + LFU_B_CURSOR, cursor, cursor_len);
		lfu_word_put(seg, LFU_B_PTRS + LFU_CHUNK_P_CURSOR,
			     lfu_list_ptr(LFU_B_PTRS + LFU_CHUNK_P_CURSOR,
					  LFU_B_CURSOR, LFU_LIST_BYTE,
					  cursor_len));
	}

	if (b->lb_count != 0) {
		lfu_put16(seg, LFU_B_ROOT, LFU_CHUNK_WHICH, LFU_CHUNK_INODES);
		lfu_word_put(seg, LFU_B_TAG,
			     lfu_list_tag(b->lb_count, LFU_INODE_DATA_WORDS,
					  LFU_INODE_PTRS));
		lfu_word_put(seg, LFU_B_PTRS + LFU_CHUNK_P_UNION,
			     lfu_list_ptr(LFU_B_PTRS + LFU_CHUNK_P_UNION,
					  LFU_B_TAG, LFU_LIST_COMPOSITE,
					  b->lb_count * LFU_REC_WORDS));
	} else {
		lfu_put16(seg, LFU_B_ROOT, LFU_CHUNK_WHICH, LFU_CHUNK_EMPTY);
		seg[LFU_B_TAG] = 0;
	}

	end = skip;
	if (b->lb_nskip != 0) {
		lfu_word_put(seg, skip,
			     lfu_list_tag(b->lb_nskip, LFU_SKIPPED_DATA_WORDS,
					  0));
		lfu_word_put(seg, LFU_B_PTRS + LFU_CHUNK_P_SKIPPED,
			     lfu_list_ptr(LFU_B_PTRS + LFU_CHUNK_P_SKIPPED,
					  skip, LFU_LIST_COMPOSITE,
					  b->lb_nskip *
					  LFU_SKIPPED_DATA_WORDS));
		for (i = 0; i < b->lb_nskip; i++) {
			__u32 w = skip + 1 + i * LFU_SKIPPED_DATA_WORDS;

			memset(seg + w, 0, LFU_SKIPPED_DATA_WORDS * LFU_WORD);
			lfu_put64(seg, w, LFU_SKIPPED_FID_SEQ,
				  b->lb_skip[i].ls_fid.f_seq);
			lfu_put32(seg, w, LFU_SKIPPED_FID_OID,
				  b->lb_skip[i].ls_fid.f_oid);
			lfu_put32(seg, w, LFU_SKIPPED_FID_VER,
				  b->lb_skip[i].ls_fid.f_ver);
			lfu_put32(seg, w, LFU_SKIPPED_ERROR,
				  (__u32)b->lb_skip[i].ls_error);
		}
		end = skip + lfu_skip_words(b->lb_nskip);
	}

	/* "Unused words are zero" */
	if (b->lb_back > end)
		memset(seg + end, 0, (size_t)(b->lb_back - end) * LFU_WORD);
}
