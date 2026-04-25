/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2017, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 */

#ifndef _LUSTRE_COMPAT_H
#define _LUSTRE_COMPAT_H

#include <linux/aio.h>
#include <lustre_compat/linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/posix_acl_xattr.h>
#include <linux/bio.h>
#include <linux/xattr.h>
#include <linux/workqueue.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/pagevec.h>
#include <linux/workqueue.h>
#include <lustre_compat/linux/shrinker.h>
#include <lustre_compat/linux/xarray.h>
#include <obd_support.h>

#ifdef HAVE_4ARGS_VFS_SYMLINK
#define ll_vfs_symlink(dir, dentry, mnt, path, mode) \
		       vfs_symlink(dir, dentry, path, mode)
#else
#define ll_vfs_symlink(dir, dentry, mnt, path, mode) \
		       vfs_symlink(dir, dentry, path)
#endif

#ifdef HAVE_BVEC_ITER
#define bio_idx(bio)			(bio->bi_iter.bi_idx)
#define bio_set_sector(bio, sector)	(bio->bi_iter.bi_sector = sector)
#define bvl_to_page(bvl)		(bvl->bv_page)
#else
#define bio_idx(bio)			(bio->bi_idx)
#define bio_set_sector(bio, sector)	(bio->bi_sector = sector)
#define bio_sectors(bio)		((bio)->bi_size >> 9)
#define bvl_to_page(bvl)		(bvl->bv_page)
#endif

#ifdef HAVE_BVEC_ITER
#define bio_start_sector(bio) (bio->bi_iter.bi_sector)
#else
#define bio_start_sector(bio) (bio->bi_sector)
#endif

#ifdef HAVE_BI_BDEV
# define bio_get_dev(bio)	((bio)->bi_bdev)
# define bio_get_disk(bio)	(bio_get_dev(bio)->bd_disk)
# define bio_get_queue(bio)	bdev_get_queue(bio_get_dev(bio))

# ifndef HAVE_BIO_SET_DEV
#  define bio_set_dev(bio, bdev) (bio_get_dev(bio) = (bdev))
# endif
#else
# define bio_get_disk(bio)	((bio)->bi_disk)
# define bio_get_queue(bio)	(bio_get_disk(bio)->queue)
#endif

#ifndef HAVE_BI_OPF
#define bi_opf bi_rw
#endif

#ifndef SLAB_MEM_SPREAD
#define SLAB_MEM_SPREAD		0
#endif

#ifdef HAVE_STRUCT_FILE_LOCK_CORE
#define C_FLC_TYPE	c.flc_type
#define C_FLC_PID	c.flc_pid
#define C_FLC_FILE	c.flc_file
#define C_FLC_FLAGS	c.flc_flags
#define C_FLC_OWNER	c.flc_owner
#else
#define C_FLC_TYPE	fl_type
#define C_FLC_PID	fl_pid
#define C_FLC_FILE	fl_file
#define C_FLC_FLAGS	fl_flags
#define C_FLC_OWNER	fl_owner
#endif

static inline struct bio *cfs_bio_alloc(struct block_device *bdev,
					unsigned short nr_vecs,
					__u32 op, gfp_t gfp_mask)
{
	struct bio *bio;
#ifdef HAVE_BIO_ALLOC_WITH_BDEV
	bio = bio_alloc(bdev, nr_vecs, op, gfp_mask);
#else
	bio = bio_alloc(gfp_mask, nr_vecs);
	if (bio) {
		bio_set_dev(bio, bdev);
		bio->bi_opf = op;
	}
#endif /* HAVE_BIO_ALLOC_WITH_BDEV */
	return bio;
}

#ifndef HAVE_D_IN_LOOKUP
static inline int d_in_lookup(struct dentry *dentry)
{
	return false;
}
#endif

#ifdef HAVE_DENTRY_D_CHILDREN
#define d_no_children(dentry)	(hlist_empty(&(dentry)->d_children))
#define d_for_each_child(child, dentry) \
	hlist_for_each_entry((child), &(dentry)->d_children, d_sib)
