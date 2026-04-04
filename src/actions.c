/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Actions — create, verify, write (modify), delete stripes.
 *
 * Each action is a complete pipeline: generate source -> EC encode ->
 * write shards with CRC headers -> write metadata -> log history.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wardtest.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		fprintf(stderr, "wardtest: clock_gettime: %s\n",
			strerror(errno));
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t next_stripe_id(uint64_t machine_id, uint32_t *counter)
{
	/*
	 * Stripe ID: high 32 bits from machine_id, low 32 bits from
	 * a per-thread counter.  Unique per process.
	 */
	uint32_t c = (*counter)++;
	return (machine_id & 0xFFFFFFFF00000000ULL) | (uint64_t)c;
}

/* ------------------------------------------------------------------ */
/* Create                                                              */
/* ------------------------------------------------------------------ */

int wt_action_create(const struct wt_config *cfg, uint64_t machine_id,
		     uint32_t *stripe_counter, uint32_t op_seed)
{
	int k = cfg->cfg_k;
	int m = cfg->cfg_m;
	int n = k + m;
	size_t shard_size = cfg->cfg_shard_size;
	size_t source_size = shard_size * (size_t)k;
	int ret = 0;

	uint64_t stripe_id = next_stripe_id(machine_id, stripe_counter);

	/* Generate deterministic source data from seed */
	uint8_t *source = malloc(source_size);
	if (!source)
		return -ENOMEM;
	wt_rng_fill(op_seed, source, source_size);

	/* Split source into k data shards + allocate m parity shards */
	uint8_t **shards = calloc((size_t)n, sizeof(uint8_t *));
	if (!shards) {
		free(source);
		return -ENOMEM;
	}

	for (int i = 0; i < k; i++)
		shards[i] = source + (size_t)i * shard_size;

	for (int i = k; i < n; i++) {
		shards[i] = calloc(1, shard_size);
		if (!shards[i]) {
			ret = -ENOMEM;
			goto out;
		}
	}

	/* EC encode */
	wt_codec_encode(cfg->cfg_codec, shards, k, m, shard_size);

	/* Write each shard atomically */
	for (int i = 0; i < n; i++) {
		struct wt_chunk_header hdr = {
			.wch_magic = WT_CHUNK_MAGIC,
			.wch_version = WT_CHUNK_VERSION,
			.wch_crc32 = wt_crc32(shards[i], shard_size),
			.wch_shard_size = (uint32_t)shard_size,
			.wch_stripe_id = stripe_id,
			.wch_shard_index = (uint32_t)i,
			.wch_k = (uint32_t)k,
			.wch_m = (uint32_t)m,
			.wch_seed = op_seed,
			.wch_machine_id = machine_id,
			.wch_timestamp = now_ns(),
		};

		ret = wt_chunk_write(cfg->cfg_data_dir, stripe_id, i,
				     &hdr, shards[i], shard_size);
		if (ret)
			goto out;
	}

	/* Write metadata (after all shards committed) */
	struct wt_stripe_meta meta = {
		.wsm_magic = WT_META_MAGIC,
		.wsm_version = WT_META_VERSION,
		.wsm_stripe_id = stripe_id,
		.wsm_seed = op_seed,
		.wsm_k = (uint32_t)k,
		.wsm_m = (uint32_t)m,
		.wsm_shard_size = (uint32_t)shard_size,
		.wsm_source_size = (uint32_t)source_size,
		.wsm_machine_id = machine_id,
		.wsm_created_ns = now_ns(),
		.wsm_state = WT_STRIPE_ACTIVE,
		.wsm_verify_count = 0,
		.wsm_codec = (uint32_t)cfg->cfg_codec,
	};

	ret = wt_meta_write(cfg->cfg_meta_dir, &meta);

out:
	for (int i = k; i < n; i++)
		free(shards[i]);
	free(shards);
	free(source);

	if (ret == 0)
		wt_history_append(cfg->cfg_hist_dir, machine_id,
				  WT_ACTION_CREATE, stripe_id, op_seed, true);

	return ret;
}

/* ------------------------------------------------------------------ */
/* Verify (read + check)                                               */
/* ------------------------------------------------------------------ */

