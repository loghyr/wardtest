/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

#include <check.h>
#include <string.h>
#include "wardtest.h"

/* Known CRC32 value for "123456789" (ISO 3309) */
START_TEST(test_crc32_known_value)
{
	const char *data = "123456789";
	uint32_t crc = wt_crc32(data, 9);

	ck_assert_uint_eq(crc, 0xCBF43926);
}
END_TEST

/* Same data always produces the same CRC */
START_TEST(test_crc32_deterministic)
{
	uint8_t data[128];
	memset(data, 0x42, sizeof(data));

	uint32_t crc1 = wt_crc32(data, sizeof(data));
	uint32_t crc2 = wt_crc32(data, sizeof(data));
	ck_assert_uint_eq(crc1, crc2);
}
END_TEST

/* Different data produces different CRCs */
START_TEST(test_crc32_different_data)
{
	uint8_t data1[64], data2[64];
	memset(data1, 0x00, sizeof(data1));
	memset(data2, 0xFF, sizeof(data2));

	ck_assert_uint_ne(wt_crc32(data1, sizeof(data1)),
			  wt_crc32(data2, sizeof(data2)));
}
END_TEST

/* Single bit flip changes the CRC */
START_TEST(test_crc32_bit_sensitivity)
{
	uint8_t data[32];
	memset(data, 0, sizeof(data));

	uint32_t crc1 = wt_crc32(data, sizeof(data));
	data[15] ^= 0x01;
	uint32_t crc2 = wt_crc32(data, sizeof(data));

	ck_assert_uint_ne(crc1, crc2);
}
END_TEST

Suite *crc32_suite(void)
{
	Suite *s = suite_create("CRC32");
	TCase *tc = tcase_create("core");

	tcase_add_test(tc, test_crc32_known_value);
	tcase_add_test(tc, test_crc32_deterministic);
	tcase_add_test(tc, test_crc32_different_data);
	tcase_add_test(tc, test_crc32_bit_sensitivity);
	suite_add_tcase(s, tc);

	return s;
}