#else
#define d_no_children(dentry)	(list_empty(&(dentry)->d_subdirs))
#define d_for_each_child(child, dentry) \
	list_for_each_entry((child), &(dentry)->d_subdirs, d_child)
#endif

#ifndef HAVE_VM_FAULT_T
#define vm_fault_t int
#endif

#ifndef HAVE_FOP_ITERATE_SHARED
#define iterate_shared iterate
#endif

#ifdef HAVE_OLDSIZE_TRUNCATE_PAGECACHE
#define ll_truncate_pagecache(inode, size) truncate_pagecache(inode, 0, size)
#else
#define ll_truncate_pagecache(inode, size) truncate_pagecache(inode, size)
#endif

#ifdef HAVE_VFS_RENAME_5ARGS
#define ll_vfs_rename(a, b, c, d) vfs_rename(a, b, c, d, NULL)
#elif defined HAVE_VFS_RENAME_6ARGS
#define ll_vfs_rename(a, b, c, d) vfs_rename(a, b, c, d, NULL, 0)
#else
#define ll_vfs_rename(a, b, c, d) vfs_rename(a, b, c, d)
#endif

#ifdef HAVE_USER_NAMESPACE_ARG
#define vfs_unlink(ns, dir, de) vfs_unlink(ns, dir, de, NULL)
#elif defined HAVE_VFS_UNLINK_3ARGS
#define vfs_unlink(ns, dir, de) vfs_unlink(dir, de, NULL)
#else
#define vfs_unlink(ns, dir, de) vfs_unlink(dir, de)
#endif

#ifdef HAVE_U64_CAPABILITY
#define ll_capability_u32(kcap) \
	((kcap).val & 0xFFFFFFFF)
#define ll_set_capability_u32(kcap, val32) \
	((kcap)->val = ((kcap)->val & 0xffffffff00000000ull) | (val32))
#else
#define ll_capability_u32(kcap) \
	((kcap).cap[0])
#define ll_set_capability_u32(kcap, val32) \
	((kcap)->cap[0] = val32)
#endif

#ifndef HAVE_PTR_ERR_OR_ZERO
static inline int __must_check PTR_ERR_OR_ZERO(__force const void *ptr)
{
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	else
		return 0;
}
#endif

#ifdef HAVE_PID_NS_FOR_CHILDREN
# define ll_task_pid_ns(task) \
	 ((task)->nsproxy ? ((task)->nsproxy->pid_ns_for_children) : NULL)
#else
# define ll_task_pid_ns(task) \
	 ((task)->nsproxy ? ((task)->nsproxy->pid_ns) : NULL)
#endif

#ifdef HAVE_FULL_NAME_HASH_3ARGS
# define ll_full_name_hash(salt, name, len) full_name_hash(salt, name, len)
#else
# define ll_full_name_hash(salt, name, len) full_name_hash(name, len)
#endif

#ifdef HAVE_STRUCT_POSIX_ACL_XATTR
# define posix_acl_xattr_header struct posix_acl_xattr_header
# define posix_acl_xattr_entry  struct posix_acl_xattr_entry
# define GET_POSIX_ACL_XATTR_ENTRY(head) ((void *)((head) + 1))
#else
# define GET_POSIX_ACL_XATTR_ENTRY(head) ((head)->a_entries)
#endif

#ifdef HAVE_IOP_XATTR
#define ll_setxattr     generic_setxattr
#define ll_getxattr     generic_getxattr
#define ll_removexattr  generic_removexattr
#endif /* HAVE_IOP_XATTR */

#ifndef HAVE_POSIX_ACL_VALID_USER_NS
#define posix_acl_valid(a, b)		posix_acl_valid(b)
#endif

#ifdef HAVE_IOP_SET_ACL
#ifdef CONFIG_LUSTRE_FS_POSIX_ACL
#if !defined(HAVE_USER_NAMESPACE_ARG) && \
	!defined(HAVE_POSIX_ACL_UPDATE_MODE) && \
	!defined(HAVE_MNT_IDMAP_ARG)
static inline int posix_acl_update_mode(struct inode *inode, umode_t *mode_p,
			  struct posix_acl **acl)
{
	umode_t mode = inode->i_mode;
	int error;

