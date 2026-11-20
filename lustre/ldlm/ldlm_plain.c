// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2017, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Author: Peter Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 */

/*
 * This file contains implementation of PLAIN lock type.
 *
 * PLAIN locks are the simplest form of LDLM locking, and are used when
 * there only needs to be a single lock on a resource. This avoids some
 * of the complexity of EXTENT and IBITS lock types, but doesn't allow
 * different "parts" of a resource to be locked concurrently.  Example
 * use cases for PLAIN locks include locking of MGS configuration logs
 * and (as of Lustre 2.4) quota records.
 */

#define DEBUG_SUBSYSTEM S_LDLM

#include <lustre_dlm.h>
#include <obd_support.h>
#include <lustre_lib.h>

#include "ldlm_internal.h"

#if IS_ENABLED(CONFIG_LUSTRE_FS_SERVER)
/**
 * ldlm_plain_compat_queue() - Determine if the lock is compatible with all
 *                             locks on the queue.
 * @queue: granted/waiting lock queue
 * @req: LDLM lock
 * @work_list: conflicting locks list
 *
 * If @work_list is provided, conflicting locks are linked there.
 * If @work_list is not provided, we exit this function on first conflict.
 *
 * Return:
 * * %0 if there are conflicting locks in the @queue
 * * %1 if the lock is compatible to all locks in @queue
 */
static inline int
ldlm_plain_compat_queue(struct list_head *queue, struct ldlm_lock *req,
			struct list_head *work_list)
{
	enum ldlm_mode req_mode = req->l_req_mode;
	struct ldlm_lock *lock, *next_lock;
	int compat = 1;

	ENTRY;
	lockmode_verify(req_mode);

	list_for_each_entry_safe(lock, next_lock, queue, l_res_link) {

		/*
		 * We stop walking the queue if we hit ourselves so we don't
		 * take conflicting locks enqueued after us into account,
		 * or we'd wait forever.
		 */
		if (req == lock)
			RETURN(compat);

		/* Advance loop cursor to last lock of mode group. */
		next_lock = list_entry(list_entry(lock->l_sl_mode.prev,
						  struct ldlm_lock,
						  l_sl_mode)->l_res_link.next,
				       struct ldlm_lock, l_res_link);

		if (lockmode_compat(lock->l_req_mode, req_mode))
			continue;

		if (!work_list)
			RETURN(0);

		compat = 0;

		/*
		 * Add locks of the mode group to @work_list as
		 * blocking locks for @req.
		 */
		if (lock->l_blocking_ast)
			ldlm_add_ast_work_item(lock, req, work_list);

		{
			struct list_head *head;

			head = &lock->l_sl_mode;
			list_for_each_entry(lock, head, l_sl_mode)
				if (lock->l_blocking_ast)
					ldlm_add_ast_work_item(lock, req,
							       work_list);
		}
	}

	RETURN(compat);
}

/**
 * ldlm_process_plain_lock() - Process a granting attempt for plain lock.
 * @lock: LDLM lock
 * @flags: Unused
 * @intention: Processing intention (Rescan/Enqueue/Recovery)
 * @err: granting attempt status, always set to %ELDLM_OK here [out]
 * @work_list: list gathering the AST work items to send: blocking ASTs of
 *             conflicting locks, except for %LDLM_PROCESS_RESCAN, and the
 *             completion AST of @lock once granted, except for
 *             %LDLM_PROCESS_ENQUEUE
 *
 * This function looks for any conflicts for @lock in the granted or
 * waiting queues. The lock is granted if no conflicts are found in
 * either queue.
 *
 * Context: Called with the resource lock held (see check_res_locked()).
 * Return:
 * * %LDLM_ITER_STOP if @intention is %LDLM_PROCESS_RESCAN and a conflicting
 *   lock was found
 * * %LDLM_ITER_CONTINUE otherwise
 */
int ldlm_process_plain_lock(struct ldlm_lock *lock, __u64 *flags,
			    enum ldlm_process_intention intention,
			    enum ldlm_error *err, struct list_head *work_list)
{
	struct ldlm_resource *res = lock->l_resource;
	struct list_head *grant_work = intention == LDLM_PROCESS_ENQUEUE ?
							NULL : work_list;
	int rc;

	ENTRY;
	LASSERT(!ldlm_is_granted(lock));
	check_res_locked(res);
	*err = ELDLM_OK;

	if (intention == LDLM_PROCESS_RESCAN) {
		LASSERT(work_list != NULL);
		rc = ldlm_plain_compat_queue(&res->lr_granted, lock, NULL);
		if (!rc)
			RETURN(LDLM_ITER_STOP);
		rc = ldlm_plain_compat_queue(&res->lr_waiting, lock, NULL);
		if (!rc)
			RETURN(LDLM_ITER_STOP);

		ldlm_resource_unlink_lock(lock);
		ldlm_grant_lock(lock, grant_work);
		RETURN(LDLM_ITER_CONTINUE);
	}

	rc = ldlm_plain_compat_queue(&res->lr_granted, lock, work_list);
	rc += ldlm_plain_compat_queue(&res->lr_waiting, lock, work_list);

	if (rc == 2) {
		ldlm_resource_unlink_lock(lock);
		ldlm_grant_lock(lock, grant_work);
	}

	RETURN(LDLM_ITER_CONTINUE);
}
#endif /* CONFIG_LUSTRE_FS_SERVER */

void ldlm_plain_policy_wire_to_local(const union ldlm_wire_policy_data *wpolicy,
				     union ldlm_policy_data *lpolicy)
{
	/* No policy for plain locks */
}

void ldlm_plain_policy_local_to_wire(const union ldlm_policy_data *lpolicy,
				     union ldlm_wire_policy_data *wpolicy)
{
	/* No policy for plain locks */
}
