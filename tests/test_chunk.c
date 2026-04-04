/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <check.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wardtest.h"

static char tmpdir[] = "/tmp/wt_test_chunk_XXXXXX";

static void chunk_setup(void)
{
	ck_assert_ptr_nonnull(mkdtemp(tmpdir));
}

static void chunk_teardown(void)
{
	char cmd[WT_PATH_BUF];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
	(void)system(cmd);
	/* Reset template for next test */
	strcpy(tmpdir, "/tmp/wt_test_chunk_XXXXXX");
}

/* Write then read round-trip */
START_TEST(test_chunk_roundtrip)
{
	uint8_t data[128];
	wt_rng_fill(42, data, sizeof(data));

	struct wt_chunk_header hdr = {
		.wch_magic = WT_CHUNK_MAGIC,
		.wch_version = WT_CHUNK_VERSION,
		.wch_crc32 = wt_crc32(data, sizeof(data)),
		.wch_shard_size = sizeof(data),
		.wch_stripe_id = 0x1234,
		.wch_shard_index = 0,
		.wch_k = 4,
		.wch_m = 1,
		.wch_seed = 42,
		.wch_machine_id = 0xAAAA,
		.wch_timestamp = 1000,
	};

	int ret = wt_chunk_write(tmpdir, 0x1234, 0, &hdr,
				 data, sizeof(data));
	ck_assert_int_eq(ret, 0);

	struct wt_chunk_header hdr_out;
	void *data_out = NULL;
	size_t data_len = 0;

	ret = wt_chunk_read(tmpdir, 0x1234, 0,
			    &hdr_out, &data_out, &data_len);
	ck_assert_int_eq(ret, 0);
	ck_assert_uint_eq(data_len, sizeof(data));
	ck_assert_mem_eq(data_out, data, sizeof(data));
	ck_assert_uint_eq(hdr_out.wch_seed, 42);
	ck_assert_uint_eq(hdr_out.wch_stripe_id, 0x1234);

	free(data_out);
}
END_TEST

/* Read of nonexistent shard returns error */
START_TEST(test_chunk_read_missing)
{
	struct wt_chunk_header hdr;
	void *data = NULL;
	size_t len = 0;

	int ret = wt_chunk_read(tmpdir, 0x9999, 0, &hdr, &data, &len);
	ck_assert_int_lt(ret, 0);
	ck_assert_ptr_null(data);
}
END_TEST

/* CRC mismatch detected on read */
START_TEST(test_chunk_crc_mismatch)
{
	uint8_t data[64];
	wt_rng_fill(99, data, sizeof(data));

	struct wt_chunk_header hdr = {
		.wch_magic = WT_CHUNK_MAGIC,
		.wch_version = WT_CHUNK_VERSION,
		.wch_crc32 = 0xDEADBEEF, /* wrong CRC */
		.wch_shard_size = sizeof(data),
		.wch_stripe_id = 0xABCD,
		.wch_shard_index = 0,
		.wch_k = 4,
		.wch_m = 1,
		.wch_seed = 99,
		.wch_machine_id = 0xBBBB,
		.wch_timestamp = 2000,
	};

	int ret = wt_chunk_write(tmpdir, 0xABCD, 0, &hdr,
				 data, sizeof(data));
	ck_assert_int_eq(ret, 0);

	struct wt_chunk_header hdr_out;
	void *data_out = NULL;
	size_t len = 0;

	ret = wt_chunk_read(tmpdir, 0xABCD, 0, &hdr_out, &data_out, &len);
	ck_assert_int_eq(ret, -EILSEQ);
	/* Data is returned for diagnostics even on CRC mismatch */
	ck_assert_ptr_nonnull(data_out);
	free(data_out);
}
END_TEST

Suite *chunk_suite(void)
{
	Suite *s = suite_create("Chunk");
	TCase *tc = tcase_create("core");

	tcase_add_checked_fixture(tc, chunk_setup, chunk_teardown);
	tcase_add_test(tc, test_chunk_roundtrip);
	tcase_add_test(tc, test_chunk_read_missing);
	tcase_add_test(tc, test_chunk_crc_mismatch);
	suite_add_tcase(s, tc);

	return s;
}
