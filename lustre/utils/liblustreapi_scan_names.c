// SPDX-License-Identifier: LGPL-2.1+
/*
 * Pathnames for a target scan, composed as the scan goes.
 *
 * An MDT object's trusted.link gives its name and parent, one step of its
 * path.  Every directory the scan meets goes into a cache, FID to (parent,
 * name), and a match is printed as soon as its parent chain reaches the
 * root.  A match whose chain does not reach it yet waits, keyed by the
 * first directory missing from the chain, and is tried again when that
 * directory arrives.  Most files follow their parent on the target, so the
 * wait list stays short; past a byte cap new waiters go to a temporary file
 * and are named at the end, when every directory of the target is known.
 *
 * What still waits at the end waits on a directory the scan never met: one
 * on another MDT, or one it could not read.  With a client mount, each such
 * directory is looked up once with fid2path and cached with its path.  For
 * an MDT the lookups run only after the scan, so a target scanned out of
 * service is never asked.  An OST object is named by its owner's
 * trusted.link, read through the mount, and the owner's parent through the
 * same cache.
 *
 * Single-threaded: llapi_find_device() runs its scan on one thread.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/xattr.h>
#include <linux/fs.h>	/* fscrypt's ioctls, in linux/fscrypt.h on newer */
#include <linux/lustre/lustre_fid.h>
#include <linux/lustre/lustre_idl.h>
#include <lustre/lustreapi.h>
#include "lustreapi_internal.h"

#define SN_DIR_MIN		1024
#define SN_WAIT_BUCKETS		4096
#define SN_MAX_DEPTH		4096	/* a cycle, not a path */

#define SN_ENC		0x1	/* its entries' names are encrypted */
#define SN_ABS		0x2	/* the name is its whole path from the root */
#define SN_DEAD		0x4	/* looked up, and it has no path */

struct sn_dir {
	struct lu_fid	sd_fid;
	struct lu_fid	sd_parent;
	size_t		sd_name;	/* offset into sn_arena */
	__u32		sd_len;
	__u32		sd_flags;
};

/* a match that waits for a directory; candidates, then the output */
struct sn_wait {
	struct sn_wait	*sw_next;
	size_t		 sw_size;	/* the whole allocation */
	size_t		 sw_bloblen;
	struct lu_fid	 sw_fid;	/* the object's own */
	unsigned int	 sw_ncand;
	char		 sw_data[];
};

/* the header as it is written to the spill file: no padding after it */
#define SN_WAIT_HDR	offsetof(struct sn_wait, sw_data)

/* an owner's trusted.link, kept: a file has several objects on an OST */
#define SN_OWNER_SLOTS	4096

struct sn_owner {
	struct lu_fid	 so_fid;
	char		*so_buf;
	ssize_t		 so_len;
};

struct sn_bucket {
	struct sn_bucket *sb_next;
	struct lu_fid	  sb_fid;	/* the directory waited for */
	struct sn_wait	 *sb_head;
	size_t		  sb_count;
};

struct scan_names {
	struct sn_dir	 *sn_tab;
	size_t		  sn_cap;	/* power of two */
	size_t		  sn_used;
	char		 *sn_arena;
	size_t		  sn_arena_used;
	size_t		  sn_arena_cap;

	struct sn_bucket *sn_wait[SN_WAIT_BUCKETS];
	size_t		  sn_wait_bytes;
	size_t		  sn_wait_max;
	FILE		 *sn_spill;

	int		  sn_mnt_fd;	/* -1: no lookups */
	/* where paths start: the filesystem root, or a fileset mount's */
	struct lu_fid	  sn_root;
	__u32		  sn_root_flags;	/* SN_ENC, from the client */
	bool		  sn_root_known;	/* the client answered */
	const char	 *sn_mnt_path;
	const char	 *sn_prefix;	/* "." or the mount point */

	scan_names_render_t sn_render;
	void		 *sn_data;

	__u64		  sn_nameless;
	__u64		  sn_outside;	/* not below a fileset mount's root */
	int		  sn_err;	/* the first failure not "no name" */
	int		  sn_lost;	/* why a waiting match was lost */
	struct sn_owner	  sn_owners[SN_OWNER_SLOTS];
};

static int sn_settle_now(struct scan_names *sn, struct sn_wait *w);
static int sn_policy_enc(struct scan_names *sn, const struct lu_fid *fid);
static int sn_emit_lookup(struct scan_names *sn, const struct lu_fid *owner,
			  bool all, const void *blob, size_t bloblen);
static int sn_settle(struct scan_names *sn, struct sn_wait *w);

