// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2026, The Lustre Collective.
 */

/*
 * Tests for the LFU Object Stream codec, liblustreapi_lfu.c.
 *
 * The codec needs no filesystem: it lays LfuChunk messages out in memory
 * and reads them back.  The program is linked with liblfu_codec.la, the
 * codec liblustreapi is built from, so that the writer, which liblustreapi
 * does not export, can be tested too.
 *
 * test1 reads a chunk that libcapnp wrote (pycapnp 2.2.4, lfu.capnp), so
 * the reader is held to Cap'n Proto and not only to its own writer, and
 * test8 one with shorter records, as an older writer produces.  The
 * other way round, "-w FILE" saves the bulk test2 writes, which a stock
 * reader can check:
 *	capnp decode lfu.capnp LfuChunk < FILE
 *
 * Author: Artem Blagodarenko <ablagodarenko@thelustrecollective.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lustre/lustreapi.h>

#include "llapi_test_utils.h"
#include "lustreapi_lfu.h"

#define SLOT	4096U

static const char *write_file;

/* the bulk every test writes into: 8-byte aligned, as the format needs */
static __u64 bulk[64 * SLOT / sizeof(__u64)];

/*
 * Written by pycapnp from lfu.capnp: an LfuChunk with scanned 7, errors 1,
 * cursor "cursor-0123", two inodes (the first with path, link, lov and
 * enc, an ino and nanoseconds) and one skipped object, its segment table
 * rewritten to fill a 4096-byte slot.  The bytes not listed are zero.
 */
