/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Codec dispatch — route encode/verify to XOR or RS based on config.
 */

#include <stdint.h>
#include <stdbool.h>

#include "wardtest.h"

void wt_codec_encode(enum wt_codec codec, uint8_t **shards,
		     int k, int m, size_t shard_size)
{
	switch (codec) {
	case WT_CODEC_RS:
		wt_rs_encode(shards, k, m, shard_size);
		break;
	case WT_CODEC_XOR:
	default:
		wt_xor_encode(shards, k, shard_size);
		break;
	}
}

bool wt_codec_verify(enum wt_codec codec, uint8_t **shards,
		     int k, int m, size_t shard_size)
{
	switch (codec) {
	case WT_CODEC_RS:
		return wt_rs_verify(shards, k, m, shard_size);
	case WT_CODEC_XOR:
	default:
		return wt_xor_verify((const uint8_t **)shards,
				     k + m, shard_size);
	}
}