static size_t sn_hash(const struct lu_fid *f)
{
	__u64 h = f->f_seq * 0x9e3779b97f4a7c15ULL + f->f_oid;

	h ^= h >> 29;
	h *= 0xbf58476d1ce4e5b9ULL;
	h ^= h >> 32;

	return (size_t)h;
}

static bool sn_fid_eq(const struct lu_fid *a, const struct lu_fid *b)
{
	return a->f_seq == b->f_seq && a->f_oid == b->f_oid;
}

static bool sn_fid_zero(const struct lu_fid *f)
{
	return f->f_seq == 0 && f->f_oid == 0;
}

static struct sn_dir *sn_get(const struct scan_names *sn,
			     const struct lu_fid *fid)
{
	size_t i = sn_hash(fid) & (sn->sn_cap - 1);

	while (!sn_fid_zero(&sn->sn_tab[i].sd_fid)) {
		if (sn_fid_eq(&sn->sn_tab[i].sd_fid, fid))
			return &sn->sn_tab[i];
		i = (i + 1) & (sn->sn_cap - 1);
	}

	return NULL;
}

static int sn_grow(struct scan_names *sn)
{
	size_t cap = sn->sn_cap * 2;
	struct sn_dir *tab = calloc(cap, sizeof(*tab));
	size_t i;

	if (tab == NULL)
		return -ENOMEM;
	for (i = 0; i < sn->sn_cap; i++) {
		size_t j;

		if (sn_fid_zero(&sn->sn_tab[i].sd_fid))
			continue;
		j = sn_hash(&sn->sn_tab[i].sd_fid) & (cap - 1);
		while (!sn_fid_zero(&tab[j].sd_fid))
			j = (j + 1) & (cap - 1);
		tab[j] = sn->sn_tab[i];
	}
	free(sn->sn_tab);
	sn->sn_tab = tab;
	sn->sn_cap = cap;

	return 0;
}

static int sn_arena_add(struct scan_names *sn, const char *s, size_t len,
			size_t *off)
{
	if (sn->sn_arena_used + len + 1 > sn->sn_arena_cap) {
		size_t cap = sn->sn_arena_cap ? sn->sn_arena_cap * 2 : 65536;
		char *a;

		while (cap < sn->sn_arena_used + len + 1)
			cap *= 2;
		a = realloc(sn->sn_arena, cap);
		if (a == NULL)
			return -ENOMEM;
		sn->sn_arena = a;
		sn->sn_arena_cap = cap;
	}
	*off = sn->sn_arena_used;
	memcpy(sn->sn_arena + *off, s, len);
	sn->sn_arena[*off + len] = '\0';
	sn->sn_arena_used += len + 1;

	return 0;
}

static int sn_put(struct scan_names *sn, const struct lu_fid *fid,
		  const struct lu_fid *parent, const char *name, __u32 flags)
{
	size_t len = strlen(name);
	struct sn_dir *d;
	size_t i;
	int rc;

	if (sn_get(sn, fid) != NULL)
		return 0;	/* the same directory twice */
	if ((sn->sn_used + 1) * 4 >= sn->sn_cap * 3) {
		rc = sn_grow(sn);
		if (rc < 0)
			return rc;
	}

	i = sn_hash(fid) & (sn->sn_cap - 1);
	while (!sn_fid_zero(&sn->sn_tab[i].sd_fid))
		i = (i + 1) & (sn->sn_cap - 1);
	d = &sn->sn_tab[i];
	rc = sn_arena_add(sn, name, len, &d->sd_name);
	if (rc < 0)
		return rc;
	d->sd_fid = *fid;
	if (parent != NULL)
		d->sd_parent = *parent;
	d->sd_len = len;
	d->sd_flags = flags;
	sn->sn_used++;

	return 0;
}

#define SN_CRYPTO_BLOCK		16	/* LL_CRYPTO_BLOCK_SIZE */

/* base64url (RFC 4648 section 5), as fscrypt shows a no-key name */
static char sn_b64(unsigned int v)
{
	if (v < 26)
		return 'A' + v;
	if (v < 52)
		return 'a' + v - 26;
	if (v < 62)
		return '0' + v - 52;
	return v == 62 ? '-' : '_';
}

static size_t sn_base64url(const __u8 *src, size_t len, char *dst)
{
	unsigned int bits = 0;
	__u32 ac = 0;
	size_t n = 0;
	size_t i;

	for (i = 0; i < len; i++) {
		ac = (ac << 8) | src[i];
		bits += 8;
		while (bits >= 6) {
			bits -= 6;
			dst[n++] = sn_b64((ac >> bits) & 0x3f);
		}
	}
	if (bits != 0)
		dst[n++] = sn_b64((ac << (6 - bits)) & 0x3f);

	return n;
}