static const unsigned char lfu_vector[] = {
	0x00, 0x00, 0x00, 0x00, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x03, 0x00, 0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
	0x5a, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x07, 0x01, 0x00, 0x00,
	0xed, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x63, 0x75, 0x72, 0x73,
	0x6f, 0x72, 0x2d, 0x30, 0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x08, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x01, 0x00, 0x01, 0x04, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01, 0xf1, 0x53, 0x65, 0x00, 0x00, 0x00, 0x00,
	0x02, 0xf1, 0x53, 0x65, 0x00, 0x00, 0x00, 0x00, 0x03, 0xf1, 0x53, 0x65,
	0x00, 0x00, 0x00, 0x00, 0x04, 0xf1, 0x53, 0x65, 0x00, 0x00, 0x00, 0x00,
	0xa4, 0x81, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0xf5, 0x01, 0x00, 0x00,
	0xf6, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xff, 0x3f, 0x00, 0x00, 0x89, 0x67, 0x45, 0x23,
	0x01, 0x00, 0x00, 0x00, 0x6f, 0x00, 0x00, 0x00, 0xde, 0x00, 0x00, 0x00,
	0x4d, 0x01, 0x00, 0x00, 0xff, 0xc9, 0x9a, 0x3b, 0x40, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x0b, 0x00, 0x02, 0x04, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xed, 0x41, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x29, 0x00, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x2a, 0x01, 0x00, 0x00,
	0x35, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00,
	0xa2, 0x00, 0x00, 0x00, 0x64, 0x69, 0x72, 0x2f, 0x66, 0x69, 0x6c, 0x65,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
	0x24, 0x00, 0x00, 0x00, 0xd0, 0x0b, 0xd1, 0x0b, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x01, 0x04, 0x00, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x00, 0x00, 0x00, 0x00,
	0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x04, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xb5, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*
 * The same chunk with LfuInode records of 12 data words, not 15, as an
 * older writer would produce: the fields past them read as zero.
 */
static const unsigned char lfu_vector_short[] = {
	0x00, 0x00, 0x00, 0x00, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x03, 0x00, 0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
	0x5a, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0xd7, 0x00, 0x00, 0x00,
	0xc5, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x63, 0x75, 0x72, 0x73,
	0x6f, 0x72, 0x2d, 0x30, 0x31, 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x08, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x01, 0x04, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01, 0xf1, 0x53, 0x65, 0x00, 0x00, 0x00, 0x00,
	0x02, 0xf1, 0x53, 0x65, 0x00, 0x00, 0x00, 0x00, 0x03, 0xf1, 0x53, 0x65,
	0x00, 0x00, 0x00, 0x00, 0x04, 0xf1, 0x53, 0x65, 0x00, 0x00, 0x00, 0x00,
	0xa4, 0x81, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0xf5, 0x01, 0x00, 0x00,
	0xf6, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xff, 0x1f, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x0a, 0x00, 0x02, 0x04, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xed, 0x41, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x25, 0x00, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x2a, 0x01, 0x00, 0x00,
	0x31, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x69, 0x72, 0x2f,
	0x66, 0x69, 0x6c, 0x65, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
	0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x00, 0x00, 0x00, 0xd0, 0x0b, 0xd1, 0x0b,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x03, 0x04, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xb5, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
};

static void load_vector(void)
{
	memset(bulk, 0, SLOT);
	memcpy(bulk, lfu_vector, sizeof(lfu_vector));
}

static void load_vector_short(void)
{
	memset(bulk, 0, SLOT);
	memcpy(bulk, lfu_vector_short, sizeof(lfu_vector_short));
}

/* a Data field of a chunk lies inside the bulk it was read from */
static void check_data_bounds(const struct llapi_scan_data *ld,
			      const void *buf, size_t len, const char *what,
			      unsigned int i)
{
	const char *lo = buf;
	const char *p = ld->ld_buf;

	if (p == NULL)
		return;
	ASSERTF(p >= lo && p + ld->ld_len <= lo + len,
		"%s %u at %p+%u is outside the bulk %p+%zu", what, i,
		(void *)p, ld->ld_len, buf, len);
}

/* every EA of a record lies inside the bulk it was read from */
static void check_ea_bounds(const struct llapi_scan_inode *li,
			    const void *buf, size_t len)
{
	unsigned int i;

	for (i = 0; i < LLAPI_SCAN_EA_SLOTS; i++) {
		if (i >= LFU_EA_DATA_MAX || i == LFU_EA_XATTRS)
			ASSERTF(li->li_ea[i].ld_buf == NULL &&
				li->li_ea[i].ld_len == 0,
				"EA slot %u, not a Data field, is set", i);
		check_data_bounds(&li->li_ea[i], buf, len, "EA", i);
	}
}

static bool data_is(const struct llapi_scan_data *ld, const void *want,
		    size_t len)
{
	return ld->ld_buf != NULL && ld->ld_len == len &&
	       memcmp(ld->ld_buf, want, len) == 0;
}

#define T1_DESC "read a chunk that libcapnp wrote"
static void test1(void)
{
	struct llapi_scan_chunk ch;
	struct llapi_scan_inode li;
	unsigned char link[37];
	struct lu_fid fid;
	unsigned int i;
	int error;
	int rc;

	load_vector();
	rc = llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch);
	ASSERTF(rc == 0, "parse: %s", strerror(-rc));
	ASSERTF(ch.lc_slots == 1, "slots %u", ch.lc_slots);
	ASSERTF(ch.lc_scanned == 7 && ch.lc_errors == 1,
		"scanned %u errors %u", ch.lc_scanned, ch.lc_errors);
	ASSERTF(ch.lc_which == LFU_CHUNK_INODES, "which %u", ch.lc_which);
	ASSERTF(ch.lc_count == 2, "count %u", ch.lc_count);
	ASSERTF(data_is(&ch.lc_cursor, "cursor-0123", 11), "cursor");

	rc = llapi_scan_chunk_inode(&ch, 0, &li);
	ASSERTF(rc == 0, "inode 0: %s", strerror(-rc));
	ASSERTF(li.li_fid.f_seq == 0x200000401ULL && li.li_fid.f_oid == 1 &&
		li.li_fid.f_ver == 0, "fid "DFID, PFID(&li.li_fid));
	ASSERTF(li.li_size == 4096 && li.li_blocks == 8, "size");
	ASSERTF(li.li_atime == 1700000001 && li.li_mtime == 1700000002 &&
		li.li_ctime == 1700000003 && li.li_btime == 1700000004,
		"times");
	ASSERTF(li.li_mode == 0100644 && li.li_uid == 500 &&
		li.li_gid == 501 && li.li_projid == 502 && li.li_nlink == 1 &&
		li.li_flags == 0x10 && li.li_rdev == 0, "scalars");
	ASSERTF(li.li_valid == (1U << 14) - 1, "valid %#x", li.li_valid);
	ASSERTF(li.li_ino == 0x123456789ULL, "ino %#llx",
		(unsigned long long)li.li_ino);
	ASSERTF(li.li_atime_nsec == 111 && li.li_mtime_nsec == 222 &&
		li.li_ctime_nsec == 333 && li.li_btime_nsec == 999999999,
		"nanoseconds");
	ASSERTF(data_is(&li.li_ea[LFU_EA_PATH], "dir/file", 8), "path");
	for (i = 0; i < sizeof(link); i++)
		link[i] = i;
	ASSERTF(data_is(&li.li_ea[LFU_EA_LINK], link, sizeof(link)), "link");
	ASSERTF(li.li_ea[LFU_EA_LOV].ld_len == 32, "lov");
	ASSERTF(li.li_ea[LFU_EA_ENC].ld_len == 20 &&
		((const __u8 *)li.li_ea[LFU_EA_ENC].ld_buf)[19] == 0x4f, "enc");
	ASSERTF(li.li_ea[LFU_EA_FID].ld_buf == NULL &&
		li.li_ea[LFU_EA_LMA].ld_buf == NULL, "null EAs");
	check_ea_bounds(&li, bulk, SLOT);

	rc = llapi_scan_chunk_inode(&ch, 1, &li);
	ASSERTF(rc == 0, "inode 1: %s", strerror(-rc));
	ASSERTF(li.li_fid.f_oid == 2 && li.li_fid.f_ver == 3 &&
		li.li_mode == 040755 && li.li_nlink == 2 &&
		li.li_valid == ((1U << 6) | (1U << 10)), "inode 1 scalars");
	for (i = 0; i < LFU_EA_DATA_MAX; i++)
		ASSERTF(li.li_ea[i].ld_buf == NULL, "inode 1 has EA %u", i);

	rc = llapi_scan_chunk_inode(&ch, 2, &li);
	ASSERTF(rc == -ERANGE, "inode 2: %d", rc);

	ASSERTF(ch.lc_skipped == 1, "skipped %u", ch.lc_skipped);
	rc = llapi_scan_chunk_skipped(&ch, 0, &fid, &error);
	ASSERTF(rc == 0 && fid.f_seq == 0x200000403ULL && fid.f_oid == 4 &&
		error == -EOVERFLOW, "skipped "DFID" %d", PFID(&fid), error);
}

