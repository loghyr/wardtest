/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wardtest.h"

static char tmpdir[] = "/tmp/wt_test_meta_XXXXXX";

static void meta_setup(void)
{
	ck_assert_ptr_nonnull(mkdtemp(tmpdir));
}

static void meta_teardown(void)
{
	char cmd[WT_PATH_BUF];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
	(void)system(cmd);
	strcpy(tmpdir, "/tmp/wt_test_meta_XXXXXX");
}

/* Write then read round-trip */
START_TEST(test_meta_roundtrip)
{
	struct wt_stripe_meta meta = {
		.wsm_magic = WT_META_MAGIC,
		.wsm_version = WT_META_VERSION,
		.wsm_stripe_id = 0xCAFE,
		.wsm_seed = 12345,
		.wsm_k = 4,
		.wsm_m = 2,
		.wsm_shard_size = 4096,
		.wsm_source_size = 16384,
		.wsm_machine_id = 0xBEEF,
		.wsm_created_ns = 999999,
		.wsm_state = WT_STRIPE_ACTIVE,
		.wsm_verify_count = 0,
		.wsm_codec = WT_CODEC_RS,
	};

	int ret = wt_meta_write(tmpdir, &meta);
	ck_assert_int_eq(ret, 0);

	struct wt_stripe_meta out;
	ret = wt_meta_read(tmpdir, 0xCAFE, &out);
	ck_assert_int_eq(ret, 0);
	ck_assert_uint_eq(out.wsm_stripe_id, 0xCAFE);
	ck_assert_uint_eq(out.wsm_seed, 12345);
	ck_assert_uint_eq(out.wsm_k, 4);
	ck_assert_uint_eq(out.wsm_m, 2);
	ck_assert_uint_eq(out.wsm_codec, WT_CODEC_RS);
}
END_TEST

/* Delete removes the meta file */
START_TEST(test_meta_delete)
{
	struct wt_stripe_meta meta = {
		.wsm_magic = WT_META_MAGIC,
		.wsm_version = WT_META_VERSION,
		.wsm_stripe_id = 0x1111,
		.wsm_seed = 1,
		.wsm_k = 2,
		.wsm_m = 1,
		.wsm_shard_size = 512,
		.wsm_source_size = 1024,
		.wsm_machine_id = 0xAAAA,
	};

	ck_assert_int_eq(wt_meta_write(tmpdir, &meta), 0);
	ck_assert_int_eq(wt_meta_delete(tmpdir, 0x1111), 0);

	struct wt_stripe_meta out;
	int ret = wt_meta_read(tmpdir, 0x1111, &out);
	ck_assert_int_lt(ret, 0); /* should fail -- file gone */
}
END_TEST

/* Pick random from empty dir returns 0 */
START_TEST(test_meta_pick_empty)
{
	uint64_t id = wt_meta_pick_random(tmpdir, 42);
	ck_assert_uint_eq(id, 0);
}
END_TEST

/* Pick random from populated dir returns a valid ID */
START_TEST(test_meta_pick_populated)
{
	for (uint64_t i = 1; i <= 5; i++) {
		struct wt_stripe_meta meta = {
			.wsm_magic = WT_META_MAGIC,
			.wsm_version = WT_META_VERSION,
			.wsm_stripe_id = i,
			.wsm_seed = (uint32_t)i,
			.wsm_k = 4,
			.wsm_m = 1,
			.wsm_shard_size = 4096,
			.wsm_source_size = 16384,
		};
		ck_assert_int_eq(wt_meta_write(tmpdir, &meta), 0);
	}

	uint64_t id = wt_meta_pick_random(tmpdir, 99);
	ck_assert_uint_gt(id, 0);
	ck_assert_uint_le(id, 5);
}
END_TEST

Suite *meta_suite(void)
{
	Suite *s = suite_create("Meta");
	TCase *tc = tcase_create("core");

	tcase_add_checked_fixture(tc, meta_setup, meta_teardown);
	tcase_add_test(tc, test_meta_roundtrip);
	tcase_add_test(tc, test_meta_delete);
	tcase_add_test(tc, test_meta_pick_empty);
	tcase_add_test(tc, test_meta_pick_populated);
	suite_add_tcase(s, tc);

	return s;
}
