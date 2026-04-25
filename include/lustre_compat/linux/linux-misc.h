/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2017, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 */

#ifndef __LIBCFS_LINUX_MISC_H__
#define __LIBCFS_LINUX_MISC_H__

#include <linux/kallsyms.h>

static inline unsigned long cfs_time_seconds(time64_t seconds)
{
	return nsecs_to_jiffies64(seconds * NSEC_PER_SEC);
}

/* TODO: This will soon be private... */
void *cfs_kallsyms_lookup_name(const char *name);
int lustre_symbols_init(void);

/*
 * For RHEL6 struct kernel_parm_ops doesn't exist. Also
 * the arguments for .set and .get take different
 * parameters which is handled below
 */
#ifdef HAVE_KERNEL_PARAM_OPS
#define cfs_kernel_param_arg_t const struct kernel_param
#else
#define cfs_kernel_param_arg_t struct kernel_param_ops
#define kernel_param_ops kernel_param
#endif /* ! HAVE_KERNEL_PARAM_OPS */

#ifndef HAVE_KERNEL_PARAM_LOCK
static inline void kernel_param_unlock(struct module *mod)
{
	__kernel_param_unlock();
}

static inline void kernel_param_lock(struct module *mod)
{
	__kernel_param_lock();
}
#endif /* ! HAVE_KERNEL_PARAM_LOCK */

#ifndef HAVE_MATCH_WILDCARD
bool match_wildcard(const char *pattern, const char *str);
#endif /* !HAVE_MATCH_WILDCARD */

#ifndef HAVE_KREF_READ
static inline int kref_read(const struct kref *kref)
{
	return atomic_read(&kref->refcount);
}
#endif /* HAVE_KREF_READ */

int cfs_arch_init(void);
void cfs_arch_exit(void);

/*
 * Linux v4.15-rc2-5-g4229a470175b added sizeof_field()
 * Linux v5.5-rc4-1-g1f07dcc459d5 removed FIELD_SIZEOF()
 * Proved a sizeof_field in terms of FIELD_SIZEOF() when one is not provided
 */
#ifndef sizeof_field
#define sizeof_field(type, member)	FIELD_SIZEOF(type, member)
#endif

#ifndef HAVE_TASK_IS_RUNNING
#define task_is_running(task)		(task->state == TASK_RUNNING)
#endif

/* interval tree */
#ifdef HAVE_INTERVAL_TREE_CACHED
#define interval_tree_root rb_root_cached
#define interval_tree_first rb_first_cached
#define INTERVAL_TREE_ROOT RB_ROOT_CACHED
#define INTERVAL_TREE_EMPTY(_root) RB_EMPTY_ROOT(&(_root)->rb_root)
#else
#define interval_tree_root rb_root
#define interval_tree_first rb_first
#define INTERVAL_TREE_ROOT RB_ROOT
#define INTERVAL_TREE_EMPTY(_root) RB_EMPTY_ROOT(_root)
#endif /* HAVE_INTERVAL_TREE_CACHED */

#ifndef HAVE_STRSCPY
static inline ssize_t strscpy(char *s1, const char *s2, size_t sz)
{
	ssize_t len = strlcpy(s1, s2, sz);

	return (len >= sz) ? -E2BIG : len;
}
#endif


#endif /* __LIBCFS_LINUX_MISC_H__ */