/* a record whose every field is a function of @i */
static void make_inode(struct llapi_scan_inode *li, unsigned int i,
		       unsigned char *eabuf, size_t easize)
{
	unsigned int e;
	size_t off = 0;

	memset(li, 0, sizeof(*li));
	li->li_fid.f_seq = 0x200000400ULL + i;
	li->li_fid.f_oid = i * 7 + 1;
	li->li_fid.f_ver = i & 3;
	li->li_size = (__u64)i * 1000003;
	li->li_blocks = i * 17;
	li->li_atime = 1700000000 + i;
	li->li_mtime = -(__s64)i;
	li->li_ctime = 1700000000 + 2 * i;
	li->li_btime = 1;
	li->li_mode = (i & 1) ? 040755 : 0100600;
	li->li_uid = i;
	li->li_gid = i + 1;
	li->li_projid = i + 2;
	li->li_nlink = 1 + (i & 1);
	li->li_flags = i & 0x30;
	li->li_rdev = i << 8;
	li->li_valid = i & 0x3fff;
	li->li_ino = 0x1000000000ULL + i * 3;
	li->li_atime_nsec = i * 1000;
	li->li_mtime_nsec = 999999999 - i;
	li->li_ctime_nsec = i;
	li->li_btime_nsec = i * 7;

	/* EAs of varied presence and length, some empty, some unaligned */
	for (e = 0; e < LFU_EA_DATA_MAX; e++) {
		size_t len = (i * 13 + e * 29) % 61;

		if (e == LFU_EA_XATTRS || (i + e) % 3 == 0)
			continue;
		if ((i + e) % 7 == 0)
			len = 0;
		if (off + len > easize)
			break;
		memset(eabuf + off, (int)(i ^ e), len);
		li->li_ea[e].ld_buf = eabuf + off;
		li->li_ea[e].ld_len = len;
		off += len;
	}
}

