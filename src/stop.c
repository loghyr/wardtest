/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Stop mechanism — eventfd for local threads, sentinel file for
 * cross-client coordination.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#include "wardtest.h"

volatile sig_atomic_t g_stop;
volatile sig_atomic_t g_stop_reason; /* 0 = signal, 1 = corruption */

static int stop_efd = -1;

int wt_stop_init(void)
{
	g_stop = 0;
	g_stop_reason = 0;
	stop_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	return (stop_efd < 0) ? -errno : 0;
}

void wt_stop_fini(void)
{
	if (stop_efd >= 0) {
		close(stop_efd);
		stop_efd = -1;
	}
}

void wt_stop_corruption(const char *meta_dir, uint64_t stripe_id,
			int shard_idx, uint32_t expected_crc,
			uint32_t actual_crc, uint64_t machine_id)
{
	g_stop = 1;
	g_stop_reason = 1;

	/* Wake local threads */
	if (stop_efd >= 0) {
		uint64_t val = 1;
		(void)write(stop_efd, &val, sizeof(val));
	}

	/* Create sentinel file for cross-client stop */
	char path[WT_MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s", meta_dir, WT_STOP_FILE);

	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd >= 0) {
		char buf[512];
		struct timespec ts;
		char host[256];

		clock_gettime(CLOCK_REALTIME, &ts);
		gethostname(host, sizeof(host));
		host[sizeof(host) - 1] = '\0';

		int n = snprintf(buf, sizeof(buf),
				 "CORRUPTION stripe=%lu shard=%d\n"
				 "machine=0x%016lx host=%s pid=%d\n"
				 "time=%ld.%09ld\n"
				 "expected_crc=0x%08x actual_crc=0x%08x\n",
				 (unsigned long)stripe_id, shard_idx,
				 (unsigned long)machine_id, host,
				 (int)getpid(), (long)ts.tv_sec,
				 (long)ts.tv_nsec, expected_crc, actual_crc);
		(void)write(fd, buf, (size_t)n);
		fsync(fd);
		close(fd);
	}

	fprintf(stderr,
		"\n*** CORRUPTION DETECTED ***\n"
		"stripe=%lu shard=%d\n"
		"expected_crc=0x%08x actual_crc=0x%08x\n"
		"ALL WRITERS STOPPED — files preserved for analysis\n\n",
		(unsigned long)stripe_id, shard_idx, expected_crc,
		actual_crc);
}

bool wt_should_stop(const char *meta_dir)
{
	if (g_stop)
		return true;

	/* Check sentinel file from other clients */
	char path[WT_MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s", meta_dir, WT_STOP_FILE);

	if (access(path, F_OK) == 0) {
		g_stop = 1;
		g_stop_reason = 1;
		return true;
	}

	return false;
}