int wt_action_verify(const struct wt_config *cfg, uint64_t machine_id,
		     uint32_t op_seed)
{
	uint64_t stripe_id = wt_meta_pick_random(cfg->cfg_meta_dir, op_seed);
	if (stripe_id == 0)
		return 0; /* nothing to verify */

	struct wt_stripe_meta meta;
	int ret = wt_meta_read(cfg->cfg_meta_dir, stripe_id, &meta);
	if (ret)
		return ret;

	int k = (int)meta.wsm_k;
	int m = (int)meta.wsm_m;
	int n = k + m;
	size_t shard_size = meta.wsm_shard_size;
	enum wt_codec codec = (enum wt_codec)meta.wsm_codec;

	/* Read all shards */
	uint8_t **shards = calloc((size_t)n, sizeof(uint8_t *));
	if (!shards)
		return -ENOMEM;

	for (int i = 0; i < n; i++) {
		struct wt_chunk_header hdr;
		void *data = NULL;
		size_t data_len = 0;

		ret = wt_chunk_read(cfg->cfg_data_dir, stripe_id, i,
				    &hdr, &data, &data_len);
		if (ret == -EILSEQ) {
			/* CRC mismatch! */
			uint32_t actual = wt_crc32(data, data_len);
			wt_stop_corruption(cfg->cfg_meta_dir, stripe_id, i,
					   hdr.wch_crc32, actual, machine_id);
			free(data);
			ret = -EILSEQ;
			goto out;
		}
		if (ret) {
			goto out;
		}

		shards[i] = data;
	}

	/* EC verify using the codec that created this stripe */
	bool ec_ok = wt_codec_verify(codec, shards, k, m, shard_size);
	if (!ec_ok) {
		wt_stop_corruption(cfg->cfg_meta_dir, stripe_id, -1,
				   0, 0, machine_id);
		ret = -EILSEQ;
		goto out;
	}

	/* Regenerate source from seed and compare against data shards */
	size_t source_size = shard_size * (size_t)k;
	uint8_t *expected = malloc(source_size);
	if (!expected) {
		ret = -ENOMEM;
		goto out;
	}
	wt_rng_fill(meta.wsm_seed, expected, source_size);

	for (int i = 0; i < k; i++) {
		if (memcmp(shards[i], expected + (size_t)i * shard_size,
			   shard_size) != 0) {
			uint32_t expected_crc =
				wt_crc32(expected + (size_t)i * shard_size,
					 shard_size);
			uint32_t actual_crc = wt_crc32(shards[i], shard_size);
			wt_stop_corruption(cfg->cfg_meta_dir, stripe_id, i,
					   expected_crc, actual_crc,
					   machine_id);
			ret = -EILSEQ;
			free(expected);
			goto out;
		}
	}
	free(expected);

	wt_history_append(cfg->cfg_hist_dir, machine_id,
			  WT_ACTION_VERIFY, stripe_id, meta.wsm_seed, true);

out:
	for (int i = 0; i < n; i++)
		free(shards[i]);
	free(shards);
	return ret;
}

/* ------------------------------------------------------------------ */
/* Delete                                                              */
/* ------------------------------------------------------------------ */

int wt_action_delete(const struct wt_config *cfg, uint64_t machine_id,
		     uint32_t op_seed)
{
	uint64_t stripe_id = wt_meta_pick_random(cfg->cfg_meta_dir, op_seed);
	if (stripe_id == 0)
		return 0;

	struct wt_stripe_meta meta;
	int ret = wt_meta_read(cfg->cfg_meta_dir, stripe_id, &meta);
	if (ret)
		return ret;

	int n = (int)meta.wsm_k + (int)meta.wsm_m;

	/* Unlink all shard files — best effort, log failures */
	for (int i = 0; i < n; i++) {
		char path[WT_PATH_BUF];
		wt_chunk_path(path, sizeof(path), cfg->cfg_data_dir,
			      stripe_id, i);
		if (unlink(path) < 0 && errno != ENOENT)
			fprintf(stderr, "wardtest: unlink(%s): %s\n",
				path, strerror(errno));
	}

	/* Unlink metadata */
	ret = wt_meta_delete(cfg->cfg_meta_dir, stripe_id);
	if (ret && ret != -ENOENT)
		fprintf(stderr, "wardtest: meta_delete stripe=%016lx: %s\n",
			(unsigned long)stripe_id, strerror(-ret));

	wt_history_append(cfg->cfg_hist_dir, machine_id,
			  WT_ACTION_DELETE, stripe_id, meta.wsm_seed, true);

	return 0;
}