/*
 * An encrypted name as a client without the key shows it, from the name the
 * MDT holds, as ll_fname_disk_to_usr() builds it: the stored name is the
 * ciphertext with '=' escapes (critical_encode()), shown in base64url, and
 * one longer than two blocks as '+' and the object's FID with a block of the
 * ciphertext.  A client with filename_enc_use_old_base64 shows another form.
 *
 * Return: the length written to @out, which holds NAME_MAX + 1.
 */
static size_t sn_enc_name(const char *name, size_t len,
			  const struct lu_fid *fid, char *out)
{
	__u8 c[NAME_MAX + 1];
	size_t n = 0;
	size_t i;

	for (i = 0; i < len && n < sizeof(c); i++) {
		if (name[i] == '=' && i + 1 < len)
			c[n++] = (__u8)name[++i] - 64;
		else
			c[n++] = (__u8)name[i];
	}

	if (n > SN_CRYPTO_BLOCK * 2) {
		__u8 digest[sizeof(*fid) + SN_CRYPTO_BLOCK];
		size_t off = (n - SN_CRYPTO_BLOCK - 1) & ~(SN_CRYPTO_BLOCK - 1);

		memcpy(digest, fid, sizeof(*fid));
		memcpy(digest + sizeof(*fid), c + off, SN_CRYPTO_BLOCK);
		out[0] = '+';
		return 1 + sn_base64url(digest, sizeof(digest), out + 1);
	}

	return sn_base64url(c, n, out);
}

/* prepend @len bytes at @at in @buf, with a '/' before them */
static int sn_prepend(char *buf, size_t *at, const char *s, size_t len,
		      bool slash)
{
	if (len + (slash ? 1 : 0) > *at)
		return -ERANGE;
	*at -= len;
	memcpy(buf + *at, s, len);
	if (slash)
		buf[--*at] = '/';

	return 0;
}

/*
 * The path of @name, the object @fid, in the directory @parent, from the
 * cache.
 *
 * Built back to front.  A component is rendered once its own directory is
 * known, since that directory says whether the names in it are encrypted.
 *
 * Return: 0 and the path in @buf; -EAGAIN and the first directory the cache
 * lacks in @missing; -ENOENT for a path under a directory that was looked up
 * and is gone; -ERANGE; -ELOOP.
 */
static int sn_compose(const struct scan_names *sn, const struct lu_fid *parent,
		      const char *name, const struct lu_fid *fid, char *buf,
		      size_t buflen, struct lu_fid *missing)
{
	char enc[NAME_MAX + 2];
	const char *cname = name;	/* the component not yet placed */
	size_t clen = strlen(name);
	struct lu_fid cfid = *fid;
	size_t at = buflen;
	unsigned int depth = 0;
	struct lu_fid cur = *parent;
	const struct sn_dir *d;
	int rc;

	if (buflen < 2)
		return -ERANGE;
	/* a damaged link: zero marks an empty slot, so it could never arrive */
	if (!fid_is_sane(parent))
		return -ENOENT;
	buf[--at] = '\0';

	/* the fileset mount's own root is the mount point itself */
	if (sn_fid_eq(fid, &sn->sn_root)) {
		rc = sn_prepend(buf, &at, sn->sn_prefix,
				strlen(sn->sn_prefix), false);
		if (rc < 0)
			return rc;
		memmove(buf, buf + at, buflen - at);
		return 0;
	}

	for (;;) {
		bool encname = false;

		/* above a fileset mount's root: not in its view */
		if (!sn_fid_eq(&cur, &sn->sn_root) &&
		    sn_fid_eq(&cur, &LU_ROOT_FID))
			return -ESRCH;
		if (sn_fid_eq(&cur, &sn->sn_root)) {
			/*
			 * The root's own flags: names below it may be
			 * encrypted.  A client may not answer for a locked
			 * root, so then the scan has to meet it first.
			 */
			d = sn_get(sn, &cur);
			if (d == NULL && !sn->sn_root_known) {
				*missing = cur;
				return -EAGAIN;
			}
			encname = (d != NULL ? d->sd_flags :
					     sn->sn_root_flags) & SN_ENC;
			d = NULL;
		} else {
			if (++depth > SN_MAX_DEPTH)
				return -ELOOP;
			d = sn_get(sn, &cur);
			if (d == NULL) {
				*missing = cur;
				return -EAGAIN;
			}
			if (d->sd_flags & SN_DEAD)
				return -ENOENT;
			encname = d->sd_flags & SN_ENC;
			if (!(d->sd_flags & SN_ABS) && d->sd_len != 0 &&
			    !fid_is_sane(&d->sd_parent))
				return -ENOENT;
		}

		/* the component, now that its directory is known */
		if (cname != NULL) {
			if (encname) {
				clen = sn_enc_name(cname, clen, &cfid, enc);
				cname = enc;
			}
			rc = sn_prepend(buf, &at, cname, clen, true);
			if (rc < 0)
				return rc;
		}
		if (d == NULL)
			break;		/* the root */
		if (d->sd_flags & SN_ABS) {
			/* its whole path, as the client shows it */
			rc = sn_prepend(buf, &at, sn->sn_arena + d->sd_name,
					d->sd_len, false);
			if (rc < 0)
				return rc;
			break;
		}
		/* a striped directory's shard: a step, not a component */
		if (d->sd_len == 0) {
			cname = NULL;
		} else {
			cname = sn->sn_arena + d->sd_name;
			clen = d->sd_len;
			cfid = d->sd_fid;
		}
		cur = d->sd_parent;
	}

	rc = sn_prepend(buf, &at, sn->sn_prefix, strlen(sn->sn_prefix), false);
	if (rc < 0)
		return rc;
	memmove(buf, buf + at, buflen - at);

	return 0;
}

