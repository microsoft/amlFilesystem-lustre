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
#include <lustre_compat/linux/folio_batch.h>
#include <linux/workqueue.h>
#include <lustre_compat/linux/shrinker.h>
#include <lustre_compat/linux/xarray.h>
#include <lustre_compat/linux/folio.h>
#include <lustre_compat/linux/blkdev.h>
#include <lustre_compat/linux/posix_acl_xattr.h>
#include <obd_support.h>

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

#define LL_BDI_CAP_FLAGS	(BDI_CAP_CGROUP_WRITEBACK | \
				 BDI_CAP_WRITEBACK | BDI_CAP_WRITEBACK_ACCT)

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

#ifndef HAVE_WB_STAT_MOD
#define wb_stat_mod(wb, item, amount)	__add_wb_stat(wb, item, amount)
#endif

#ifdef HAVE_IS_PCI_P2PDMA_PAGE
#include <linux/pci-p2pdma.h>
#endif

static inline bool lustre_is_p2prdma_page(struct page *page)
{
#ifdef HAVE_IS_PCI_P2PDMA_PAGE
	return is_pci_p2pdma_page(page);
#else
	return false;
#endif
}

#endif /* _LUSTRE_COMPAT_H */
