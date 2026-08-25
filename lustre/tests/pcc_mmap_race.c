// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2026, DataDirect Networks, Inc.
 */

/*
 * Races between PCC read and mmap_lock for sanity-pcc.
 *
 * A read() that is served from the PCC copy holds an active PCC I/O while it
 * copies data into the user buffer. If the user buffer is not populated yet,
 * the copy faults and needs mmap_lock for read. This program puts another
 * thread into a PCC code path that waits for the active PCC I/Os to finish
 * while it holds mmap_lock, so a deadlock is easy to hit if that wait is
 * not avoided. The pause points are set by the caller with fail_loc.
 *
 * Modes:
 *   mmap:    reader: pread() into a fresh buffer, paused with an active
 *            PCC I/O (OBD_FAIL_LLITE_PCC_READ_PAUSE);
 *            main: mmap() the file, which holds mmap_lock for write.
 *   mkwrite: writer: store into a MAP_SHARED mapping of the file, paused
 *            in ->page_mkwrite() with mmap_lock held for read
 *            (OBD_FAIL_LLITE_PCC_MKWRITE_PAUSE); for a PCC-RO file the
 *            store detaches the file from PCC;
 *            mapper: mmap() an anonymous region, which queues for
 *            mmap_lock for write behind the writer;
 *            reader: pread() into a fresh buffer, which takes an active
 *            PCC I/O and then queues for mmap_lock behind the mapper.
 *
 * All threads and buffers are set up before the first pause, as creating a
 * thread or allocating memory may need mmap_lock for write as well. The
 * threads are then released one by one with @stage.
 *
 * Exit 0 when all threads finish. A deadlock leaves the program hung, so
 * the caller should run it under timeout(1).
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define READ_SIZE	(1UL << 20)

enum {
	STAGE_SETUP = 0,
	STAGE_WRITE,
	STAGE_MAP,
	STAGE_READ,
};

static int fd;
static int stage;
static int nready;

static void wait_stage(int s)
{
	__atomic_add_fetch(&nready, 1, __ATOMIC_SEQ_CST);
	while (__atomic_load_n(&stage, __ATOMIC_SEQ_CST) < s)
		usleep(1000);
}

static void set_stage(int s, unsigned int delay_us)
{
	__atomic_store_n(&stage, s, __ATOMIC_SEQ_CST);
	usleep(delay_us);
}

static void *reader(void *arg)
{
	char *buf;
	ssize_t rc;

	/*
	 * A fresh anonymous mapping without any page populated, so that the
	 * copy to user space faults and takes mmap_lock.
	 */
	buf = mmap(NULL, READ_SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) {
		fprintf(stderr, "%s: mmap: %s\n", __func__, strerror(errno));
		exit(1);
	}

	wait_stage(STAGE_READ);
	rc = pread(fd, buf, READ_SIZE, 0);
	if (rc < 0) {
		fprintf(stderr, "%s: pread: %s\n", __func__, strerror(errno));
		exit(1);
	}

	munmap(buf, READ_SIZE);
	return NULL;
}

static void *mapper(void *arg)
{
	void *p;

	wait_stage(STAGE_MAP);
	p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "%s: mmap: %s\n", __func__, strerror(errno));
		exit(1);
	}

	munmap(p, 4096);
	return NULL;
}

static void *writer(void *arg)
{
	char *map = arg;

	wait_stage(STAGE_WRITE);
	/* store into the page, which calls ->page_mkwrite() */
	__atomic_store_n(map, __atomic_load_n(map, __ATOMIC_RELAXED),
			 __ATOMIC_RELAXED);
	return NULL;
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s {mmap|mkwrite} <file> [delay_ms]\n", prog);
	exit(1);
}

static void start(pthread_t *th, void *(*fn)(void *), void *arg)
{
	int rc = pthread_create(th, NULL, fn, arg);

	if (rc) {
		fprintf(stderr, "pthread_create: %s\n", strerror(rc));
		exit(1);
	}
}

static void wait_ready(int n)
{
	while (__atomic_load_n(&nready, __ATOMIC_SEQ_CST) < n)
		usleep(1000);
}

int main(int argc, char **argv)
{
	unsigned int delay_us = 500000;
	pthread_t r, w, m;
	char *map;

	if (argc < 3)
		usage(argv[0]);
	if (argc > 3)
		delay_us = atoi(argv[3]) * 1000;

	if (strcmp(argv[1], "mmap") == 0) {
		fd = open(argv[2], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "open %s: %s\n", argv[2],
				strerror(errno));
			return 1;
		}

		start(&r, reader, NULL);
		wait_ready(1);

		/* the reader is paused with an active PCC I/O held */
		set_stage(STAGE_READ, delay_us);

		map = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
		if (map == MAP_FAILED) {
			fprintf(stderr, "mmap %s: %s\n", argv[2],
				strerror(errno));
			return 1;
		}
		__atomic_load_n(map, __ATOMIC_RELAXED);

		pthread_join(r, NULL);
		munmap(map, 4096);
	} else if (strcmp(argv[1], "mkwrite") == 0) {
		fd = open(argv[2], O_RDWR);
		if (fd < 0) {
			fprintf(stderr, "open %s: %s\n", argv[2],
				strerror(errno));
			return 1;
		}

		map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
			   fd, 0);
		if (map == MAP_FAILED) {
			fprintf(stderr, "mmap %s: %s\n", argv[2],
				strerror(errno));
			return 1;
		}
		/* populate the page so that the store calls ->page_mkwrite() */
		__atomic_load_n(map, __ATOMIC_RELAXED);

		start(&w, writer, map);
		start(&m, mapper, NULL);
		start(&r, reader, NULL);
		wait_ready(3);

		/* paused in ->page_mkwrite() with mmap_lock held for read */
		set_stage(STAGE_WRITE, delay_us);
		/* queued for mmap_lock for write */
		set_stage(STAGE_MAP, delay_us);
		/* takes an active PCC I/O, then queues for mmap_lock */
		set_stage(STAGE_READ, 0);

		pthread_join(w, NULL);
		pthread_join(m, NULL);
		pthread_join(r, NULL);
		munmap(map, 4096);
	} else {
		usage(argv[0]);
	}

	close(fd);
	return 0;
}
