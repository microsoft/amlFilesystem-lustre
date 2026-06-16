// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Google.
 * Userspace binary for GSSIAM server-side upcall.
 *
 * Usage: l_gssiam_auth [-v validity_duration] <key> <token_encoded>
 *	  <subdir> <upcall_name> <options>
 *
 * It mocks authentication for GSSIAM tokens by approving the token and
 * writing the result back to
 * /sys/fs/lustre/sptlrpc/<upcall_name>/gssiam_auth_downcall
 *
 * Result format: <key> <mount_option>:<expire>:<approved_options>:<principal>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/lustre/lustre_idl.h>
#include <linux/lustre/lgss.h>

#include <libcfs/util/param.h>
#include "err_util.h"

int main(int argc, char *argv[])
{
	char *token_encoded;
	char *token;
	char *subdir;
	char *uc_name;
	int token_len;
	unsigned long long key;
	char result[1024];
	int fd;
	time_t expire;
	int validity_duration = 600; /* Default validity */
	unsigned int options = 0;
	const char *mode = "rw";
	const char *principal = "mock_gssiam_user";
	unsigned int approved_options = 0;
	int opt;
	glob_t path;
	int rc;

	initerr("l_gssiam_auth", LL_MAX, isatty(STDOUT_FILENO));

	while ((opt = getopt(argc, argv, "+v:")) != -1) {
		switch (opt) {
		case 'v':
			validity_duration = atoi(optarg);
			break;
		default:
			logmsg(LL_ERR,
				"Usage: %s [-v validity_duration] <key> <token_encoded> <subdir> <upcall_name> <options>\n",
				argv[0]);
			return 1;
		}
	}

	if (argc - optind < 5) {
		logmsg(LL_ERR,
			"Usage: %s [-v validity_duration] <key> <token_encoded> <subdir> <upcall_name> <options>\n",
			argv[0]);
		return 1;
	}

	key = strtoull(argv[optind], NULL, 10);
	token_encoded = argv[optind + 1];
	subdir = argv[optind + 2];
	uc_name = argv[optind + 3];

	/*
	 * Parse client-side mount options bitmask
	 * (enum gssiam_mount_option_flags):
	 * - GSSIAM_OPT_RDONLY (0x1): client mounted read-only (-o ro).
	 * - GSSIAM_OPT_DISABLE_ROOT_SQUASH (0x2): client requested
	 *   disable_rootsquash.
	 */
	options = strtoul(argv[optind + 4], NULL, 10);

	token = malloc(strlen(token_encoded) + 1);
	if (!token) {
		logmsg(LL_ERR, "malloc: %s\n", strerror(errno));
		return 1;
	}

	token_len = gss_base64url_decode(&token_encoded, token,
					 strlen(token_encoded) + 1);
	if (token_len <= 0) {
		logmsg(LL_ERR, "Failed to decode token\n");
		free(token);
		return 1;
	}
	token[token_len] = '\0';

	logmsg(LL_INFO, "subdir %s ucname %s options %u\n",
		subdir, uc_name, options);

	/* Mock validation: unconditionally approve token */

	expire = validity_duration;

	/* Look up the parameter path transparently */
	rc = cfs_get_param_paths(&path, "sptlrpc/%s/gssiam_auth_downcall",
				 uc_name);
	if (rc) {
		logmsg(LL_ERR,
			"Failed to resolve parameter path for sptlrpc/%s/gssiam_auth_downcall: %s\n",
			uc_name, strerror(errno));
		free(token);
		return 1;
	}

	fd = open(path.gl_pathv[0], O_WRONLY);
	if (fd < 0) {
		logmsg(LL_ERR, "Failed to open file for %s at %s: %s\n",
			uc_name, path.gl_pathv[0], strerror(errno));
		cfs_free_param_data(&path);
		free(token);
		return 1;
	}


	if (options & GSSIAM_OPT_DISABLE_ROOT_SQUASH) {
		/*
		 * Real implementations would enforce security policies here.
		 * For this mock upcall, we always approve the request if the
		 * client specified disable_rootsquash.
		 */
		approved_options |= GSSIAM_OPT_DISABLE_ROOT_SQUASH;
	}

	/* XXX Extract principal from the token */
	/* Write result: key mount_option:expire:approved_options:principal */
	snprintf(result, sizeof(result), "%llu %s:%lld:%u:%s\n", key, mode,
		 (long long)expire, approved_options, principal);

	logmsg(LL_INFO,
	       "Trying downcall with key %llu (token_len=%d) to %s %.*s\n",
	       key, token_len, path.gl_pathv[0],
	       (int)strlen(result) > 0 ? (int)strlen(result) - 1 : 0, result);

	if (write(fd, result, strlen(result)) < 0) {
		logmsg(LL_ERR, "write: %s\n", strerror(errno));
		close(fd);
		cfs_free_param_data(&path);
		free(token);
		return 1;
	}

	close(fd);
	cfs_free_param_data(&path);
	free(token);
	return 0;
}
