/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * History logger -- per-client append-only log.
 *
 * Uses O_APPEND for atomic writes (guaranteed atomic for writes
 * <= PIPE_BUF on Linux).  Each record is a single line < 256 bytes.
 *
 * History logging is best-effort -- failures are reported to stderr
 * but do not stop the test.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wardtest.h"

static const char *action_names[] = {
	"CREATE", "READ", "WRITE", "DELETE", "VERIFY",
};

void wt_history_append(const char *dir, uint64_t machine_id,
		       enum wt_action action, uint64_t stripe_id,
		       uint32_t seed, bool success)
{
	char path[WT_PATH_BUF];
	char buf[256];
	struct timespec ts;

	int pn = snprintf(path, sizeof(path), "%s/history_%016lx.log",
			  dir, (unsigned long)machine_id);
	if (pn < 0 || (size_t)pn >= sizeof(path)) {
		fprintf(stderr, "wardtest: history path truncated\n");
		return;
	}

	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		fprintf(stderr, "wardtest: clock_gettime: %s\n",
			strerror(errno));
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
	}

	int n = snprintf(buf, sizeof(buf), "%ld.%09ld %-6s stripe=%016lx "
			 "seed=0x%08x %s\n",
			 (long)ts.tv_sec, (long)ts.tv_nsec,
			 action_names[action],
			 (unsigned long)stripe_id, seed,
			 success ? "OK" : "FAIL");
	if (n < 0 || (size_t)n >= sizeof(buf)) {
		fprintf(stderr, "wardtest: history line truncated\n");
		return;
	}

	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0) {
		fprintf(stderr, "wardtest: open(%s): %s\n",
			path, strerror(errno));
		return;
	}

	ssize_t wr = write(fd, buf, (size_t)n);
	if (wr != (ssize_t)n)
		fprintf(stderr, "wardtest: write history: %s\n",
			wr < 0 ? strerror(errno) : "short write");

	if (close(fd) < 0)
		fprintf(stderr, "wardtest: close history: %s\n",
			strerror(errno));
}
