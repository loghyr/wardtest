/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Deterministic RNG for wardtest.
 *
 * Given the same seed, always produces the same output.  Used to
 * generate source data for stripes -- the verifier regenerates the
 * expected data from the seed stored in the metadata.
 *
 * Uses a simple xoshiro128** generator seeded from the input seed.
 * Fast, deterministic, good distribution.  Not cryptographic.
 */

#include <stdint.h>
#include <string.h>

#include "wardtest.h"

/* xoshiro128** state */
struct rng_state {
	uint32_t s[4];
};

static inline uint32_t rotl(uint32_t x, int k)
{
	return (x << k) | (x >> (32 - k));
}

static uint32_t rng_next(struct rng_state *st)
{
	uint32_t result = rotl(st->s[1] * 5, 7) * 9;
	uint32_t t = st->s[1] << 9;

	st->s[2] ^= st->s[0];
	st->s[3] ^= st->s[1];
	st->s[1] ^= st->s[2];
	st->s[0] ^= st->s[3];
	st->s[2] ^= t;
	st->s[3] = rotl(st->s[3], 11);

	return result;
}

static void rng_seed(struct rng_state *st, uint32_t seed)
{
	/*
	 * SplitMix32 to expand the seed into 4 state words.
	 * Ensures all-zero state is avoided.
	 */
	for (int i = 0; i < 4; i++) {
		seed += 0x9e3779b9;
		uint32_t z = seed;
		z = (z ^ (z >> 16)) * 0x45d9f3b;
		z = (z ^ (z >> 16)) * 0x45d9f3b;
		z = z ^ (z >> 16);
		st->s[i] = z;
	}
}

void wt_rng_fill(uint32_t seed, void *buf, size_t len)
{
	struct rng_state st;
	uint8_t *p = buf;

	rng_seed(&st, seed);

	/* Fill in 4-byte chunks */
	while (len >= 4) {
		uint32_t val = rng_next(&st);
		memcpy(p, &val, 4);
		p += 4;
		len -= 4;
	}

	/* Remaining bytes */
	if (len > 0) {
		uint32_t val = rng_next(&st);
		memcpy(p, &val, len);
	}
}
