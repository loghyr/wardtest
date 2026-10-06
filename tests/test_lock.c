/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

/*
 * Lock tests: hotspot read-modify-write under the byte-range lock.
 *
 * These run single-process, so the fcntl() lock never contends (POSIX
 * locks are per-process); what they cover is the RMW self-consistency,
 * the hotspot namespace staying out of the verify/delete picker, and the
 * per-hotspot mutex serializing sibling threads of one process.  True
 * cross-client lock-manager stress needs several wardtest processes
 * pointed at one shared (NFS) mount, where the fcntl locks contend at the
 * server -- not covered by this single-process unit suite.
 */

#include <check.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wardtest.h"

static char data_dir[] = "/tmp/wt_test_lock_data_XXXXXX";
static char meta_dir[] = "/tmp/wt_test_lock_meta_XXXXXX";
static char hist_dir[] = "/tmp/wt_test_lock_hist_XXXXXX";

static void lock_setup(void)
{
	ck_assert_ptr_nonnull(mkdtemp(data_dir));
	ck_assert_ptr_nonnull(mkdtemp(meta_dir));
	ck_assert_ptr_nonnull(mkdtemp(hist_dir));
	ck_assert_int_eq(wt_stop_init(), 0);
	ck_assert_int_eq(wt_lock_init(meta_dir), 0);
	g_stop = 0;
	g_stop_reason = 0;
}

static void lock_teardown(void)
{
	wt_lock_fini();
	wt_stop_fini();
	char cmd[WT_PATH_BUF];
	snprintf(cmd, sizeof(cmd), "rm -rf %s %s %s",
		 data_dir, meta_dir, hist_dir);
	(void)system(cmd);
	strcpy(data_dir, "/tmp/wt_test_lock_data_XXXXXX");
	strcpy(meta_dir, "/tmp/wt_test_lock_meta_XXXXXX");
	strcpy(hist_dir, "/tmp/wt_test_lock_hist_XXXXXX");
}

static void fill_cfg(struct wt_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->cfg_shard_size = 256;
	cfg->cfg_k = 4;
	cfg->cfg_m = 1;
	cfg->cfg_codec = WT_CODEC_XOR;
	strncpy(cfg->cfg_data_dir, data_dir, sizeof(cfg->cfg_data_dir) - 1);
	strncpy(cfg->cfg_meta_dir, meta_dir, sizeof(cfg->cfg_meta_dir) - 1);
	strncpy(cfg->cfg_hist_dir, hist_dir, sizeof(cfg->cfg_hist_dir) - 1);
}

/* Repeated RMW of the hotspots stays consistent (first touch then modify). */
START_TEST(test_lock_rmw_consistent)
{
	struct wt_config cfg;
	fill_cfg(&cfg);
	uint64_t mid = 0xC0FFEE;

	for (int i = 0; i < 200; i++) {
		int ret = wt_action_lock(&cfg, mid, (uint32_t)(i + 1));
		ck_assert_int_eq(ret, 0);
	}
	ck_assert_int_eq(g_stop_reason, 0);

	/* Every hotspot that was touched reads back clean. */
	for (int h = 0; h < WT_LOCK_HOTSPOT_COUNT; h++) {
		uint64_t id = WT_LOCK_HOTSPOT_BASE | (uint64_t)h;
		int ret = wt_stripe_verify(&cfg, mid, id, NULL);
		ck_assert(ret == 0 || ret == -ENOENT);
	}
	ck_assert_int_eq(g_stop_reason, 0);
}
END_TEST

/* The verify/delete picker must never return a hotspot stripe. */
START_TEST(test_lock_hotspots_excluded_from_pick)
{
	struct wt_config cfg;
	fill_cfg(&cfg);
	uint64_t mid = 0xD00D;
	uint32_t counter = 0;

	/* Some ordinary stripes so the picker has candidates. */
	for (int i = 0; i < 8; i++)
		ck_assert_int_eq(wt_action_create(&cfg, mid, &counter,
						  (uint32_t)(i + 1)), 0);

	/* Touch every hotspot so their .meta files exist in meta_dir. */
	for (int h = 0; h < WT_LOCK_HOTSPOT_COUNT; h++)
		ck_assert_int_eq(wt_action_lock(&cfg, mid, (uint32_t)h), 0);

	for (uint32_t s = 0; s < 500; s++) {
		uint64_t id = wt_meta_pick_random(meta_dir, s);
		ck_assert_msg((id & WT_LOCK_HOTSPOT_MASK) != WT_LOCK_HOTSPOT_BASE,
			      "picker returned hotspot id %016lx",
			      (unsigned long)id);
	}
}
END_TEST

struct hammer_arg {
	const struct wt_config *cfg;
	uint64_t mid;
	uint32_t base_seed;
	int ret;
};

static void *hammer(void *p)
{
	struct hammer_arg *a = p;

	a->ret = 0;
	for (int i = 0; i < 100; i++) {
		int r = wt_action_lock(a->cfg, a->mid, a->base_seed + (uint32_t)i);
		if (r != 0) {
			a->ret = r;
			return NULL;
		}
	}
	return NULL;
}

/* Sibling threads of one process RMW overlapping hotspots without tearing
 * -- the per-hotspot mutex must serialize them (POSIX locks would not). */
START_TEST(test_lock_threads_consistent)
{
	struct wt_config cfg;
	fill_cfg(&cfg);
	uint64_t mid = 0xBEEF;

	enum { NTHREADS = 6 };
	pthread_t th[NTHREADS];
	struct hammer_arg args[NTHREADS];

	for (int i = 0; i < NTHREADS; i++) {
		args[i].cfg = &cfg;
		args[i].mid = mid;
		/* Seeds chosen so threads collide on the same hotspots. */
		args[i].base_seed = (uint32_t)(i * 7);
		args[i].ret = 0;
		ck_assert_int_eq(pthread_create(&th[i], NULL, hammer,
						&args[i]), 0);
	}

	for (int i = 0; i < NTHREADS; i++) {
		ck_assert_int_eq(pthread_join(th[i], NULL), 0);
		ck_assert_int_eq(args[i].ret, 0);
	}

	ck_assert_int_eq(g_stop_reason, 0);

	for (int h = 0; h < WT_LOCK_HOTSPOT_COUNT; h++) {
		uint64_t id = WT_LOCK_HOTSPOT_BASE | (uint64_t)h;
		int ret = wt_stripe_verify(&cfg, mid, id, NULL);
		ck_assert(ret == 0 || ret == -ENOENT);
	}
	ck_assert_int_eq(g_stop_reason, 0);
}
END_TEST

Suite *lock_suite(void)
{
	Suite *s = suite_create("Lock");
	TCase *tc = tcase_create("core");

	tcase_add_checked_fixture(tc, lock_setup, lock_teardown);
	tcase_add_test(tc, test_lock_rmw_consistent);
	tcase_add_test(tc, test_lock_hotspots_excluded_from_pick);
	tcase_add_test(tc, test_lock_threads_consistent);
	suite_add_tcase(s, tc);

	return s;
}