	error = posix_acl_equiv_mode(*acl, &mode);
	if (error < 0)
		return error;
	if (error == 0)
		*acl = NULL;
	if (!in_group_p(inode->i_gid) &&
	    !capable_wrt_inode_uidgid(inode, CAP_FSETID))
		mode &= ~S_ISGID;
	*mode_p = mode;
	return 0;
}
#endif /* HAVE_POSIX_ACL_UPDATE_MODE */
#endif
#endif

#ifndef HAVE_IOV_ITER_TRUNCATE
static inline void iov_iter_truncate(struct iov_iter *i, u64 count)
{
	if (i->count > count)
		i->count = count;
}
#endif

/*
 * mount MS_* flags split from superblock SB_* flags
 * if the SB_* flags are not available use the MS_* flags
 */
#if !defined(SB_RDONLY) && defined(MS_RDONLY)
# define SB_RDONLY MS_RDONLY
#endif
#if !defined(SB_ACTIVE) && defined(MS_ACTIVE)
# define SB_ACTIVE MS_ACTIVE
#endif
#if !defined(SB_NOSEC) && defined(MS_NOSEC)
# define SB_NOSEC MS_NOSEC
#endif
#if !defined(SB_POSIXACL) && defined(MS_POSIXACL)
# define SB_POSIXACL MS_POSIXACL
#endif
#if !defined(SB_NODIRATIME) && defined(MS_NODIRATIME)
# define SB_NODIRATIME MS_NODIRATIME
#endif
#if !defined(SB_KERNMOUNT) && defined(MS_KERNMOUNT)
# define SB_KERNMOUNT MS_KERNMOUNT
#endif

#ifndef HAVE_IOV_ITER_IOVEC
static inline struct iovec iov_iter_iovec(const struct iov_iter *iter)
{
	return (struct iovec) {
		.iov_base = iter->__iov->iov_base + iter->iov_offset,
		.iov_len = min(iter->count,
			       iter->__iov->iov_len - iter->iov_offset),
	};
}
#endif

static inline void __user *get_vmf_address(struct vm_fault *vmf)
{
#ifdef HAVE_VM_FAULT_ADDRESS
	return (void __user *)vmf->address;
#else
	return vmf->virtual_address;
#endif
}

#ifdef HAVE_VM_OPS_USE_VM_FAULT_ONLY
# define __ll_filemap_fault(vma, vmf) filemap_fault(vmf)
#else
# define __ll_filemap_fault(vma, vmf) filemap_fault(vma, vmf)
#endif

#ifndef HAVE_CURRENT_TIME
static inline struct timespec current_time(struct inode *inode)
{
	return CURRENT_TIME;
}
#endif

#ifndef time_after32
/**
 * time_after32 - compare two 32-bit relative times
 * @a: the time which may be after @b
 * @b: the time which may be before @a
 *
 * Needed for kernels earlier than v4.14-rc1~134^2
 *
 * time_after32(a, b) returns true if the time @a is after time @b.
 * time_before32(b, a) returns true if the time @b is before time @a.
 *
 * Similar to time_after(), compare two 32-bit timestamps for relative
 * times.  This is useful for comparing 32-bit seconds values that can't
 * be converted to 64-bit values (e.g. due to disk format or wire protocol
 * issues) when it is known that the times are less than 68 years apart.
 */
#define time_after32(a, b)     ((s32)((u32)(b) - (u32)(a)) < 0)
#define time_before32(b, a)    time_after32(a, b)

#endif

/* kernel version less than 4.2, smp_store_mb is not defined, use set_mb */
#ifndef smp_store_mb
#define smp_store_mb(var, value) set_mb(var, value) /* set full mem barrier */
#endif

#ifndef HAVE_IN_COMPAT_SYSCALL
#define in_compat_syscall	is_compat_task
#endif

