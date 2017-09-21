/*
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
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2017, Intel Corporation.
 *
 * Author:
 *   Sonia Sharma <sonia.sharma@intel.com>
 */


#include <lnet/lib-lnet.h>
#include <lnet/lnet-sysfs.h>

struct kobject *lnet_kobj;
EXPORT_SYMBOL_GPL(lnet_kobj);

/*
 * Helper functions for sysfs callbacks implementation
 */
ssize_t lnet_attr_show(struct kobject *kobj,
		       struct attribute *attr, char *buf)
{
	struct lnet_attr *a = container_of(attr, struct lnet_attr, attr);

	return a->show ? a->show(kobj, attr, buf) : 0;
}
EXPORT_SYMBOL_GPL(lnet_attr_show);

ssize_t lnet_attr_store(struct kobject *kobj, struct attribute *attr,
			  const char *buf, size_t len)
{
	struct lnet_attr *a = container_of(attr, struct lnet_attr, attr);

	return a->store ? a->store(kobj, attr, buf, len) : len;
}
EXPORT_SYMBOL_GPL(lnet_attr_store);

const struct sysfs_ops lnet_sysfs_ops = {
	.show  = lnet_attr_show,
	.store = lnet_attr_store,
};
EXPORT_SYMBOL_GPL(lnet_sysfs_ops);
