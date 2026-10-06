/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

/*
 * Locking -- POSIX byte-range lock stress against the server lock manager.
 *
 * A small fixed set of "hotspot" stripes (see wardtest.h) is shared by
 * every client.  Each lock action read-modify-writes one hotspot in
 * place, serialized by a POSIX advisory byte-range lock (fcntl F_SETLKW,
 * one byte per hotspot) on a shared lock file.  Over NFSv4 those become
 * LOCK/LOCKU against the server; over v3, NLM.
 *
 * POSIX record locks are per-process, so they do not serialize this
 * program's own worker threads -- a per-hotspot process-local mutex does
 * that.  The fcntl lock is therefore the cross-client primitive under
 * test: if the server's lock manager wrongly lets two clients hold the
 * same byte at once, their RMWs interleave and tear the stripe, which the
 * read-back's EC/CRC/seed check reports as corruption.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wardtest.h"

/* Shared lock file (one open description for this process) and the
 * process-local mutexes that serialize sibling threads per hotspot. */
static int g_lock_fd = -1;
static pthread_mutex_t g_hot_mtx[WT_LOCK_HOTSPOT_COUNT];

int wt_lock_init(const char *meta_dir)
{
	char path[WT_PATH_BUF];

	if (snprintf(path, sizeof(path), "%s/%s", meta_dir, WT_LOCK_FILE) >=
	    (int)sizeof(path))
		return -ENAMETOOLONG;

	g_lock_fd = open(path, O_RDWR | O_CREAT, 0644);
	if (g_lock_fd < 0)
		return -errno;

	/* One byte per hotspot; locking past EOF is legal but keep it tidy. */
	if (ftruncate(g_lock_fd, WT_LOCK_HOTSPOT_COUNT) < 0)
		fprintf(stderr, "wardtest: ftruncate(%s): %s\n",
			path, strerror(errno));

	for (int i = 0; i < WT_LOCK_HOTSPOT_COUNT; i++) {
		int err = pthread_mutex_init(&g_hot_mtx[i], NULL);
		if (err) {
			while (--i >= 0)
				pthread_mutex_destroy(&g_hot_mtx[i]);
			close(g_lock_fd);
			g_lock_fd = -1;
			return -err;
		}
	}

	return 0;
}

void wt_lock_fini(void)
{
	if (g_lock_fd < 0)
		return;

	for (int i = 0; i < WT_LOCK_HOTSPOT_COUNT; i++)
		pthread_mutex_destroy(&g_hot_mtx[i]);

	if (close(g_lock_fd) < 0)
		fprintf(stderr, "wardtest: close lock file: %s\n",
			strerror(errno));
	g_lock_fd = -1;
}

/* Advance a seed deterministically (same LCG as the worker loop). */
static uint32_t next_seed(uint32_t s)
{
	return s * 1103515245U + 12345U;
}

int wt_action_lock(const struct wt_config *cfg, uint64_t machine_id,
		   uint32_t op_seed)
{
	if (g_lock_fd < 0)
		return -EINVAL;

	int h = (int)(op_seed % WT_LOCK_HOTSPOT_COUNT);
	uint64_t stripe_id = WT_LOCK_HOTSPOT_BASE | (uint64_t)h;

	struct flock fl = {
		.l_type   = F_WRLCK,
		.l_whence = SEEK_SET,
		.l_start  = h,   /* one byte-range per hotspot */
		.l_len    = 1,
	};

	/* Serialize sibling threads first: POSIX locks don't distinguish
	 * threads of one process, so without this two of our own threads
	 * on the same hotspot would race and self-corrupt. */
	pthread_mutex_lock(&g_hot_mtx[h]);

	/* Cross-client exclusion: the primitive actually under test. */
	int r;
	do {
		r = fcntl(g_lock_fd, F_SETLKW, &fl);
	} while (r < 0 && errno == EINTR && !g_stop);

	if (r < 0) {
		int ret = -errno;
		pthread_mutex_unlock(&g_hot_mtx[h]);
		return ret;
	}

	int ret = 0;
	bool wrote = false;
	uint32_t new_seed = 0;

	/*
	 * F_SETLKW may have blocked on another client; re-check for shutdown
	 * after acquiring so we don't start an RMW we're about to abandon.
	 */
	if (g_stop)
		goto unlock;

	/*
	 * Read-modify-write under the lock.  A consistent read of an existing
	 * hotspot means rewrite it; a missing hotspot means first touch, so
	 * create it; -EILSEQ means the locked read already found (and
	 * reported) a torn stripe -- a lock-manager failure.
	 *
	 * The replacement seed is made writer-distinct (machine_id ^ op_seed).
	 * If a broken lock manager ever lets two clients hold this hotspot at
	 * once, they must write DIFFERENT data -- otherwise the interleave
	 * lands on identical bytes and the EC/CRC/seed check cannot see the
	 * lost update.  machine_id separates processes; op_seed separates this
	 * process's own threads and iterations.
	 *
	 * NOTE: wt_stripe_write commits the k+m shards then the metadata as
	 * separate atomic renames, so a mid-RMW I/O failure (e.g. ENOSPC in
	 * the FULL state) can leave a torn hotspot that the NEXT locked verify
	 * reports as -EILSEQ corruption even though locking worked.  Triage a
	 * stop that coincides with a write error as an I/O failure, not a
	 * lock-manager tear.
	 */
	{
		uint32_t entropy = op_seed ^ (uint32_t)machine_id ^
				   (uint32_t)(machine_id >> 32);
		uint32_t cur_seed = 0;

		ret = wt_stripe_verify(cfg, machine_id, stripe_id, &cur_seed);
		if (ret == 0)
			new_seed = next_seed(cur_seed ^ entropy);
		else if (ret == -ENOENT)
			new_seed = next_seed(op_seed ^
					     ((uint32_t)h * 2654435761U));

		if (ret == 0 || ret == -ENOENT) {
			ret = wt_stripe_write(cfg, machine_id, stripe_id,
					      new_seed);
			wrote = (ret == 0);
		}
	}

unlock:
	/* Release the byte-range lock, then the thread mutex. */
	fl.l_type = F_UNLCK;
	if (fcntl(g_lock_fd, F_SETLK, &fl) < 0)
		fprintf(stderr, "wardtest: unlock hotspot %d: %s\n",
			h, strerror(errno));

	pthread_mutex_unlock(&g_hot_mtx[h]);

	if (wrote)
		wt_history_append(cfg->cfg_hist_dir, machine_id,
				  WT_ACTION_LOCK, stripe_id, new_seed, true);

	return ret;
}