#ifdef HAVE_I_PAGES
#define page_tree i_pages
#define ll_xa_lock_irqsave(lockp, flags) xa_lock_irqsave(lockp, flags)
#define ll_xa_unlock_irqrestore(lockp, flags) xa_unlock_irqrestore(lockp, flags)
#else
#define i_pages tree_lock
#define ll_xa_lock_irqsave(lockp, flags) spin_lock_irqsave(lockp, flags)
#define ll_xa_unlock_irqrestore(lockp, flags) spin_unlock_irqrestore(lockp, \
								     flags)
#endif

#ifndef KMEM_CACHE_USERCOPY
#define kmem_cache_create_usercopy(name, size, align, flags, useroffset, \
				   usersize, ctor)			 \
	kmem_cache_create(name, size, align, flags, ctor)
#endif

static inline bool ll_security_xattr_wanted(struct inode *in)
{
#ifdef CONFIG_SECURITY
	return in->i_security && in->i_sb->s_security;
#else
	return false;
#endif
}

static inline int ll_vfs_getxattr(struct dentry *dentry, struct inode *inode,
				  const char *name,
				  void *value, size_t size)
{
#if defined(HAVE_MNT_IDMAP_ARG) || defined(HAVE_USER_NAMESPACE_ARG) || \
	defined(HAVE_VFS_SETXATTR)
	return __vfs_getxattr(dentry, inode, name, value, size);
#else
	if (unlikely(!inode->i_op->getxattr))
		return -ENODATA;

	return inode->i_op->getxattr(dentry, name, value, size);
#endif
}

static inline int ll_vfs_setxattr(struct dentry *dentry, struct inode *inode,
				  const char *name,
				  const void *value, size_t size, int flags)
{
#if defined(HAVE_MNT_IDMAP_ARG) || defined(HAVE_USER_NAMESPACE_ARG)
	return __vfs_setxattr(&nop_mnt_idmap, dentry, inode, name,
			    VFS_SETXATTR_VALUE(value), size, flags);
#elif defined(HAVE_VFS_SETXATTR)
	return __vfs_setxattr(dentry, inode, name, value, size, flags);
#else
	if (unlikely(!inode->i_op->setxattr))
		return -EOPNOTSUPP;

	return inode->i_op->setxattr(dentry, name, value, size, flags);
#endif
}

static inline int ll_vfs_removexattr(struct dentry *dentry, struct inode *inode,
				     const char *name)
{
#if defined(HAVE_MNT_IDMAP_ARG) || defined(HAVE_USER_NAMESPACE_ARG)
	return __vfs_removexattr(&nop_mnt_idmap, dentry, name);
#elif defined(HAVE_VFS_SETXATTR)
	return __vfs_removexattr(dentry, name);
#else
	if (unlikely(!inode->i_op->setxattr))
		return -EOPNOTSUPP;

	return inode->i_op->removexattr(dentry, name);
#endif
}

/* until v3.19-rc5-3-gb4caecd48005 */
#ifndef BDI_CAP_MAP_COPY
#define BDI_CAP_MAP_COPY		0
#endif

/* from v4.1-rc2-56-g89e9b9e07a39, until v5.9-rc3-161-gf56753ac2a90 */
#ifndef BDI_CAP_CGROUP_WRITEBACK
#define BDI_CAP_CGROUP_WRITEBACK	0
#endif

/* from v5.9-rc3-161-gf56753ac2a90 */
#ifndef BDI_CAP_WRITEBACK
#define BDI_CAP_WRITEBACK		0
#endif

/* from v5.9-rc3-161-gf56753ac2a90 */
#ifndef BDI_CAP_WRITEBACK_ACCT
#define BDI_CAP_WRITEBACK_ACCT		0
#endif

#define LL_BDI_CAP_FLAGS	(BDI_CAP_CGROUP_WRITEBACK | BDI_CAP_MAP_COPY | \
				 BDI_CAP_WRITEBACK | BDI_CAP_WRITEBACK_ACCT)

#ifndef FALLOC_FL_COLLAPSE_RANGE
#define FALLOC_FL_COLLAPSE_RANGE 0x08 /* remove a range of a file */
#endif

#ifndef FALLOC_FL_ZERO_RANGE
#define FALLOC_FL_ZERO_RANGE 0x10 /* convert range to zeros */
#endif