static void check_inode(const struct llapi_scan_inode *got,
			const struct llapi_scan_inode *want, unsigned int i)
{
	unsigned int e;

	ASSERTF(memcmp(&got->li_fid, &want->li_fid, sizeof(got->li_fid)) == 0,
		"%u: fid", i);
	ASSERTF(got->li_size == want->li_size &&
		got->li_blocks == want->li_blocks &&
		got->li_atime == want->li_atime &&
		got->li_mtime == want->li_mtime &&
		got->li_ctime == want->li_ctime &&
		got->li_btime == want->li_btime &&
		got->li_mode == want->li_mode &&
		got->li_uid == want->li_uid && got->li_gid == want->li_gid &&
		got->li_projid == want->li_projid &&
		got->li_nlink == want->li_nlink &&
		got->li_flags == want->li_flags &&
		got->li_rdev == want->li_rdev &&
		got->li_valid == want->li_valid &&
		got->li_ino == want->li_ino &&
		got->li_atime_nsec == want->li_atime_nsec &&
		got->li_mtime_nsec == want->li_mtime_nsec &&
		got->li_ctime_nsec == want->li_ctime_nsec &&
		got->li_btime_nsec == want->li_btime_nsec, "%u: scalars", i);
	for (e = 0; e < LFU_EA_DATA_MAX; e++) {
		const struct llapi_scan_data *w = &want->li_ea[e];
		const struct llapi_scan_data *g = &got->li_ea[e];

		if (w->ld_buf == NULL) {
			ASSERTF(g->ld_buf == NULL, "%u: EA %u not null", i, e);
			continue;
		}
		ASSERTF(data_is(g, w->ld_buf, w->ld_len),
			"%u: EA %u differs (%u bytes, want %u)", i, e,
			g->ld_len, w->ld_len);
	}
}

/* the FID record @i is skipped with, when fill_bulk() names one */
static void skip_fid(struct lu_fid *fid, unsigned int i)
{
	fid->f_seq = 0x200000500ULL + i;
	fid->f_oid = i + 1;
	fid->f_ver = 0;
}

/*
 * Fill @nslots slots with records 0..; return how many were written.  With
 * @skips, every fifth record is followed by an entry of the skipped list,
 * as a producer adds one for an object too large for its slot.
 */
/* the skipped entries the last fill_bulk() wrote */
static unsigned int fill_skips;