static void sn_failed(struct scan_names *sn, int rc)
{
	if (rc == -ESRCH)
		sn->sn_outside++;
	else if (rc == -ENOENT || rc == -ENODATA || rc == -EINVAL)
		sn->sn_nameless++;
	else if (sn->sn_err == 0)
		sn->sn_err = rc;
}

/* the candidates of @w, one at a time */
static const char *sn_cand(const struct sn_wait *w, size_t *off,
			   struct lu_fid *parent)
{
	const char *p = w->sw_data + *off;
	size_t len;

	memcpy(parent, p, sizeof(*parent));
	len = strlen(p + sizeof(*parent));
	*off += sizeof(*parent) + len + 1;

	return p + sizeof(*parent);
}

static const char *sn_blob(const struct sn_wait *w)
{
	return w->sw_data + w->sw_size - SN_WAIT_HDR - w->sw_bloblen;
}

/*
 * Print @w under the first candidate whose path composes.
 *
 * Return: 0 printed; -EAGAIN with *missing, the directory the first
 * waiting candidate needs; else the first candidate's failure.
 */
static int sn_try(struct scan_names *sn, const struct sn_wait *w,
		  struct lu_fid *missing)
{
	char path[PATH_MAX];
	bool waits = false;
	int first = 0;
	size_t off = 0;
	unsigned int i;

	for (i = 0; i < w->sw_ncand; i++) {
		struct lu_fid parent;
		struct lu_fid m;
		const char *name = sn_cand(w, &off, &parent);
		int rc;

		rc = sn_compose(sn, &parent, name, &w->sw_fid, path,
				sizeof(path), &m);
		if (rc == 0) {
			sn->sn_render(sn_blob(w), w->sw_bloblen, path,
				      sn->sn_data);
			return 0;
		}
		if (rc == -EAGAIN && !waits) {
			waits = true;
			*missing = m;
		}
		if (first == 0 || first == -ENOENT)
			first = rc;
	}

	return waits ? -EAGAIN : first;
}

static struct sn_bucket **sn_bucket_slot(struct scan_names *sn,
					 const struct lu_fid *fid)
{
	struct sn_bucket **b = &sn->sn_wait[sn_hash(fid) % SN_WAIT_BUCKETS];

	while (*b != NULL && !sn_fid_eq(&(*b)->sb_fid, fid))
		b = &(*b)->sb_next;

	return b;
}

/*
 * @w to the temporary file, freed either way.  A failure is final: the scan
 * stops rather than go on with matches it could not keep.
 */
static int sn_spill(struct scan_names *sn, struct sn_wait *w)
{
	int rc = 0;

	if (sn->sn_spill == NULL) {
		sn->sn_spill = tmpfile();
		if (sn->sn_spill == NULL)
			rc = -errno;
	}
	if (rc == 0 && fwrite(w, w->sw_size, 1, sn->sn_spill) != 1)
		rc = -errno ?: -EIO;
	free(w);
	if (rc < 0) {
		llapi_error(LLAPI_MSG_ERROR, rc,
			    "cannot keep the matches still waiting for a directory in a temporary file");
		if (sn->sn_lost == 0)
			sn->sn_lost = rc;
		return rc;
	}

	return 0;
}

/* a record read back from the temporary file is whole and in bounds */
static bool sn_wait_valid(const struct sn_wait *w)
{
	const char *p = w->sw_data;
	const char *end;
	unsigned int i;

	if (w->sw_size < SN_WAIT_HDR + w->sw_bloblen)
		return false;
	end = w->sw_data + w->sw_size - SN_WAIT_HDR - w->sw_bloblen;
	for (i = 0; i < w->sw_ncand; i++) {
		p += sizeof(struct lu_fid);
		if (p >= end)
			return false;
		p = memchr(p, '\0', end - p);
		if (p == NULL)
			return false;
		p++;
	}

	return true;
}