#ifndef FALLOC_FL_INSERT_RANGE
#define FALLOC_FL_INSERT_RANGE 0x20 /* insert space within file */
#endif

#ifndef raw_cpu_ptr
#define raw_cpu_ptr(p) __this_cpu_ptr(p)
#endif

#ifndef HAVE_IS_ROOT_INODE
static inline bool is_root_inode(struct inode *inode)
{
	return inode == inode->i_sb->s_root->d_inode;
}
#endif

#if defined(HAVE_DIRECTIO_ITER) || defined(HAVE_IOV_ITER_RW) || \
	defined(HAVE_DIRECTIO_2ARGS) || defined(HAVE_IOV_ITER_GET_PAGES_ALLOC2)
#define HAVE_DIO_ITER 1
#endif


#ifdef HAVE_AOPS_MIGRATE_FOLIO
#define folio_migr	folio
#else
#define folio_migr	page
#define migrate_folio	migratepage
#endif

static inline const char *shrinker_debugfs_path(struct shrinker *shrinker)
{
#ifndef CONFIG_SHRINKER_DEBUG
 #ifndef HAVE_SHRINKER_ALLOC
	struct ll_shrinker *s = container_of(shrinker, struct ll_shrinker,
					     ll_shrinker);
 #else
	struct ll_shrinker *s = shrinker->private_data;
 #endif
#else /* !CONFIG_SHRINKER_DEBUG */
	struct shrinker *s = shrinker;
#endif /* CONFIG_SHRINKER_DEBUG */

	return s->debugfs_entry->d_name.name;
}

#ifndef fallthrough
# if defined(__GNUC__) && __GNUC__ >= 7
#  define fallthrough  __attribute__((fallthrough)) /* fallthrough */
# else
#  define fallthrough do {} while (0)  /* fallthrough */
# endif
#endif

#ifdef VERIFY_WRITE /* removed in kernel commit v4.20-10979-g96d4f267e40f */
#define ll_access_ok(ptr, len) access_ok(VERIFY_WRITE, ptr, len)
#else
#define ll_access_ok(ptr, len) access_ok(ptr, len)
#endif

#ifndef HAVE_WB_STAT_MOD
#define wb_stat_mod(wb, item, amount)	__add_wb_stat(wb, item, amount)
#endif

#ifdef HAVE_SEC_RELEASE_SECCTX_1ARG
#ifndef HAVE_LSMCONTEXT_INIT
/* Ubuntu 5.19 */
static inline void lsmcontext_init(struct lsm_context *cp, char *context,
				   u32 size, int slot)
{
#ifdef HAVE_LSMCONTEXT_HAS_ID
	cp->id = slot;
#else
	cp->slot = slot;
#endif
	cp->context = context;
	cp->len = size;
}
#endif
#endif

static inline void ll_security_release_secctx(char *secdata, u32 seclen,
					      int slot)
{
#ifdef HAVE_SEC_RELEASE_SECCTX_1ARG
	struct lsm_context context = { };

	lsmcontext_init(&context, secdata, seclen, slot);
	return security_release_secctx(&context);
#else
	return security_release_secctx(secdata, seclen);
#endif
}

#if !defined(HAVE_USER_NAMESPACE_ARG) && !defined(HAVE_MNT_IDMAP_ARG)
#define posix_acl_update_mode(ns, inode, mode, acl) \
	posix_acl_update_mode(inode, mode, acl)
#define notify_change(ns, de, attr, inode)	notify_change(de, attr, inode)
#define inode_owner_or_capable(ns, inode)	inode_owner_or_capable(inode)
#define vfs_mkdir(ns, dir, de, mode)		vfs_mkdir(dir, de, mode)
#define ll_set_acl(ns, inode, acl, type)	ll_set_acl(inode, acl, type)
#endif

#ifdef HAVE_RADIX_TREE_REPLACE_SLOT_3ARGS
# define radix_tree_rcu	__rcu
#else /* !HAVE_RADIX_TREE_REPLACE_SLOT_3ARGS */
# define radix_tree_rcu
#endif /* HAVE_RADIX_TREE_REPLACE_SLOT_3ARGS */

#endif /* _LUSTRE_COMPAT_H */