static unsigned int fill_bulk(unsigned int nslots, unsigned int *chunks,
			      bool skips)
{
	unsigned char eabuf[LFU_EA_DATA_MAX * 64];
	struct llapi_scan_inode li;
	struct lfu_builder b;
	unsigned int slot;
	unsigned int i = 0;
	char cursor[16];

	*chunks = 0;
	fill_skips = 0;
	for (slot = 0; slot < nslots; slot++) {
		unsigned int start = i;

		lfu_build_begin(&b, (char *)bulk + slot * SLOT, SLOT);
		for (;;) {
			make_inode(&li, i, eabuf, sizeof(eabuf));
			if (lfu_build_inode(&b, &li) != 0)
				break;
			i++;
			if (skips && (i - 1) % 5 == 0) {
				struct lu_fid fid;

				/* the record is in: only its entry is not */
				skip_fid(&fid, i - 1);
				if (lfu_build_skipped(&b, &fid, -EOVERFLOW))
					break;
				fill_skips++;
			}
		}
		ASSERTF(i > start, "slot %u took no record", slot);
		snprintf(cursor, sizeof(cursor), "next=%u", i);
		lfu_build_finish(&b, i - start + 1, 1, cursor,
				 strlen(cursor));
		(*chunks)++;
	}
	return i;
}

#define T2_DESC "write records over several chunks and read them back"
static void test2(void)
{
	unsigned char eabuf[LFU_EA_DATA_MAX * 64];
	const unsigned int nslots = 16;
	struct llapi_scan_chunk ch;
	struct llapi_scan_inode got;
	struct llapi_scan_inode want;
	unsigned int written;
	unsigned int chunks;
	unsigned int seen = 0;
	unsigned int n = 0;
	size_t off = 0;
	char cursor[16];
	int rc;

	written = fill_bulk(nslots, &chunks, false);

	while (off < nslots * SLOT) {
		unsigned int j;

		rc = llapi_scan_chunk_parse((char *)bulk + off,
					    nslots * SLOT - off, SLOT, &ch);
		ASSERTF(rc == 0, "chunk %u: %s", n, strerror(-rc));
		ASSERTF(ch.lc_which == LFU_CHUNK_INODES && ch.lc_count > 0,
			"chunk %u: which %u count %u", n, ch.lc_which,
			ch.lc_count);
		ASSERTF(ch.lc_scanned == ch.lc_count + 1 && ch.lc_errors == 1,
			"chunk %u: counters", n);
		for (j = 0; j < ch.lc_count; j++) {
			rc = llapi_scan_chunk_inode(&ch, j, &got);
			ASSERTF(rc == 0, "record %u: %s", seen, strerror(-rc));
			check_ea_bounds(&got, (char *)bulk + off, SLOT);
			make_inode(&want, seen, eabuf, sizeof(eabuf));
			check_inode(&got, &want, seen);
			seen++;
		}
		snprintf(cursor, sizeof(cursor), "next=%u", seen);
		ASSERTF(data_is(&ch.lc_cursor, cursor, strlen(cursor)),
			"chunk %u: cursor", n);
		off += ch.lc_slots * SLOT;
		n++;
	}
	ASSERTF(n == chunks && seen == written,
		"read %u chunks, %u records; wrote %u, %u", n, seen, chunks,
		written);

	if (write_file != NULL) {
		int fd = open(write_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		ASSERTF(fd >= 0, "open %s: %s", write_file, strerror(errno));
		ASSERTF(write(fd, bulk, nslots * SLOT) ==
			(ssize_t)(nslots * SLOT), "write %s", write_file);
		close(fd);
	}
}

#define T3_DESC "a record that does not fit leaves the chunk as it was"
static void test3(void)
{
	static unsigned char big[SLOT];
	struct llapi_scan_inode li = { .li_fid = { 0x200000400ULL, 1, 0 } };
	struct llapi_scan_inode got;
	struct llapi_scan_chunk ch;
	struct lfu_builder b;
	size_t need;
	int rc;

	lfu_build_begin(&b, bulk, SLOT);
	rc = lfu_build_inode(&b, &li);
	ASSERTF(rc == 0, "small record: %s", strerror(-rc));

	/* an EA as large as the slot cannot fit beside the header */
	li.li_fid.f_oid = 2;
	li.li_ea[LFU_EA_LINK].ld_buf = big;
	li.li_ea[LFU_EA_LINK].ld_len = sizeof(big);
	rc = lfu_build_inode(&b, &li);
	ASSERTF(rc == -ENOSPC, "big record: %d", rc);

	lfu_build_finish(&b, 2, 0, NULL, 0);
	rc = llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch);
	ASSERTF(rc == 0 && ch.lc_count == 1 && ch.lc_cursor.ld_buf == NULL,
		"after -ENOSPC: rc %d count %u", rc, ch.lc_count);
	rc = llapi_scan_chunk_inode(&ch, 0, &got);
	ASSERTF(rc == 0 && got.li_fid.f_oid == 1 &&
		got.li_ea[LFU_EA_LINK].ld_buf == NULL, "first record kept");

	/* a jumbo chunk: three slots, one record */
	need = lfu_build_inode_bytes(&li);
	ASSERTF(need > SLOT && need <= 3 * SLOT, "need %zu", need);
	lfu_build_begin(&b, bulk, 3 * SLOT);
	rc = lfu_build_inode(&b, &li);
	ASSERTF(rc == 0, "jumbo: %s", strerror(-rc));
	lfu_build_finish(&b, 1, 0, "j", 1);
	rc = llapi_scan_chunk_parse(bulk, 3 * SLOT, SLOT, &ch);
	ASSERTF(rc == 0 && ch.lc_slots == 3 && ch.lc_count == 1,
		"jumbo: rc %d slots %u", rc, ch.lc_slots);
	rc = llapi_scan_chunk_inode(&ch, 0, &got);
	ASSERTF(rc == 0 && data_is(&got.li_ea[LFU_EA_LINK], big, sizeof(big)),
		"jumbo record");
	/* and it does not parse in less than its own length */
	rc = llapi_scan_chunk_parse(bulk, 2 * SLOT, SLOT, &ch);
	ASSERTF(rc == -EINVAL, "jumbo in 2 slots: %d", rc);
}

