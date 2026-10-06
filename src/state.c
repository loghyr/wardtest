/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

/*
 * Filesystem state machine -- adapts operation weights based on
 * how full the filesystem is.
 */

#include <sys/statvfs.h>

#include "wardtest.h"

enum wt_fs_state wt_state_check(const char *path)
{
	struct statvfs sv;

	if (statvfs(path, &sv) < 0)
		return WT_STATE_NORMAL;

	if (sv.f_blocks == 0)
		return WT_STATE_NORMAL;

	unsigned long used_pct =
		100 - (sv.f_bavail * 100 / sv.f_blocks);

	if (used_pct < 10)
		return WT_STATE_EMPTY;
	if (used_pct > 90)
		return WT_STATE_FULL;

	return WT_STATE_NORMAL;
}

/*
 * Action weight tables.  Each row sums to 100.
 *
 *            CREATE  READ  WRITE  DELETE  VERIFY  LOCK
 * EMPTY:       55     10     10      5     15      5
 * NORMAL:      18     22     22     10     18     10
 * FULL:         0     18     18     38     16     10
 *
 * EMPTY has non-zero delete/write/verify/lock weights so all code paths
 * are exercised even on large filesystems that never leave EMPTY.  LOCK
 * read-modify-writes a shared hotspot stripe under a byte-range lock and
 * does not grow the filesystem, so it carries weight in every state.
 */
static const int weights[3][WT_ACTION_COUNT] = {
	{ 55, 10, 10,  5, 15,  5 },   /* EMPTY */
	{ 18, 22, 22, 10, 18, 10 },   /* NORMAL */
	{  0, 18, 18, 38, 16, 10 },   /* FULL */
};

enum wt_action wt_state_pick_action(enum wt_fs_state state,
				    uint32_t random_val,
				    bool verify_only)
{
	if (verify_only)
		return WT_ACTION_VERIFY;

	int r = (int)(random_val % 100);
	const int *w = weights[state];
	int cumulative = 0;

	for (int i = 0; i < WT_ACTION_COUNT; i++) {
		cumulative += w[i];
		if (r < cumulative)
			return (enum wt_action)i;
	}

	return WT_ACTION_VERIFY;
}
