/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "wardtest.h"

/* RS encode then verify should succeed */
START_TEST(test_rs_encode_verify)
{
	const size_t shard_size = 64;
	const int k = 4, m = 2, n = 6;
	uint8_t *shards[6];

	for (int i = 0; i < n; i++)
		shards[i] = calloc(1, shard_size);

	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 1), shards[i], shard_size);

	wt_rs_encode(shards, k, m, shard_size);
	ck_assert(wt_rs_verify(shards, k, m, shard_size));

	for (int i = 0; i < n; i++)
		free(shards[i]);
}
END_TEST

/* RS detects single-shard corruption */
START_TEST(test_rs_detect_corruption)
{
	const size_t shard_size = 64;
	const int k = 4, m = 2, n = 6;
	uint8_t *shards[6];

	for (int i = 0; i < n; i++)
		shards[i] = calloc(1, shard_size);

	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 10), shards[i], shard_size);

	wt_rs_encode(shards, k, m, shard_size);

	/* Corrupt data shard 1 */
	shards[1][0] ^= 0xFF;
	ck_assert(!wt_rs_verify(shards, k, m, shard_size));

	for (int i = 0; i < n; i++)
		free(shards[i]);
}
END_TEST

/* RS detects parity shard corruption */
START_TEST(test_rs_detect_parity_corruption)
{
	const size_t shard_size = 32;
	const int k = 4, m = 2, n = 6;
	uint8_t *shards[6];

	for (int i = 0; i < n; i++)
		shards[i] = calloc(1, shard_size);

	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 20), shards[i], shard_size);

	wt_rs_encode(shards, k, m, shard_size);

	/* Corrupt parity shard */
	shards[k][15] ^= 0x42;
	ck_assert(!wt_rs_verify(shards, k, m, shard_size));

	for (int i = 0; i < n; i++)
		free(shards[i]);
}
END_TEST

/* RS with m=1 should also work */
START_TEST(test_rs_m1)
{
	const size_t shard_size = 32;
	const int k = 4, m = 1, n = 5;
	uint8_t *shards[5];

	for (int i = 0; i < n; i++)
		shards[i] = calloc(1, shard_size);

	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 30), shards[i], shard_size);

	wt_rs_encode(shards, k, m, shard_size);
	ck_assert(wt_rs_verify(shards, k, m, shard_size));

	for (int i = 0; i < n; i++)
		free(shards[i]);
}
END_TEST

/* RS with k=2, m=3 (more parity than data) */
START_TEST(test_rs_more_parity)
{
	const size_t shard_size = 16;
	const int k = 2, m = 3, n = 5;
	uint8_t *shards[5];

	for (int i = 0; i < n; i++)
		shards[i] = calloc(1, shard_size);

	for (int i = 0; i < k; i++)
		wt_rng_fill((uint32_t)(i + 40), shards[i], shard_size);

	wt_rs_encode(shards, k, m, shard_size);
	ck_assert(wt_rs_verify(shards, k, m, shard_size));

	for (int i = 0; i < n; i++)
		free(shards[i]);
}
END_TEST

Suite *rs_suite(void)
{
	Suite *s = suite_create("RS");
	TCase *tc = tcase_create("core");

	tcase_add_test(tc, test_rs_encode_verify);
	tcase_add_test(tc, test_rs_detect_corruption);
	tcase_add_test(tc, test_rs_detect_parity_corruption);
	tcase_add_test(tc, test_rs_m1);
	tcase_add_test(tc, test_rs_more_parity);
	suite_add_tcase(s, tc);

	return s;
}