/* @w waits for @fid; it is the cache's to free from here on */
static int sn_wait(struct scan_names *sn, struct sn_wait *w,
		   const struct lu_fid *fid)
{
	struct sn_bucket **slot;

	if (sn->sn_wait_bytes + w->sw_size > sn->sn_wait_max)
		return sn_spill(sn, w);

	slot = sn_bucket_slot(sn, fid);
	if (*slot == NULL) {
		*slot = calloc(1, sizeof(**slot));
		if (*slot == NULL) {
			free(w);
			return -ENOMEM;
		}
		(*slot)->sb_fid = *fid;
	}
	w->sw_next = (*slot)->sb_head;
	(*slot)->sb_head = w;
	(*slot)->sb_count++;
	sn->sn_wait_bytes += w->sw_size;

	return 0;
}

/* @w is done with, one way or another */
static int sn_settle(struct scan_names *sn, struct sn_wait *w)
{
	struct lu_fid missing;
	int rc = sn_try(sn, w, &missing);

	if (rc == -EAGAIN)
		return sn_wait(sn, w, &missing);
	if (rc != 0)
		sn_failed(sn, rc);
	free(w);

	return 0;
}

/* @fid is in the cache now: what waited for it can go on */
static int sn_wake(struct scan_names *sn, const struct lu_fid *fid)
{
	struct sn_bucket **slot = sn_bucket_slot(sn, fid);
	struct sn_bucket *b = *slot;
	struct sn_wait *w;
	int rc = 0;

	if (b == NULL)
		return 0;
	*slot = b->sb_next;
	w = b->sb_head;
	free(b);

	while (w != NULL) {
		struct sn_wait *next = w->sw_next;
		int rc2;

		sn->sn_wait_bytes -= w->sw_size;
		rc2 = sn_settle(sn, w);
		if (rc2 < 0 && rc == 0)
			rc = rc2;
		w = next;
	}

	return rc;
}

int scan_names_init(struct scan_names **snp, int mnt_fd, const char *mnt_path,
		    const struct lu_fid *root, size_t wait_max,
		    scan_names_render_t render, void *data)
{
	struct scan_names *sn = calloc(1, sizeof(*sn));

	if (sn == NULL)
		return -ENOMEM;
	sn->sn_tab = calloc(SN_DIR_MIN, sizeof(*sn->sn_tab));
	if (sn->sn_tab == NULL) {
		free(sn);
		return -ENOMEM;
	}
	sn->sn_cap = SN_DIR_MIN;
	sn->sn_mnt_fd = mnt_fd;
	sn->sn_mnt_path = mnt_path;
	sn->sn_root = root != NULL ? *root : LU_ROOT_FID;
	/* a fileset mount's root may itself encrypt the names below it */
	sn->sn_root_known = sn_fid_eq(&sn->sn_root, &LU_ROOT_FID);
	if (!sn->sn_root_known && mnt_fd >= 0 && mnt_path != NULL) {
		int enc = sn_policy_enc(sn, &sn->sn_root);

		sn->sn_root_known = enc >= 0;
		sn->sn_root_flags = enc > 0 ? SN_ENC : 0;
	}
	sn->sn_prefix = mnt_path != NULL ? mnt_path : ".";
	sn->sn_wait_max = wait_max;
	sn->sn_render = render;
	sn->sn_data = data;
	*snp = sn;

	return 0;
}

/* the filesystem root: the prefix itself, or outside a fileset mount */
int scan_names_print_root(struct scan_names *sn, const void *blob,
			  size_t len)
{
	if (!sn_fid_eq(&sn->sn_root, &LU_ROOT_FID)) {
		sn->sn_outside++;
		return 0;
	}
	sn->sn_render(blob, len, sn->sn_prefix, sn->sn_data);

	return 0;
}

/*
 * A directory the scan met, with whether its entries' names are encrypted;
 * a shard has the name "".
 */
int scan_names_dir(struct scan_names *sn, const struct lu_fid *fid,
		   const struct lu_fid *parent, const char *name,
		   bool names_enc)
{
	int rc;

	rc = sn_put(sn, fid, parent, name, names_enc ? SN_ENC : 0);
	if (rc < 0)
		return rc;

	return sn_wake(sn, fid);
}

/*
 * An OST object's name is its owner's: the owner's trusted.link, read
 * through the mount without opening it, and kept for the owner's next
 * object.  *bufp is the cache's.
 */