#define T4_DESC "a chunk of progress only: no records, some skipped"
static void test4(void)
{
	struct lu_fid fid = { 0x200000400ULL, 9, 0 };
	struct llapi_scan_chunk ch;
	struct llapi_scan_inode li;
	struct lfu_builder b;
	struct lu_fid got;
	unsigned int i;
	int error;
	int rc;

	/* the writer must zero what is unused */
	memset(bulk, 0xa5, SLOT);
	lfu_build_begin(&b, bulk, SLOT);
	for (i = 0; i < LFU_BUILD_SKIP_MAX; i++) {
		fid.f_oid = 100 + i;
		rc = lfu_build_skipped(&b, &fid, -EOVERFLOW);
		ASSERTF(rc == 0, "skip %u: %s", i, strerror(-rc));
	}
	rc = lfu_build_skipped(&b, &fid, -EOVERFLOW);
	ASSERTF(rc == -ENOSPC, "skip past the limit: %d", rc);
	lfu_build_finish(&b, 1000, LFU_BUILD_SKIP_MAX, "c", 1);

	rc = llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch);
	ASSERTF(rc == 0, "parse: %s", strerror(-rc));
	ASSERTF(ch.lc_which == LFU_CHUNK_EMPTY && ch.lc_count == 0 &&
		ch.lc_scanned == 1000 && ch.lc_skipped == LFU_BUILD_SKIP_MAX,
		"which %u count %u skipped %u", ch.lc_which, ch.lc_count,
		ch.lc_skipped);
	rc = llapi_scan_chunk_inode(&ch, 0, &li);
	ASSERTF(rc == -ERANGE, "inode of an empty chunk: %d", rc);
	for (i = 0; i < ch.lc_skipped; i++) {
		rc = llapi_scan_chunk_skipped(&ch, i, &got, &error);
		ASSERTF(rc == 0 && got.f_oid == 100 + i && error == -EOVERFLOW,
			"skipped %u", i);
	}
	/*
	 * Past the skipped list, every word up to the slot's end is zero.  In
	 * a chunk with no records the writer puts the list's tag at segment
	 * word 11 and its entries after it; the segment starts at bulk[1].
	 */
	for (i = 1 + 12 + LFU_BUILD_SKIP_MAX * LFU_SKIPPED_DATA_WORDS;
	     i < SLOT / 8; i++)
		ASSERTF(bulk[i] == 0, "word %u not zero", i);
}

