/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Machine ID — unique per process per host.
 * Uses a simple hash of hostname + pid.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "wardtest.h"

/*
 * FNV-1a 64-bit hash — public domain, no dependencies.
 */
static uint64_t fnv1a_64(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint64_t h = 0xcbf29ce484222325ULL;

	for (size_t i = 0; i < len; i++) {
		h ^= p[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

uint64_t wt_machine_id(void)
{
	char buf[512];
	char host[256];

	if (gethostname(host, sizeof(host)) < 0) {
		fprintf(stderr, "wardtest: gethostname: %s\n",
			strerror(errno));
		strncpy(host, "unknown", sizeof(host) - 1);
	}
	host[sizeof(host) - 1] = '\0';

	pid_t pid = getpid();
	int n = snprintf(buf, sizeof(buf), "%s:%d", host, (int)pid);
	if (n < 0 || (size_t)n >= sizeof(buf)) {
		fprintf(stderr, "wardtest: machine_id buffer truncated\n");
		n = (int)strlen(buf);
	}

	return fnv1a_64(buf, (size_t)n);
}
