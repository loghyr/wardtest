/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

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
 *            CREATE  READ  WRITE  DELETE  VERIFY
 * EMPTY:       80      5      5      0     10
 * NORMAL:      20     25     25     10     20
 * FULL:         0     20     20     40     20
 */
static const int weights[3][5] = {
	{ 80,  5,  5,  0, 10 },   /* EMPTY */
	{ 20, 25, 25, 10, 20 },   /* NORMAL */
	{  0, 20, 20, 40, 20 },   /* FULL */
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

	for (int i = 0; i < 5; i++) {
		cumulative += w[i];
		if (r < cumulative)
			return (enum wt_action)i;
	}

	return WT_ACTION_VERIFY;
}
