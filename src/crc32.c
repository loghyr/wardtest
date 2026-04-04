/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * CRC32 — ISO 3309 / ITU-T V.42 polynomial.
 * Table-driven, no dependencies.
 */

#include <stdint.h>
#include <stddef.h>

#include "wardtest.h"

static uint32_t crc_table[256];
static int crc_table_init;

static void crc32_build_table(void)
{
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int j = 0; j < 8; j++) {
			if (c & 1)
				c = 0xedb88320 ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc_table[i] = c;
	}
	crc_table_init = 1;
}

uint32_t wt_crc32(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint32_t crc = 0xffffffff;

	if (!crc_table_init)
		crc32_build_table();

	for (size_t i = 0; i < len; i++)
		crc = crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);

	return crc ^ 0xffffffff;
}