#define T5_DESC "bytes that are not a chunk are refused"
static void test5(void)
{
	struct llapi_scan_chunk ch;
	__u32 *table = (__u32 *)bulk;
	int rc;

	load_vector();
	ASSERTF(llapi_scan_chunk_parse(bulk, SLOT - 8, SLOT, &ch) == -EINVAL,
		"shorter than a slot");
	ASSERTF(llapi_scan_chunk_parse((char *)bulk + 4, SLOT, SLOT, &ch) ==
		-EINVAL, "misaligned");
	ASSERTF(llapi_scan_chunk_parse(bulk, SLOT, 3000, &ch) == -EINVAL,
		"slot size not a power of two");
	ASSERTF(llapi_scan_chunk_parse(bulk, 2 * SLOT, 2048, &ch) == -EINVAL,
		"slot size below the minimum");

	table[0] = 1;	/* two segments */
	ASSERTF(llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch) == -EINVAL,
		"two segments");
	load_vector();
	table[1] = 100;	/* does not end on a slot boundary */
	ASSERTF(llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch) == -EINVAL,
		"segment not a slot");
	load_vector();
	bulk[1] = 0;	/* null root */
	ASSERTF(llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch) == -EINVAL,
		"null root");
	load_vector();
	bulk[1] |= 2;	/* a far pointer as the root */
	ASSERTF(llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch) == -EINVAL,
		"far root");

	load_vector();
	rc = llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch);
	ASSERTF(rc == 0, "vector again: %s", strerror(-rc));
}

#define T6_DESC "damaged chunks never read outside the slot"
static void test6(void)
{
	unsigned int written;
	unsigned int chunks;
	unsigned int pos;
	unsigned int bit;
	unsigned int bad = 0;
	__u64 save;

	/*
	 * One slot of records and skipped entries, then every bit of it
	 * flipped: the records, their EA values, the skipped list and the
	 * list headers between them.
	 */
	written = fill_bulk(1, &chunks, true);
	ASSERTF(written > 0, "no records");
	for (pos = 0; pos < SLOT / sizeof(__u64); pos++) {
		for (bit = 0; bit < 64; bit++) {
			struct llapi_scan_chunk ch;
			struct llapi_scan_inode li;
			struct lu_fid fid;
			unsigned int j;
			int error;

			save = bulk[pos];
			bulk[pos] ^= 1ULL << bit;
			if (llapi_scan_chunk_parse(bulk, SLOT, SLOT,
						   &ch) != 0) {
				bad++;
				bulk[pos] = save;
				continue;
			}
			check_data_bounds(&ch.lc_cursor, bulk, SLOT,
					  "cursor", 0);
			for (j = 0; j < ch.lc_count; j++) {
				if (llapi_scan_chunk_inode(&ch, j, &li) != 0) {
					bad++;
					continue;
				}
				check_ea_bounds(&li, bulk, SLOT);
			}
			for (j = 0; j < ch.lc_skipped; j++)
				if (llapi_scan_chunk_skipped(&ch, j, &fid,
							     &error) != 0)
					bad++;
			bulk[pos] = save;
		}
	}
	ASSERTF(bad > 0, "no damage was noticed");
}

