/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "wardtest.h"

/* Encode then verify should succeed */
START_TEST(test_xor_encode_verify)
{
	const size_t shard_size = 64;
	const int k = 4;
	uint8_t *shards[5];

	for (int i = 0; i < 5; i++)
		shards[i] = calloc(1, shard_size);

	/* Fill data shards with deterministic data */
	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 1), shards[i], shard_size);

	wt_xor_encode(shards, k, shard_size);
	ck_assert(wt_xor_verify((const uint8_t **)shards, 5, shard_size));

	for (int i = 0; i < 5; i++)
		free(shards[i]);
}
END_TEST

/* Corrupt one shard, verify detects it */
START_TEST(test_xor_detect_corruption)
{
	const size_t shard_size = 64;
	const int k = 4;
	uint8_t *shards[5];

	for (int i = 0; i < 5; i++)
		shards[i] = calloc(1, shard_size);

	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 10), shards[i], shard_size);

	wt_xor_encode(shards, k, shard_size);

	/* Flip a bit in shard 2 */
	shards[2][0] ^= 0x01;

	ck_assert(!wt_xor_verify((const uint8_t **)shards, 5, shard_size));

	for (int i = 0; i < 5; i++)
		free(shards[i]);
}
END_TEST

/* k=1: single data shard, parity is a copy */
START_TEST(test_xor_k1)
{
	const size_t shard_size = 32;
	uint8_t *shards[2];

	shards[0] = calloc(1, shard_size);
	shards[1] = calloc(1, shard_size);

	wt_rng_fill(77, shards[0], shard_size);
	wt_xor_encode(shards, 1, shard_size);

	/* Parity should equal data for k=1 */
	ck_assert_mem_eq(shards[0], shards[1], shard_size);
	ck_assert(wt_xor_verify((const uint8_t **)shards, 2, shard_size));

	free(shards[0]);
	free(shards[1]);
}
END_TEST

Suite *xor_suite(void)
{
	Suite *s = suite_create("XOR");
	TCase *tc = tcase_create("core");

	tcase_add_test(tc, test_xor_encode_verify);
	tcase_add_test(tc, test_xor_detect_corruption);
	tcase_add_test(tc, test_xor_k1);
	suite_add_tcase(s, tc);

	return s;
}
