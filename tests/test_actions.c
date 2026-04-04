/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Integration tests: create -> verify -> delete pipeline.
 * Exercises the full data path end-to-end.
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wardtest.h"

static char data_dir[] = "/tmp/wt_test_act_data_XXXXXX";
static char meta_dir[] = "/tmp/wt_test_act_meta_XXXXXX";
static char hist_dir[] = "/tmp/wt_test_act_hist_XXXXXX";

static void actions_setup(void)
{
	ck_assert_ptr_nonnull(mkdtemp(data_dir));
	ck_assert_ptr_nonnull(mkdtemp(meta_dir));
	ck_assert_ptr_nonnull(mkdtemp(hist_dir));
	ck_assert_int_eq(wt_stop_init(), 0);
	g_stop = 0;
	g_stop_reason = 0;
}

static void actions_teardown(void)
{
	wt_stop_fini();
	char cmd[WT_PATH_BUF];
	snprintf(cmd, sizeof(cmd), "rm -rf %s %s %s",
		 data_dir, meta_dir, hist_dir);
	(void)system(cmd);
	strcpy(data_dir, "/tmp/wt_test_act_data_XXXXXX");
	strcpy(meta_dir, "/tmp/wt_test_act_meta_XXXXXX");
	strcpy(hist_dir, "/tmp/wt_test_act_hist_XXXXXX");
}

/* Create + verify round-trip with XOR */
START_TEST(test_action_create_verify_xor)
{
	struct wt_config cfg = { .cfg_shard_size = 256, .cfg_k = 4,
		.cfg_m = 1, .cfg_codec = WT_CODEC_XOR };
	strncpy(cfg.cfg_data_dir, data_dir, sizeof(cfg.cfg_data_dir) - 1);
	strncpy(cfg.cfg_meta_dir, meta_dir, sizeof(cfg.cfg_meta_dir) - 1);
	strncpy(cfg.cfg_hist_dir, hist_dir, sizeof(cfg.cfg_hist_dir) - 1);

	uint32_t counter = 0;
	uint64_t mid = 0x1234;

	int ret = wt_action_create(&cfg, mid, &counter, 42);
	ck_assert_int_eq(ret, 0);

	/* Verify the stripe we just created */
	ret = wt_action_verify(&cfg, mid, 42);
	ck_assert_int_eq(ret, 0);
	ck_assert_int_eq(g_stop_reason, 0);
}
END_TEST

/* Create + verify round-trip with RS */
START_TEST(test_action_create_verify_rs)
{
	struct wt_config cfg = { .cfg_shard_size = 256, .cfg_k = 4,
		.cfg_m = 2, .cfg_codec = WT_CODEC_RS };
	strncpy(cfg.cfg_data_dir, data_dir, sizeof(cfg.cfg_data_dir) - 1);
	strncpy(cfg.cfg_meta_dir, meta_dir, sizeof(cfg.cfg_meta_dir) - 1);
	strncpy(cfg.cfg_hist_dir, hist_dir, sizeof(cfg.cfg_hist_dir) - 1);

	uint32_t counter = 0;
	uint64_t mid = 0x5678;

	int ret = wt_action_create(&cfg, mid, &counter, 99);
	ck_assert_int_eq(ret, 0);

	ret = wt_action_verify(&cfg, mid, 99);
	ck_assert_int_eq(ret, 0);
	ck_assert_int_eq(g_stop_reason, 0);
}
END_TEST

/* Create + delete + verify-picks-nothing */
START_TEST(test_action_create_delete)
{
	struct wt_config cfg = { .cfg_shard_size = 128, .cfg_k = 2,
		.cfg_m = 1, .cfg_codec = WT_CODEC_XOR };
	strncpy(cfg.cfg_data_dir, data_dir, sizeof(cfg.cfg_data_dir) - 1);
	strncpy(cfg.cfg_meta_dir, meta_dir, sizeof(cfg.cfg_meta_dir) - 1);
	strncpy(cfg.cfg_hist_dir, hist_dir, sizeof(cfg.cfg_hist_dir) - 1);

	uint32_t counter = 0;
	uint64_t mid = 0xAAAA;

	ck_assert_int_eq(wt_action_create(&cfg, mid, &counter, 77), 0);
	ck_assert_int_eq(wt_action_delete(&cfg, mid, 77), 0);

	/* After delete, verify should find nothing (returns 0) */
	int ret = wt_action_verify(&cfg, mid, 77);
	ck_assert_int_eq(ret, 0);
}
END_TEST

/* Multiple creates, all verify clean */
START_TEST(test_action_multi_create_verify)
{
	struct wt_config cfg = { .cfg_shard_size = 64, .cfg_k = 4,
		.cfg_m = 1, .cfg_codec = WT_CODEC_XOR };
	strncpy(cfg.cfg_data_dir, data_dir, sizeof(cfg.cfg_data_dir) - 1);
	strncpy(cfg.cfg_meta_dir, meta_dir, sizeof(cfg.cfg_meta_dir) - 1);
	strncpy(cfg.cfg_hist_dir, hist_dir, sizeof(cfg.cfg_hist_dir) - 1);

	uint32_t counter = 0;
	uint64_t mid = 0xBBBB;

	for (int i = 0; i < 10; i++) {
		int ret = wt_action_create(&cfg, mid, &counter,
					   (uint32_t)(i + 1));
		ck_assert_int_eq(ret, 0);
	}

	/* Verify several times with different seeds (picks random stripes) */
	for (int i = 0; i < 20; i++) {
		int ret = wt_action_verify(&cfg, mid, (uint32_t)(i + 100));
		ck_assert_int_eq(ret, 0);
	}

	ck_assert_int_eq(g_stop_reason, 0);
}
END_TEST

Suite *actions_suite(void)
{
	Suite *s = suite_create("Actions");
	TCase *tc = tcase_create("core");

	tcase_add_checked_fixture(tc, actions_setup, actions_teardown);
	tcase_add_test(tc, test_action_create_verify_xor);
	tcase_add_test(tc, test_action_create_verify_rs);
	tcase_add_test(tc, test_action_create_delete);
	tcase_add_test(tc, test_action_multi_create_verify);
	suite_add_tcase(s, tc);

	return s;
}
