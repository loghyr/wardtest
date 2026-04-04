/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Reed-Solomon GF(2^8) codec -- k data + m parity shards.
 *
 * Clean-room implementation using only pre-2000 textbook sources:
 *   - Reed & Solomon 1960 (original paper)
 *   - Berlekamp 1968 "Algebraic Coding Theory" (GF arithmetic)
 *   - Peterson & Weldon 1972 "Error-Correcting Codes"
 *
 * Uses scalar log/antilog table GF(2^8) multiply with the standard
 * irreducible polynomial x^8 + x^4 + x^3 + x^2 + 1 (0x11d).
 * No SIMD.  No Plank/Jerasure/GF-Complete references.
 *
 * Vandermonde matrix construction for systematic encoding.
 * Encoding is O(k * m * shard_size) -- acceptable for a test tool.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "wardtest.h"

/* ------------------------------------------------------------------ */
/* GF(2^8) arithmetic                                                  */
/* ------------------------------------------------------------------ */

#define GF_SIZE 256
#define GF_POLY 0x11d  /* x^8 + x^4 + x^3 + x^2 + 1 */

static uint8_t gf_log[GF_SIZE];
static uint8_t gf_exp[GF_SIZE * 2]; /* doubled for mod-free lookup */
static int gf_init_done;

static void gf_init(void)
{
	if (gf_init_done)
		return;

	uint16_t x = 1;

	for (int i = 0; i < 255; i++) {
		gf_exp[i] = (uint8_t)x;
		gf_log[(uint8_t)x] = (uint8_t)i;
		x <<= 1;
		if (x & 0x100)
			x ^= GF_POLY;
	}

	/* gf_log[0] is undefined (log of 0 has no meaning).
	 * Set to 0 as a sentinel -- callers must check for zero. */
	gf_log[0] = 0;

	/* Extend exp table for easy modular lookup */
	for (int i = 255; i < 510; i++)
		gf_exp[i] = gf_exp[i - 255];

	gf_init_done = 1;
}

static inline uint8_t gf_mul(uint8_t a, uint8_t b)
{
	if (a == 0 || b == 0)
		return 0;
	return gf_exp[gf_log[a] + gf_log[b]];
}

/* ------------------------------------------------------------------ */
/* Vandermonde encoding matrix                                         */
/* ------------------------------------------------------------------ */

/*
 * Build the m x k encoding matrix for the parity rows.
 * Row i, column j = alpha^(i * j) where alpha = gf_exp[1] = 2.
 *
 * This is the lower portion of a Vandermonde matrix.  The upper k x k
 * identity matrix is implicit (systematic code -- data shards are
 * passed through unchanged).
 *
 * Matrix is stored row-major: matrix[i * k + j].
 */
static void build_encode_matrix(uint8_t *matrix, int k, int m)
{
	for (int i = 0; i < m; i++) {
		for (int j = 0; j < k; j++) {
			/* Vandermonde: element = (i+1)^j in GF(2^8) */
			uint8_t val = 1;
			uint8_t base = (uint8_t)(i + 1);
			for (int p = 0; p < j; p++)
				val = gf_mul(val, base);
			matrix[i * k + j] = val;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void wt_rs_encode(uint8_t **shards, int k, int m, size_t shard_size)
{
	gf_init();

	uint8_t matrix[WT_MAX_SHARDS * WT_MAX_SHARDS];
	build_encode_matrix(matrix, k, m);

	/*
	 * For each parity shard i (shards[k+i]):
	 *   parity[i] = sum over j of (matrix[i][j] * data[j])
	 * where sum is XOR and product is GF multiply.
	 */
	for (int i = 0; i < m; i++) {
		uint8_t *parity = shards[k + i];
		memset(parity, 0, shard_size);

		for (int j = 0; j < k; j++) {
			uint8_t coeff = matrix[i * k + j];
			if (coeff == 0)
				continue;

			const uint8_t *data = shards[j];
			for (size_t b = 0; b < shard_size; b++)
				parity[b] ^= gf_mul(coeff, data[b]);
		}
	}
}

bool wt_rs_verify(uint8_t **shards, int k, int m, size_t shard_size)
{
	gf_init();

	uint8_t matrix[WT_MAX_SHARDS * WT_MAX_SHARDS];
	build_encode_matrix(matrix, k, m);

	/*
	 * Recompute each parity shard from the data shards and compare.
	 * Any mismatch means corruption.
	 */
	for (int i = 0; i < m; i++) {
		const uint8_t *stored_parity = shards[k + i];

		for (size_t b = 0; b < shard_size; b++) {
			uint8_t expected = 0;

			for (int j = 0; j < k; j++) {
				uint8_t coeff = matrix[i * k + j];
				if (coeff != 0)
					expected ^= gf_mul(coeff, shards[j][b]);
			}

			if (expected != stored_parity[b])
				return false;
		}
	}

	return true;
}
