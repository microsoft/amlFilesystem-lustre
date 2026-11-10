// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Exercise LL_IOC_SET_IO_SCOPE / llapi_io_scope_set().
 */

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lustre/lustreapi.h>

#define IO_SCOPE_WRITE_SIZE	4096

static void usage(void)
{
	fprintf(stderr,
		"usage: io_scope [-s FILE|MIRROR|STRIPE|OBJECT] [-i mid] [-x stripe_idx] [-v layout_version] [-b] [-S] [-p off:len] [-w|-r|-t size|-f|-l] FILE\n"
		"  -b clear O_DIRECT after setting the scope,\n"
		"  -S clear the scope during a paused 4k read at 0,\n"
		"  -p pread len bytes at off and print the count read,\n"
		"  -w pwrite 4k at 0, -r pread 4k at 0, -t size ftruncate,\n"
		"  -f fallocate [0, 8k), -l lseek(SEEK_DATA, 0)\n");
	exit(EXIT_FAILURE);
}

/* exit status is the errno of the I/O under test */
static void __attribute__((noreturn)) die_errno(int fd, const char *op)
{
	int rc = errno;

	fprintf(stderr, "%s: %s\n", op, strerror(rc));
	close(fd);
	exit(rc);
}

static void do_pread(int fd, off_t off, size_t len)
{
	void *buf;
	ssize_t nread;
	int rc;

	rc = posix_memalign(&buf, sysconf(_SC_PAGESIZE), len);
	if (rc)
		errx(EXIT_FAILURE, "posix_memalign: %s", strerror(rc));

	nread = pread(fd, buf, len, off);
	if (nread < 0)
		die_errno(fd, "pread");

	printf("%zd\n", nread);
	free(buf);
}

/* CFS_FAILED from libcfs_fail.h; set once OBD_FAIL_LLITE_READ_PAUSE hits */
#define CFS_FAILED	0x40000000U

/*
 * Wait until the read of @pid reaches OBD_FAIL_LLITE_READ_PAUSE.  The
 * read takes its scope before the pause, so the scope can be cleared then.
 */
static void wait_read_paused(pid_t pid)
{
	const int timeout_ms = 30000;
	const int poll_ms = 100;
	int waited;

	for (waited = 0; waited < timeout_ms; waited += poll_ms) {
		FILE *fp;
		unsigned long loc = 0;
		int status;
		pid_t got;

		got = waitpid(pid, &status, WNOHANG);
		if (got < 0)
			err(EXIT_FAILURE, "waitpid");
		if (got == pid) {
			if (WIFEXITED(status))
				errx(EXIT_FAILURE,
				     "read finished before pause: rc = %d",
				     WEXITSTATUS(status));
			errx(EXIT_FAILURE,
			     "read finished before pause: status %#x", status);
		}

		fp = popen("lctl get_param -n fail_loc", "r");
		if (!fp)
			errx(EXIT_FAILURE, "popen lctl get_param fail_loc");
		if (fscanf(fp, "%lu", &loc) != 1)
			loc = 0;
		pclose(fp);

		if (loc & CFS_FAILED)
			return;

		usleep(poll_ms * 1000);
	}

	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	errx(EXIT_FAILURE, "timed out waiting for read pause");
}

/*
 * Clear the scope while a child reads @fd.  The caller pauses the read
 * with OBD_FAIL_LLITE_READ_PAUSE.  Exits with the errno of the read,
 * which must still use the scope it started with.
 */
static void do_switch(int fd)
{
	int status;
	pid_t pid;
	int rc;

	pid = fork();
	if (pid < 0)
		err(EXIT_FAILURE, "fork");
	if (pid == 0) {
		do_pread(fd, 0, sysconf(_SC_PAGESIZE));
		exit(0);
	}

	wait_read_paused(pid);
	rc = llapi_io_scope_clear(fd);

	if (waitpid(pid, &status, 0) < 0)
		err(EXIT_FAILURE, "waitpid");
	if (rc < 0) {
		errno = -rc;
		die_errno(fd, "clear scope during read");
	}
	if (!WIFEXITED(status))
		errx(EXIT_FAILURE, "read during clear: status %#x", status);
	exit(WEXITSTATUS(status));
}

