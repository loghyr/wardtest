/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

#include <check.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wardtest.h"

static char tmpdir[] = "/tmp/wt_test_ctl_XXXXXX";

static void ctl_setup(void)
{
	ck_assert_ptr_nonnull(mkdtemp(tmpdir));
}

static void ctl_teardown(void)
{
	char cmd[WT_PATH_BUF];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
	(void)system(cmd);
	strcpy(tmpdir, "/tmp/wt_test_ctl_XXXXXX");
}

/* First sync creates the control file */
START_TEST(test_control_create)
{
	struct wt_config cfg = {
		.cfg_k = 4,
		.cfg_m = 1,
		.cfg_shard_size = 4096,
		.cfg_codec = WT_CODEC_XOR,
	};

	int ret = wt_control_sync(tmpdir, &cfg);
	ck_assert_int_eq(ret, 0);

	/* Verify file exists */
	char path[WT_PATH_BUF];
	snprintf(path, sizeof(path), "%s/%s", tmpdir, WT_CONTROL_FILE);
	ck_assert_int_eq(access(path, F_OK), 0);
}
END_TEST

/* Second sync with same params succeeds */
START_TEST(test_control_match)
{
	struct wt_config cfg = {
		.cfg_k = 4,
		.cfg_m = 2,
		.cfg_shard_size = 8192,
		.cfg_codec = WT_CODEC_RS,
	};

	ck_assert_int_eq(wt_control_sync(tmpdir, &cfg), 0);
	ck_assert_int_eq(wt_control_sync(tmpdir, &cfg), 0);
}
END_TEST

/* Mismatch returns -EINVAL */
START_TEST(test_control_mismatch)
{
	struct wt_config cfg1 = {
		.cfg_k = 4,
		.cfg_m = 1,
		.cfg_shard_size = 4096,
		.cfg_codec = WT_CODEC_XOR,
	};
	struct wt_config cfg2 = {
		.cfg_k = 8,
		.cfg_m = 2,
		.cfg_shard_size = 4096,
		.cfg_codec = WT_CODEC_RS,
	};

	ck_assert_int_eq(wt_control_sync(tmpdir, &cfg1), 0);
	ck_assert_int_eq(wt_control_sync(tmpdir, &cfg2), -EINVAL);
}
END_TEST

/* Verify-only client adopts control file params */
START_TEST(test_control_adopt)
{
	struct wt_config writer = {
		.cfg_k = 4,
		.cfg_m = 2,
		.cfg_shard_size = 8192,
		.cfg_codec = WT_CODEC_RS,
	};
	ck_assert_int_eq(wt_control_sync(tmpdir, &writer), 0);

	struct wt_config verifier = {
		.cfg_k = 1,       /* different */
		.cfg_m = 1,       /* different */
		.cfg_shard_size = 512, /* different */
		.cfg_codec = WT_CODEC_XOR, /* different */
		.cfg_verify_only = true,
	};
	ck_assert_int_eq(wt_control_sync(tmpdir, &verifier), 0);

	/* Verifier should now have writer's params */
	ck_assert_int_eq(verifier.cfg_k, 4);
	ck_assert_int_eq(verifier.cfg_m, 2);
	ck_assert_uint_eq(verifier.cfg_shard_size, 8192);
	ck_assert_int_eq(verifier.cfg_codec, WT_CODEC_RS);
}
END_TEST

Suite *control_suite(void)
{
	Suite *s = suite_create("Control");
	TCase *tc = tcase_create("core");

	tcase_add_checked_fixture(tc, ctl_setup, ctl_teardown);
	tcase_add_test(tc, test_control_create);
	tcase_add_test(tc, test_control_match);
	tcase_add_test(tc, test_control_mismatch);
	tcase_add_test(tc, test_control_adopt);
	suite_add_tcase(s, tc);

	return s;
}
