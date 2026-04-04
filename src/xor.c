/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * XOR parity codec — k data shards + 1 parity shard.
 *
 * Encode: parity = data[0] ^ data[1] ^ ... ^ data[k-1]
 * Verify: XOR all k+1 shards → must be all zeros.
 *
 * The fastest possible erasure code.  Detects any single-shard
 * corruption.  Memory bandwidth limited, no GF math.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "wardtest.h"

void wt_xor_encode(uint8_t **shards, int k, size_t shard_size)
{
	/* shards[k] is the parity output */
	memcpy(shards[k], shards[0], shard_size);

	for (int i = 1; i < k; i++) {
		uint8_t *parity = shards[k];
		const uint8_t *data = shards[i];

		for (size_t j = 0; j < shard_size; j++)
			parity[j] ^= data[j];
	}
}

bool wt_xor_verify(const uint8_t **shards, int n, size_t shard_size)
{
	/*
	 * XOR all n shards (k data + m parity).  If the data is
	 * consistent, the result is all zeros.
	 */
	uint8_t check;

	for (size_t j = 0; j < shard_size; j++) {
		check = 0;
		for (int i = 0; i < n; i++)
			check ^= shards[i][j];
		if (check != 0)
			return false;
	}

	return true;
}