static int sn_owner_linkea(struct scan_names *sn, const struct lu_fid *owner,
			   char **bufp, ssize_t *lenp)
{
	struct sn_owner *o = &sn->sn_owners[sn_hash(owner) % SN_OWNER_SLOTS];
	char path[PATH_MAX];
	ssize_t len = 4096;
	char *buf;

	if (sn->sn_mnt_path == NULL)
		return -ENOENT;
	if (o->so_buf != NULL && sn_fid_eq(&o->so_fid, owner)) {
		*bufp = o->so_buf;
		*lenp = o->so_len;
		return 0;
	}
	snprintf(path, sizeof(path), "%s/.lustre/fid/"DFID, sn->sn_mnt_path,
		 PFID(owner));
	for (;;) {
		buf = malloc(len);
		if (buf == NULL)
			return -ENOMEM;
		len = lgetxattr(path, XATTR_NAME_LINK, buf, len);
		if (len >= 0)
			break;
		free(buf);
		/* many hardlinks: ask for the size, then read again */
		if (errno != ERANGE)
			return -errno;
		len = lgetxattr(path, XATTR_NAME_LINK, NULL, 0);
		if (len <= 0)
			return len < 0 ? -errno : -ENODATA;
	}
	free(o->so_buf);
	o->so_fid = *owner;
	o->so_buf = buf;
	o->so_len = len;
	*bufp = buf;
	*lenp = len;

	return 0;
}

/* one output, under the first of @c whose path composes */
static int sn_emit_one(struct scan_names *sn, const struct lu_fid *fid,
		       const struct scan_name_cand *c, unsigned int nc,
		       const void *blob, size_t bloblen, bool now)
{
	size_t size = SN_WAIT_HDR + bloblen;
	struct sn_wait *w;
	unsigned int i;
	char *p;

	for (i = 0; i < nc; i++)
		size += sizeof(struct lu_fid) + strlen(c[i].snc_name) + 1;
	w = malloc(size);
	if (w == NULL)
		return -ENOMEM;
	w->sw_next = NULL;
	w->sw_size = size;
	w->sw_bloblen = bloblen;
	w->sw_fid = *fid;
	w->sw_ncand = nc;
	p = w->sw_data;
	for (i = 0; i < nc; i++) {
		size_t len = strlen(c[i].snc_name) + 1;

		memcpy(p, &c[i].snc_parent, sizeof(struct lu_fid));
		p += sizeof(struct lu_fid);
		memcpy(p, c[i].snc_name, len);
		p += len;
	}
	memcpy(p, blob, bloblen);

	return now ? sn_settle_now(sn, w) : sn_settle(sn, w);
}

/*
 * An OST object: its owner's first link, or every one.  The owner's names
 * are all on an MDT, so what they lack is looked up now.
 */
static int sn_emit_owner(struct scan_names *sn, const struct lu_fid *owner,
			 bool all, const void *blob, size_t bloblen)
{
	struct scan_name_cand *c;
	char *names;
	size_t off = 0;
	unsigned int nc = 0;
	char *buf;
	ssize_t len;
	int nr;
	int i;
	int rc;

	rc = sn_owner_linkea(sn, owner, &buf, &len);
	/*
	 * A trusted xattr is ENODATA, not EPERM, to a caller without
	 * CAP_SYS_ADMIN, and a fileset mount may have no .lustre: fid2path
	 * says which it is, and names it if it can.
	 */
	if (rc == -ENODATA || rc == -ENOENT)
		return sn_emit_lookup(sn, owner, all, blob, bloblen);
	if (rc < 0)
		return rc;
	nr = scan_linkea_entry(buf, len, 0, NULL, 0, NULL);
	if (nr == 0)
		return -ENODATA;
	c = calloc(nr, sizeof(*c));
	names = malloc((size_t)nr * (NAME_MAX + 1));
	if (c == NULL || names == NULL) {
		free(c);
		free(names);
		return -ENOMEM;
	}
	/* every link, tried in turn as on an MDT, or each printed */
	for (i = 0; i < nr; i++) {
		char *name = names + (size_t)nc * (NAME_MAX + 1);

		if (scan_linkea_next(buf, len, &off, name, NAME_MAX + 1,
				     &c[nc].snc_parent) == 0)
			break;
		if (name[0] == '\0')
			continue;
		c[nc++].snc_name = name;
	}
	if (nc == 0)
		rc = -ENODATA;
	else if (!all)
		rc = sn_emit_one(sn, owner, c, nc, blob, bloblen, true);
	for (i = 0; all && i < (int)nc && rc == 0; i++)
		rc = sn_emit_one(sn, owner, &c[i], 1, blob, bloblen, true);
	free(c);
	free(names);

	return rc;
}

/*
 * An encrypted OST object: its owner's names are rendered by the client,
 * which has the policy and the encoder, so the owner is looked up whole.
 */