#define T7_DESC "records and skipped entries in one chunk"
static void test7(void)
{
	unsigned char eabuf[LFU_EA_DATA_MAX * 64];
	struct llapi_scan_inode want;
	struct llapi_scan_inode got;
	struct llapi_scan_chunk ch;
	unsigned int written;
	unsigned int chunks;
	unsigned int nskip;
	unsigned int i;
	int rc;

	/*
	 * The skipped list follows the records, at lb_front, and not at its
	 * place in a chunk without them; lfu_build_inode() keeps room for it
	 * as the records grow.  Fill a slot to the last word that way.
	 */
	memset(bulk, 0xa5, SLOT);
	written = fill_bulk(1, &chunks, true);
	nskip = fill_skips;
	ASSERTF(nskip > 1, "only %u skipped entries", nskip);

	rc = llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch);
	ASSERTF(rc == 0, "parse: %s", strerror(-rc));
	ASSERTF(ch.lc_which == LFU_CHUNK_INODES && ch.lc_count == written &&
		ch.lc_skipped == nskip, "which %u count %u/%u skipped %u/%u",
		ch.lc_which, ch.lc_count, written, ch.lc_skipped, nskip);
	for (i = 0; i < ch.lc_count; i++) {
		rc = llapi_scan_chunk_inode(&ch, i, &got);
		ASSERTF(rc == 0, "record %u: %s", i, strerror(-rc));
		check_ea_bounds(&got, bulk, SLOT);
		make_inode(&want, i, eabuf, sizeof(eabuf));
		check_inode(&got, &want, i);
	}
	for (i = 0; i < ch.lc_skipped; i++) {
		struct lu_fid fid;
		struct lu_fid exp;
		int error;

		rc = llapi_scan_chunk_skipped(&ch, i, &fid, &error);
		skip_fid(&exp, i * 5);
		ASSERTF(rc == 0 && memcmp(&fid, &exp, sizeof(fid)) == 0 &&
			error == -EOVERFLOW, "skipped %u: "DFID" %d", i,
			PFID(&fid), error);
	}
}

#define T8_DESC "read records shorter than LfuInode"
static void test8(void)
{
	struct llapi_scan_chunk ch;
	struct llapi_scan_inode li;
	int rc;

	load_vector_short();
	rc = llapi_scan_chunk_parse(bulk, SLOT, SLOT, &ch);
	ASSERTF(rc == 0, "parse: %s", strerror(-rc));
	ASSERTF(ch.lc_count == 2, "count %u", ch.lc_count);

	rc = llapi_scan_chunk_inode(&ch, 0, &li);
	ASSERTF(rc == 0, "inode 0: %s", strerror(-rc));
	ASSERTF(li.li_fid.f_oid == 1 && li.li_btime == 1700000004 &&
		li.li_valid == (1U << 13) - 1, "inode 0 scalars");
	ASSERTF(li.li_ino == 0 && li.li_atime_nsec == 0 &&
		li.li_mtime_nsec == 0 && li.li_ctime_nsec == 0 &&
		li.li_btime_nsec == 0, "fields past a short record are set");
	ASSERTF(data_is(&li.li_ea[LFU_EA_PATH], "dir/file", 8), "path");
}

static struct test_tbl_entry test_tbl[] = {
	TEST_REGISTER(1),
	TEST_REGISTER(2),
	TEST_REGISTER(3),
	TEST_REGISTER(4),
	TEST_REGISTER(5),
	TEST_REGISTER(6),
	TEST_REGISTER(7),
	TEST_REGISTER(8),
	TEST_REGISTER_END
};

static void usage(char *prog)
{
	fprintf(stderr,
		"Usage: %s [-e <test>] [-o <test>] [-w <file>]\n"
		"       -e  tests to skip\n"
		"       -o  tests to run\n"
		"       -w  save the bulk test2 writes\n",
		prog);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	int c;

	while ((c = getopt(argc, argv, "e:o:w:")) != -1) {
		switch (c) {
		case 'e':
			set_tests_to_skip(optarg, test_tbl);
			break;
		case 'o':
			set_tests_to_run(optarg, test_tbl);
			break;
		case 'w':
			write_file = optarg;
			break;
		default:
			usage(argv[0]);
		}
	}

	return run_test_tbl(test_tbl);
}
