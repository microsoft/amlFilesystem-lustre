// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, DDN.
 */
/*
 * Test program for swapping a single stripe between two files by OST FID.
 *
 * Usage: swap_single_stripe [-c [-b]] <file1> <fid1> <file2> <fid2>
 *   fid format: seq:oid:ver (brackets optional) e.g. 0x240000400:0x105:0x0
 *   -c: swap at the close of a lease on file1 (SWAP_LAYOUTS_CLOSE)
 *   -b: with -c, break the lease with an open for write before the swap
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <lustre/lustreapi.h>

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-c [-b]] <file1> <fid1> <file2> <fid2>\n"
		"  fid format: seq:oid:ver (brackets optional)\n"
		"  -c: swap at the close of a lease on file1\n"
		"  -b: with -c, break the lease before the swap\n", prog);
	exit(1);
}

#define SWAP_MAX_RETRIES 10

/*
 * Swap the objects when the lease on @fd1 is closed, as a migration that
 * must not lose a racing write does.  The data versions are compared by
 * the caller, so they are only taken here.
 */
static int swap_at_close(int fd1, int fd2, const char *file1,
			 const struct lu_fid *fid1, const struct lu_fid *fid2,
			 bool lease_break)
{
	__u64 dv1, dv2;
	int rc;

	rc = llapi_lease_acquire(fd1, LL_LEASE_RDLCK);
	if (rc < 0) {
		fprintf(stderr, "Error: lease on %s: %s\n",
			file1, strerror(-rc));
		return 1;
	}

	if (lease_break) {
		int fd3 = open(file1, O_WRONLY);

		if (fd3 < 0) {
			fprintf(stderr, "Error: cannot open %s: %s\n",
				file1, strerror(errno));
			llapi_lease_release(fd1);
			return 1;
		}
		close(fd3);
	}

	rc = llapi_get_data_version(fd1, &dv1, LL_DV_RD_FLUSH);
	if (rc == 0)
		rc = llapi_get_data_version(fd2, &dv2, LL_DV_RD_FLUSH);
	if (rc == 0)
		rc = llapi_fswap_layout_fids(fd1, fd2, fid1, fid2, dv1, dv2,
					      SWAP_LAYOUTS_CLOSE);
	if (rc < 0) {
		/* the close may already have released the lease */
		llapi_lease_release(fd1);
		fprintf(stderr, "Error: swap at close failed: %s\n",
			strerror(-rc));
		return 1;
	}

	printf("Stripe swap at close successful\n");
	return 0;
}

int main(int argc, char *argv[])
{
	struct lu_fid fid1, fid2;
	const char *file1, *file2;
	bool lease_close = false;
	bool lease_break = false;
	__u64 dv1, dv2;
	int fd1, fd2, rc;
	int retries;
	int c;

	while ((c = getopt(argc, argv, "bc")) != -1) {
		switch (c) {
		case 'b':
			lease_break = true;
			break;
		case 'c':
			lease_close = true;
			break;
		default:
			usage(argv[0]);
		}
	}
	if (argc - optind != 4 || (lease_break && !lease_close))
		usage(argv[0]);

	file1 = argv[optind];
	file2 = argv[optind + 2];
	if (llapi_fid_parse(argv[optind + 1], &fid1, NULL) ||
	    llapi_fid_parse(argv[optind + 3], &fid2, NULL)) {
		fprintf(stderr, "Error: invalid FID\n");
		return 1;
	}

	fd1 = open(file1, O_RDWR);
	if (fd1 < 0) {
		fprintf(stderr, "Error: cannot open %s: %s\n",
			file1, strerror(errno));
		return 1;
	}
	fd2 = open(file2, O_RDWR);
	if (fd2 < 0) {
		fprintf(stderr, "Error: cannot open %s: %s\n",
			file2, strerror(errno));
		close(fd1);
		return 1;
	}

	printf("Swapping " DFID " in %s with " DFID " in %s%s\n",
	       PFID(&fid1), file1, PFID(&fid2), file2,
	       lease_close ? " at lease close" : "");

	if (lease_close) {
		rc = swap_at_close(fd1, fd2, file1, &fid1, &fid2, lease_break);
		close(fd2);
		close(fd1);
		return rc;
	}

	/*
	 * API always checks data versions; capture them just before the
	 * swap. The version can be bumped by unrelated activity (e.g. a
	 * glimpse/attribute revalidation) between the read here and the
	 * check done internally by the swap ioctl, so retry a few times
	 * on -EAGAIN instead of treating it as a hard failure.
	 */
	for (retries = 0; retries < SWAP_MAX_RETRIES; retries++) {
		rc = llapi_get_data_version(fd1, &dv1, 0);
		if (rc < 0) {
			fprintf(stderr, "Error: data version %s: %s\n",
				file1, strerror(-rc));
			close(fd2);
			close(fd1);
			return 1;
		}
		rc = llapi_get_data_version(fd2, &dv2, 0);
		if (rc < 0) {
			fprintf(stderr, "Error: data version %s: %s\n",
				file2, strerror(-rc));
			close(fd2);
			close(fd1);
			return 1;
		}

		rc = llapi_fswap_layout_fids(fd1, fd2, &fid1, &fid2, dv1, dv2,
					      0);
		if (rc != -EAGAIN)
			break;

		usleep(100000);
	}
	if (rc < 0) {
		fprintf(stderr, "Error: swap failed: %s\n", strerror(-rc));
		close(fd2);
		close(fd1);
		return 1;
	}

	printf("Stripe swap successful\n");
	close(fd2);
	close(fd1);
	return 0;
}
