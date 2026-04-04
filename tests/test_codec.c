/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "wardtest.h"

/* Codec dispatch routes XOR correctly */
START_TEST(test_codec_dispatch_xor)
{
	const size_t shard_size = 32;
	const int k = 4, m = 1, n = 5;
	uint8_t *shards[5];

	for (int i = 0; i < n; i++)
		shards[i] = calloc(1, shard_size);
	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 1), shards[i], shard_size);

	wt_codec_encode(WT_CODEC_XOR, shards, k, m, shard_size);
	ck_assert(wt_codec_verify(WT_CODEC_XOR, shards, k, m, shard_size));

	for (int i = 0; i < n; i++)
		free(shards[i]);
}
END_TEST

/* Codec dispatch routes RS correctly */
START_TEST(test_codec_dispatch_rs)
{
	const size_t shard_size = 32;
	const int k = 4, m = 2, n = 6;
	uint8_t *shards[6];

	for (int i = 0; i < n; i++)
		shards[i] = calloc(1, shard_size);
	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 1), shards[i], shard_size);

	wt_codec_encode(WT_CODEC_RS, shards, k, m, shard_size);
	ck_assert(wt_codec_verify(WT_CODEC_RS, shards, k, m, shard_size));

	for (int i = 0; i < n; i++)
		free(shards[i]);
}
END_TEST

Suite *codec_suite(void)
{
	Suite *s = suite_create("Codec");
	TCase *tc = tcase_create("core");

	tcase_add_test(tc, test_codec_dispatch_xor);
	tcase_add_test(tc, test_codec_dispatch_rs);
	suite_add_tcase(s, tc);

	return s;
}
