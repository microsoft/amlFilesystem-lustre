/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see [sun.com URL with a
 * copy of GPLv2].
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright  2008 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/fid/fid_internal.h
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 */
#ifndef __FID_INTERNAL_H
#define __FID_INTERNAL_H

#include <lustre/lustre_idl.h>
#include <dt_object.h>

#include <libcfs/libcfs.h>

#ifdef __KERNEL__
struct seq_thread_info {
        struct req_capsule     *sti_pill;
        struct txn_param        sti_txn;
        struct lu_range         sti_space;
        struct lu_buf           sti_buf;
};

extern struct lu_context_key seq_thread_key;

/* Functions used internally in module. */
int seq_client_alloc_super(struct lu_client_seq *seq,
                           const struct lu_env *env);

int seq_client_replay_super(struct lu_client_seq *seq,
                            struct lu_range *range,
                            const struct lu_env *env);

/* Store API functions. */
int seq_store_init(struct lu_server_seq *seq,
                   const struct lu_env *env,
                   struct dt_device *dt);

void seq_store_fini(struct lu_server_seq *seq,
                    const struct lu_env *env);

int seq_store_write(struct lu_server_seq *seq,
                    const struct lu_env *env);

int seq_store_read(struct lu_server_seq *seq,
                   const struct lu_env *env);

#ifdef LPROCFS
extern struct lprocfs_vars seq_server_proc_list[];
extern struct lprocfs_vars seq_client_proc_list[];
#endif

#endif

extern cfs_proc_dir_entry_t *seq_type_proc_dir;

#endif /* __FID_INTERNAL_H */