static int sn_emit_lookup(struct scan_names *sn, const struct lu_fid *owner,
			  bool all, const void *blob, size_t bloblen)
{
	char path[PATH_MAX];
	char full[PATH_MAX * 2];
	long long recno = -1;
	int linkno = 0;
	int rc;

	if (sn->sn_mnt_fd < 0)
		return -ENOENT;
	do {
		int prev = linkno;

		rc = llapi_fid2path_at(sn->sn_mnt_fd, owner, path,
				       sizeof(path), &recno, &linkno);
		if (rc < 0)
			return prev == 0 ? rc : 0;
		snprintf(full, sizeof(full), "%s/%s", sn->sn_prefix, path);
		sn->sn_render(blob, bloblen, full, sn->sn_data);
		/* the same link again: there are no more */
		if (linkno == prev)
			break;
	} while (all);

	return 0;
}

/*
 * Something to print: its candidate names, in the order to try them, and
 * the output, which the render callback turns into text once a path is
 * known.  It is printed once, under the first name that can be placed, or
 * with @all once under each.  An OST object's single candidate has a NULL
 * name and its owner's FID as the parent.
 *
 * Return: 0 when printed or waiting, else why it has no name.
 */
int scan_names_emit(struct scan_names *sn, const struct lu_fid *fid,
		    const struct scan_name_cand *c, unsigned int nc,
		    unsigned int flags, const void *blob, size_t bloblen)
{
	bool all = flags & SCAN_NAMES_ALL;
	unsigned int i;
	int rc = 0;

	if (nc == 0)
		return -ENOENT;
	if (c[0].snc_name == NULL && (flags & SCAN_NAMES_ENC))
		return sn_emit_lookup(sn, &c[0].snc_parent, all, blob,
				      bloblen);
	if (c[0].snc_name == NULL)
		return sn_emit_owner(sn, &c[0].snc_parent, all, blob, bloblen);
	if (!all)
		return sn_emit_one(sn, fid, c, nc, blob, bloblen, false);
	for (i = 0; i < nc && rc == 0; i++)
		rc = sn_emit_one(sn, fid, &c[i], 1, blob, bloblen, false);

	return rc;
}

/*
 * Whether a directory the scan did not meet has encrypted names: the
 * client's answer, from the policy it holds.
 *
 * Return: 1 encrypted, 0 not, or a negative errno when the client could not
 * be asked, as for a locked directory.
 */
static int sn_policy_enc(struct scan_names *sn, const struct lu_fid *fid)
{
#ifdef FS_IOC_GET_ENCRYPTION_POLICY_EX
	struct fscrypt_get_policy_ex_arg arg = {
		.policy_size = sizeof(arg.policy),
	};
	char path[PATH_MAX];
	__u8 mode = 0;
	int rc = 0;
	int fd;

	snprintf(path, sizeof(path), "%s/.lustre/fid/"DFID, sn->sn_mnt_path,
		 PFID(fid));
	fd = open(path, O_RDONLY | O_DIRECTORY | O_NOCTTY);
	if (fd < 0)
		return -errno;
	if (ioctl(fd, FS_IOC_GET_ENCRYPTION_POLICY_EX, &arg) == 0)
		mode = arg.policy.version == FSCRYPT_POLICY_V1 ?
		       arg.policy.v1.filenames_encryption_mode :
		       arg.policy.v2.filenames_encryption_mode;
	else if (errno != ENODATA)	/* ENODATA: not encrypted */
		rc = -errno;
	close(fd);

	return rc < 0 ? rc : mode != 0;
#else
	return -EOPNOTSUPP;
#endif
}

/* one fid2path for a directory the scan did not meet, then cached */
static int sn_lookup_dir(struct scan_names *sn, const struct lu_fid *fid)
{
	char path[PATH_MAX];
	char abs[PATH_MAX + 1];
	int rc;

	rc = llapi_fid2path_at(sn->sn_mnt_fd, fid, path, sizeof(path), NULL,
			       NULL);
	/* gone, or no path: final.  Anything else may pass, so not kept */
	if (rc == -ENOENT || rc == -EINVAL || rc == -ENODATA)
		return sn_put(sn, fid, NULL, "", SN_DEAD);
	if (rc < 0)
		return rc;
	snprintf(abs, sizeof(abs), "/%s", path);

	return sn_put(sn, fid, NULL, abs,
		      SN_ABS | (sn_policy_enc(sn, fid) > 0 ? SN_ENC : 0));
}

/*
 * @w, with every directory it lacks looked up through the mount.
 *
 * Return: 0, or a lookup's failure that was not "no name", for the caller
 * to stop on: an MDT that does not answer would leave the rest unnamed too.
 */
