/* SPDX-License-Identifier: LGPL-2.1+ WITH Linux-syscall-note */

/*
 * Copyright (c) 2026, The Lustre Collective.
 */

/*
 * The LFU Object Stream wire format, as compiled from lfu.capnp.
 *
 * lfu.capnp in this directory is the definition.  This header holds what
 * a C reader or writer needs and Cap'n Proto's compiler would otherwise
 * generate: the enum values, and where each field the Lustre producers
 * write sits in its struct.  Offsets are in bytes from the start of a
 * struct's data section, or are an index into its pointer section.  They
 * are fixed by the schema's field numbers, so they change only if the
 * schema is changed incompatibly, which its rules forbid.
 *
 * A chunk is one unpacked, single-segment Cap'n Proto message whose root
 * is an LfuChunk, starting on a slot boundary and padded with zeroes to
 * one.  See "Bulk Format" in lfu.capnp.
 */
#ifndef _LUSTRE_LFU_CAPNP_H
#define _LUSTRE_LFU_CAPNP_H

#include <linux/types.h>

/* Slot sizes, in bytes: a power of two between the two limits */
#define LFU_CHUNK_SIZE_MIN	4096
#define LFU_CHUNK_SIZE_DEFAULT	65536
#define LFU_CHUNK_SIZE_MAX	1048576

/* enum LfuKind: what a stream carries */
enum lfu_kind {
	LFU_KIND_MDT_INODES	= 0,
	LFU_KIND_CHANGELOG	= 1,
	LFU_KIND_OST_OBJECTS	= 2,
};

/* enum LfuSource: which scanner produces a stream */
enum lfu_source {
	LFU_SOURCE_ANY		= 0,
	LFU_SOURCE_OSD		= 1,
	LFU_SOURCE_DEVICE	= 2,
	LFU_SOURCE_NAMESPACE	= 3,
	LFU_SOURCE_POSIX	= 4,
	LFU_SOURCE_CHANGELOG	= 5,
	LFU_SOURCE_INDEX	= 6,
	LFU_SOURCE_TRANSCRIPT	= 7,
};

/* enum LfuFeature: bit numbers of LfuOpen.features and the reply's */
enum lfu_feature {
	LFU_FEATURE_JUMBO_CHUNKS	= 0,
	LFU_FEATURE_PARTIAL_RECORDS	= 1,
	LFU_FEATURE_FID_CHUNKS		= 2,
	LFU_FEATURE_LOOKUP		= 3,
	LFU_FEATURE_BUCKET_CHUNKS	= 4,
	LFU_FEATURE_SKIPPED		= 5,
	LFU_FEATURE_END_CHUNK		= 6,
};

#define LFU_FEATURE_BIT(f)	(1ULL << (f))

/* enum LfuBulkEncoding */
enum lfu_bulk_encoding {
	LFU_BULK_SLOTS		= 0,
	LFU_BULK_TRIMMED	= 1,
	LFU_BULK_PACKED		= 2,
};

/* enum LfuAttr: bit numbers of LfuInode.valid */
enum lfu_attr {
	LFU_ATTR_SIZE		= 0,
	LFU_ATTR_BLOCKS		= 1,
	LFU_ATTR_ATIME		= 2,
	LFU_ATTR_MTIME		= 3,
	LFU_ATTR_CTIME		= 4,
	LFU_ATTR_BTIME		= 5,
	LFU_ATTR_MODE		= 6,
	LFU_ATTR_UID		= 7,
	LFU_ATTR_GID		= 8,
	LFU_ATTR_PROJID		= 9,
	LFU_ATTR_NLINK		= 10,
	LFU_ATTR_FLAGS		= 11,
	LFU_ATTR_RDEV		= 12,
	LFU_ATTR_INO		= 13,
};

#define LFU_ATTR_BIT(a)		(1U << (a))

/* struct LfuChunk: 2 data words, 3 pointers */
#define LFU_CHUNK_DATA_WORDS	2
#define LFU_CHUNK_PTRS		3
#define LFU_CHUNK_SCANNED	0	/* UInt32 */
#define LFU_CHUNK_ERRORS	4	/* UInt32 */
#define LFU_CHUNK_WHICH		8	/* UInt16, the union's discriminant */
#define LFU_CHUNK_P_CURSOR	0	/* Data */
#define LFU_CHUNK_P_UNION	1	/* the union member's pointer */
#define LFU_CHUNK_P_SKIPPED	2	/* List(LfuSkipped) */