int main(int argc, char *argv[])
{
	struct ll_ioc_io_scope ios;
	const char *name = "FILE";
	const char *pread_arg = NULL;
	unsigned long mirror_id = 0;
	unsigned long layout_version = 0;
	unsigned long stripe_idx = 0;
	long trunc_size = -1;
	bool buffered = false;
	bool switch_scope = false;
	bool do_write = false;
	bool do_read = false;
	bool do_falloc = false;
	bool do_lseek = false;
	void *buf;
	ssize_t written;
	int fd;
	int c;
	int rc;

	memset(&ios, 0, sizeof(ios));
	opterr = 0;
	while ((c = getopt(argc, argv, "s:i:x:v:bSp:wrt:fl")) != -1) {
		switch (c) {
		case 's':
			name = optarg;
			break;
		case 'i':
			mirror_id = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			layout_version = strtoul(optarg, NULL, 0);
			break;
		case 'x':
			stripe_idx = strtoul(optarg, NULL, 0);
			break;
		case 'b':
			buffered = true;
			break;
		case 'S':
			switch_scope = true;
			break;
		case 'p':
			pread_arg = optarg;
			break;
		case 'w':
			do_write = true;
			break;
		case 'r':
			do_read = true;
			break;
		case 't':
			trunc_size = strtol(optarg, NULL, 0);
			break;
		case 'f':
			do_falloc = true;
			break;
		case 'l':
			do_lseek = true;
			break;
		default:
			usage();
		}
	}

	if (argc == optind)
		usage();

	fd = open(argv[optind], O_RDWR | O_DIRECT);
	if (fd < 0)
		err(EXIT_FAILURE, "open %s", argv[optind]);

	if (!strcmp(name, "FILE"))
		ios.ios_scope = LL_IO_SCOPE_FILE;
	else if (!strcmp(name, "MIRROR"))
		ios.ios_scope = LL_IO_SCOPE_MIRROR;
	else if (!strcmp(name, "STRIPE"))
		ios.ios_scope = LL_IO_SCOPE_STRIPE;
	else if (!strcmp(name, "OBJECT"))
		ios.ios_scope = LL_IO_SCOPE_OBJECT;
	else
		errx(EXIT_FAILURE, "unknown scope '%s'", name);

	ios.ios_mirror_id = mirror_id;
	ios.ios_stripe_idx = stripe_idx;
	ios.ios_layout_version = layout_version;

	rc = llapi_io_scope_set(fd, &ios);
	if (rc < 0) {
		fprintf(stderr, "llapi_io_scope_set: %s\n", strerror(-rc));
		close(fd);
		exit(-rc);
	}

	if (buffered && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_DIRECT) < 0)
		err(EXIT_FAILURE, "fcntl %s", argv[optind]);

	if (switch_scope)
		do_switch(fd);

	if (pread_arg) {
		long long off;
		size_t len;

		if (sscanf(pread_arg, "%lld:%zu", &off, &len) != 2)
			usage();
		do_pread(fd, off, len);
	}

	if (do_write) {
		rc = posix_memalign(&buf, IO_SCOPE_WRITE_SIZE,
				    IO_SCOPE_WRITE_SIZE);
		if (rc)
			errx(EXIT_FAILURE, "posix_memalign: %s", strerror(rc));
		memset(buf, 0, IO_SCOPE_WRITE_SIZE);

		written = pwrite(fd, buf, IO_SCOPE_WRITE_SIZE, 0);
		if (written < 0)
			die_errno(fd, "pwrite");
		free(buf);
		if (written != IO_SCOPE_WRITE_SIZE)
			errx(EXIT_FAILURE, "short write %zd", written);
	}

	if (do_read) {
		rc = posix_memalign(&buf, IO_SCOPE_WRITE_SIZE,
				    IO_SCOPE_WRITE_SIZE);
		if (rc)
			errx(EXIT_FAILURE, "posix_memalign: %s", strerror(rc));
		written = pread(fd, buf, IO_SCOPE_WRITE_SIZE, 0);
		if (written < 0)
			die_errno(fd, "pread");
		free(buf);
	}

	if (trunc_size >= 0 && ftruncate(fd, trunc_size) < 0)
		die_errno(fd, "ftruncate");

	if (do_falloc && fallocate(fd, 0, 0, 2 * IO_SCOPE_WRITE_SIZE) < 0)
		die_errno(fd, "fallocate");

	if (do_lseek && lseek(fd, 0, SEEK_DATA) < 0 && errno != ENXIO)
		die_errno(fd, "lseek");


	close(fd);
	return 0;
}
