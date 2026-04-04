/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <check.h>
#include <string.h>
#include "wardtest.h"

/* Same seed always produces the same output */
START_TEST(test_rng_deterministic)
{
	uint8_t buf1[256], buf2[256];

	wt_rng_fill(42, buf1, sizeof(buf1));
	wt_rng_fill(42, buf2, sizeof(buf2));
	ck_assert_mem_eq(buf1, buf2, sizeof(buf1));
}
END_TEST

/* Different seeds produce different output */
START_TEST(test_rng_different_seeds)
{
	uint8_t buf1[256], buf2[256];

	wt_rng_fill(1, buf1, sizeof(buf1));
	wt_rng_fill(2, buf2, sizeof(buf2));
	ck_assert_mem_ne(buf1, buf2, sizeof(buf1));
}
END_TEST

/* Small fills (< 4 bytes) work correctly */
START_TEST(test_rng_small_fill)
{
	uint8_t buf1[3], buf2[3];

	wt_rng_fill(99, buf1, sizeof(buf1));
	wt_rng_fill(99, buf2, sizeof(buf2));
	ck_assert_mem_eq(buf1, buf2, sizeof(buf1));
}
END_TEST

/* Zero-length fill doesn't crash */
START_TEST(test_rng_zero_length)
{
	uint8_t buf[4] = { 0xAA, 0xBB, 0xCC, 0xDD };

	wt_rng_fill(1, buf, 0);
	/* buf should be unchanged */
	ck_assert_int_eq(buf[0], 0xAA);
}
END_TEST

Suite *rng_suite(void)
{
	Suite *s = suite_create("RNG");
	TCase *tc = tcase_create("core");

	tcase_add_test(tc, test_rng_deterministic);
	tcase_add_test(tc, test_rng_different_seeds);
	tcase_add_test(tc, test_rng_small_fill);
	tcase_add_test(tc, test_rng_zero_length);
	suite_add_tcase(s, tc);

	return s;
}