/* LfuChunk's union, the values of LFU_CHUNK_WHICH */
enum lfu_chunk_which {
	LFU_CHUNK_EMPTY		= 0,
	LFU_CHUNK_INODES	= 1,
	LFU_CHUNK_FIDS		= 2,
	LFU_CHUNK_BUCKETS	= 3,
	LFU_CHUNK_CHANGELOGS	= 4,
	LFU_CHUNK_END		= 5,
};

/* struct LfuInode: 15 data words, 1 pointer, 128 bytes */
#define LFU_INODE_DATA_WORDS	15
#define LFU_INODE_PTRS		1
#define LFU_INODE_FID_SEQ	0	/* UInt64 */
#define LFU_INODE_FID_OID	8	/* UInt32 */
#define LFU_INODE_FID_VER	12	/* UInt32 */
#define LFU_INODE_SIZE		16	/* UInt64 */
#define LFU_INODE_BLOCKS	24	/* UInt64 */
#define LFU_INODE_ATIME		32	/* Int64 */
#define LFU_INODE_MTIME		40	/* Int64 */
#define LFU_INODE_CTIME		48	/* Int64 */
#define LFU_INODE_BTIME		56	/* Int64 */
#define LFU_INODE_MODE		64	/* UInt32 */
#define LFU_INODE_UID		68	/* UInt32 */
#define LFU_INODE_GID		72	/* UInt32 */
#define LFU_INODE_PROJID	76	/* UInt32 */
#define LFU_INODE_NLINK		80	/* UInt32 */
#define LFU_INODE_FLAGS		84	/* UInt32 */
#define LFU_INODE_RDEV		88	/* UInt32 */
#define LFU_INODE_VALID		92	/* UInt32 */
#define LFU_INODE_INO		96	/* UInt64 */
#define LFU_INODE_ATIME_NSEC	104	/* UInt32 */
#define LFU_INODE_MTIME_NSEC	108	/* UInt32 */
#define LFU_INODE_CTIME_NSEC	112	/* UInt32 */
#define LFU_INODE_BTIME_NSEC	116	/* UInt32 */
#define LFU_INODE_P_EA		0	/* LfuEa */

/*
 * struct LfuEa: 11 pointers, each Data but xattrs, and the index of each
 * in its pointer section.  Written cut after its last non-null field.
 */
enum lfu_ea {
	LFU_EA_PATH	= 0,
	LFU_EA_FID	= 1,	/* trusted.fid */
	LFU_EA_LINK	= 2,	/* trusted.link */
	LFU_EA_LOV	= 3,	/* trusted.lov */
	LFU_EA_LMV	= 4,	/* trusted.lmv */
	LFU_EA_HSM	= 5,	/* trusted.hsm */
	LFU_EA_SOM	= 6,	/* trusted.som */
	LFU_EA_LMA	= 7,	/* trusted.lma */
	LFU_EA_DMV	= 8,	/* trusted.dmv */
	LFU_EA_XATTRS	= 9,	/* List(LfuXattr), not Data */
	LFU_EA_ENC	= 10,	/* the encryption context */
	LFU_EA_DATA_MAX	= 11,	/* the Data fields, but xattrs, end here */
};

#define LFU_EA_PTRS		11

/* struct LfuSkipped: 3 data words */
#define LFU_SKIPPED_DATA_WORDS	3
#define LFU_SKIPPED_FID_SEQ	0	/* UInt64 */
#define LFU_SKIPPED_FID_OID	8	/* UInt32 */
#define LFU_SKIPPED_FID_VER	12	/* UInt32 */
#define LFU_SKIPPED_ERROR	16	/* Int32 */

/* struct LfuFid: 2 data words */
#define LFU_FID_DATA_WORDS	2
#define LFU_FID_SEQ		0	/* UInt64 */
#define LFU_FID_OID		8	/* UInt32 */
#define LFU_FID_VER		12	/* UInt32 */

#endif /* _LUSTRE_LFU_CAPNP_H */