static int sn_settle_now(struct scan_names *sn, struct sn_wait *w)
{
	struct lu_fid missing;
	int rc;

	while ((rc = sn_try(sn, w, &missing)) == -EAGAIN &&
	       sn->sn_mnt_fd >= 0) {
		rc = sn_lookup_dir(sn, &missing);
		if (rc < 0) {
			free(w);
			return rc;
		}
	}
	if (rc == -EAGAIN)
		rc = -ENOENT;
	if (rc != 0)
		sn_failed(sn, rc);
	free(w);

	return 0;
}

/*
 * The scan is over, so every directory of the target is in the cache.  What
 * still waits, waits on one it never met: look it up through the mount if
 * there is one, or count it as having no name.
 *
 * Return: 0, or the first failure that was not "no name".
 */
int scan_names_finish(struct scan_names *sn, __u64 *nameless, __u64 *outside)
{
	bool more = true;
	unsigned int i;
	int rc = 0;

	/*
	 * Each directory is looked up, or marked as having no path, once:
	 * what it then wakes either prints or waits on another, so the order
	 * does not matter, and table order keeps this linear.
	 */
	while (more && rc == 0) {
		more = false;
		for (i = 0; i < SN_WAIT_BUCKETS && rc == 0; i++) {
			while (sn->sn_wait[i] != NULL && rc == 0) {
				struct lu_fid fid = sn->sn_wait[i]->sb_fid;

				if (sn->sn_mnt_fd >= 0)
					rc = sn_lookup_dir(sn, &fid);
				else
					rc = sn_put(sn, &fid, NULL, "",
						    SN_DEAD);
				if (rc == 0)
					rc = sn_wake(sn, &fid);
				more = true;
			}
		}
	}

	/* a write that only failed when flushed must not be read back */
	if (rc == 0 && sn->sn_spill != NULL &&
	    (fflush(sn->sn_spill) != 0 || ferror(sn->sn_spill)))
		rc = -EIO;
	if (rc == 0 && sn->sn_spill != NULL) {
		struct sn_wait head;

		rewind(sn->sn_spill);
		while (fread(&head, SN_WAIT_HDR, 1, sn->sn_spill) == 1) {
			struct sn_wait *w;

			if (head.sw_size < SN_WAIT_HDR ||
			    head.sw_size > SN_WAIT_HDR + (64UL << 20)) {
				rc = -EIO;
				break;
			}
			w = malloc(head.sw_size);
			if (w == NULL) {
				rc = -ENOMEM;
				break;
			}
			memcpy(w, &head, SN_WAIT_HDR);
			if (fread(w->sw_data, head.sw_size - SN_WAIT_HDR, 1,
				  sn->sn_spill) != 1 || !sn_wait_valid(w)) {
				free(w);
				rc = -EIO;
				break;
			}
			rc = sn_settle_now(sn, w);
			if (rc < 0)
				break;
		}
		if (rc == 0 && ferror(sn->sn_spill))
			rc = -EIO;
	}

	if (nameless != NULL)
		*nameless += sn->sn_nameless;
	if (outside != NULL)
		*outside += sn->sn_outside;
	if (rc == 0)
		rc = sn->sn_err;

	return rc;
}

/* why a match could not be kept, which ends the scan; 0 if none was lost */
int scan_names_lost(const struct scan_names *sn)
{
	return sn == NULL ? 0 : sn->sn_lost;
}

/* whether a match still waits for a directory the scan did not meet */
bool scan_names_waiting(const struct scan_names *sn)
{
	return sn->sn_wait_bytes != 0 || sn->sn_spill != NULL;
}

/*
 * Look up the directories the scan never met through @mnt_fd, a mount of the
 * whole filesystem, while paths still start at ".".
 */
void scan_names_lookup_mount(struct scan_names *sn, int mnt_fd,
			     const char *mnt_path)
{
	sn->sn_mnt_fd = mnt_fd;
	sn->sn_mnt_path = mnt_path;
}

void scan_names_free(struct scan_names *sn)
{
	unsigned int i;

	if (sn == NULL)
		return;
	for (i = 0; i < SN_WAIT_BUCKETS; i++) {
		while (sn->sn_wait[i] != NULL) {
			struct sn_bucket *b = sn->sn_wait[i];

			sn->sn_wait[i] = b->sb_next;
			while (b->sb_head != NULL) {
				struct sn_wait *w = b->sb_head;

				b->sb_head = w->sw_next;
				free(w);
			}
			free(b);
		}
	}
	if (sn->sn_spill != NULL)
		fclose(sn->sn_spill);
	for (i = 0; i < SN_OWNER_SLOTS; i++)
		free(sn->sn_owners[i].so_buf);
	free(sn->sn_tab);
	free(sn->sn_arena);
	free(sn);
}
