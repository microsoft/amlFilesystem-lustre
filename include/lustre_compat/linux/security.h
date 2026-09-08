/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __COMPAT_SECURITY_H
#define __COMPAT_SECURITY_H

#include <linux/security.h>

#ifdef CONFIG_SECURITY
int compat_security_file_alloc(struct file *file);
void compat_security_file_free(struct file *file);

#ifndef COMPAT_BUILD
#define security_file_alloc(file)	compat_security_file_alloc(file)
#define security_file_free(file)	compat_security_file_free(file)
#endif

#endif

#ifndef XATTR_SELINUX_SUFFIX
# define XATTR_SELINUX_SUFFIX "selinux"
#endif

#ifndef XATTR_NAME_SELINUX
# define XATTR_NAME_SELINUX XATTR_SECURITY_PREFIX XATTR_SELINUX_SUFFIX
#endif

#ifdef HAVE_SECURITY_DENTRY_INIT_SECURTY_WITH_CTX
#define HAVE_SECURITY_DENTRY_INIT_WITH_XATTR_NAME_ARG 1
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

#endif /* __COMPAT_SECURITY_H */
