/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * wardtest.h — all data structures, constants, and function declarations.
 */

#ifndef _WARDTEST_H
#define _WARDTEST_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <signal.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define WT_CHUNK_MAGIC   0x57415244 /* "WARD" */
#define WT_CHUNK_VERSION 1
#define WT_MAX_PATH      4096
#define WT_PATH_BUF      (WT_MAX_PATH + 256)
#define WT_MAX_SHARDS    32
#define WT_DEFAULT_K     4
#define WT_DEFAULT_M     1
#define WT_DEFAULT_SHARD_SIZE 4096
#define WT_STOP_FILE     ".wardtest_stop"
#define WT_CLIENTS_FILE  ".wardtest_clients"

/* ------------------------------------------------------------------ */
/* Codec types                                                         */
/* ------------------------------------------------------------------ */

enum wt_codec {
	WT_CODEC_XOR = 0,
	WT_CODEC_RS  = 1,
};

/* ------------------------------------------------------------------ */
/* Chunk header — written at the start of each shard file              */
/* ------------------------------------------------------------------ */

struct wt_chunk_header {
	uint32_t wch_magic;
	uint32_t wch_version;
	uint32_t wch_crc32;        /* CRC of shard data (not header) */
	uint32_t wch_shard_size;
	uint64_t wch_stripe_id;    /* unique ID for this stripe */
	uint32_t wch_shard_index;  /* 0..k-1 = data, k..k+m-1 = parity */
	uint32_t wch_k;
	uint32_t wch_m;
	uint32_t wch_seed;         /* RNG seed for source regeneration */
	uint64_t wch_machine_id;
	uint64_t wch_timestamp;    /* CLOCK_REALTIME nanoseconds */
};

/* ------------------------------------------------------------------ */
/* Stripe metadata — one file per stripe in the meta directory         */
/* ------------------------------------------------------------------ */

#define WT_META_MAGIC   0x57544D44 /* "WTMD" */
#define WT_META_VERSION 1

enum wt_stripe_state {
	WT_STRIPE_ACTIVE   = 0,
	WT_STRIPE_VERIFIED = 1,
	WT_STRIPE_CORRUPT  = 2,
};

struct wt_stripe_meta {
	uint32_t wsm_magic;
	uint32_t wsm_version;
	uint64_t wsm_stripe_id;
	uint32_t wsm_seed;
	uint32_t wsm_k;
	uint32_t wsm_m;
	uint32_t wsm_shard_size;
	uint32_t wsm_source_size;
	uint64_t wsm_machine_id;
	uint64_t wsm_created_ns;
	uint32_t wsm_state;
	uint32_t wsm_verify_count;
};

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

struct wt_config {
	char     cfg_data_dir[WT_MAX_PATH];
	char     cfg_meta_dir[WT_MAX_PATH];
	char     cfg_hist_dir[WT_MAX_PATH];
	uint64_t cfg_iterations;   /* 0 = infinite */
	int      cfg_clients;      /* writer threads */
	uint32_t cfg_shard_size;
	int      cfg_k;
	int      cfg_m;
	enum wt_codec cfg_codec;
	bool     cfg_verify_only;
	uint32_t cfg_seed;
	int      cfg_report_interval; /* seconds */
};

/* ------------------------------------------------------------------ */
/* Per-thread worker context                                           */
/* ------------------------------------------------------------------ */

struct wt_worker {
	pthread_t        ww_thread;
	const struct wt_config *ww_cfg;
	uint64_t         ww_machine_id;
	int              ww_id;           /* worker index (0-based) */
	uint32_t         ww_seed;         /* per-thread RNG seed */
	uint32_t         ww_stripe_counter;
	uint64_t         ww_iterations;
	uint64_t         ww_stats[5];     /* per-action counts */
	int              ww_ret;          /* exit status */
};

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */

/* Stop flag — set on corruption or SIGTERM.  Checked every operation. */
extern volatile sig_atomic_t g_stop;

/* Reason for stop: 0 = signal, 1 = corruption */
extern volatile sig_atomic_t g_stop_reason;

/* ------------------------------------------------------------------ */
/* Machine ID (machine.c)                                              */
/* ------------------------------------------------------------------ */

uint64_t wt_machine_id(void);

/* ------------------------------------------------------------------ */
/* RNG (rng.c) — deterministic, seed-based                             */
/* ------------------------------------------------------------------ */

/*
 * Fill buf with deterministic pseudo-random data from seed.
 * Same seed always produces the same output.
 */
void wt_rng_fill(uint32_t seed, void *buf, size_t len);

/* ------------------------------------------------------------------ */
/* CRC32 (crc32.c)                                                     */
/* ------------------------------------------------------------------ */

uint32_t wt_crc32(const void *data, size_t len);

/* ------------------------------------------------------------------ */
/* XOR codec (xor.c)                                                   */
/* ------------------------------------------------------------------ */

/*
 * XOR-encode: compute parity shard from k data shards.
 * shards[0..k-1] = data (input), shards[k] = parity (output).
 * All shards must be shard_size bytes.
 */
void wt_xor_encode(uint8_t **shards, int k, size_t shard_size);

/*
 * XOR-verify: XOR all k+1 shards — result should be all zeros.
 * Returns true if data is consistent, false if corrupted.
 */
bool wt_xor_verify(const uint8_t **shards, int n, size_t shard_size);

/* ------------------------------------------------------------------ */
/* Chunk I/O (chunk.c)                                                 */
/* ------------------------------------------------------------------ */

/*
 * Write a shard file atomically (write-temp/fsync/rename).
 * dir is the data directory, stripe_id and shard_idx form the name.
 */
int wt_chunk_write(const char *dir, uint64_t stripe_id, int shard_idx,
		   const struct wt_chunk_header *hdr,
		   const void *data, size_t data_len);

/*
 * Read a shard file.  Allocates *data_out (caller frees).
 * Returns 0 on success, -errno on failure.
 */
int wt_chunk_read(const char *dir, uint64_t stripe_id, int shard_idx,
		  struct wt_chunk_header *hdr_out,
		  void **data_out, size_t *data_len_out);

/* Build the filename for a shard. */
void wt_chunk_path(char *buf, size_t bufsz, const char *dir,
		   uint64_t stripe_id, int shard_idx);

/* ------------------------------------------------------------------ */
/* Stripe metadata I/O (meta.c)                                        */
/* ------------------------------------------------------------------ */

int wt_meta_write(const char *dir, const struct wt_stripe_meta *meta);
int wt_meta_read(const char *dir, uint64_t stripe_id,
		 struct wt_stripe_meta *meta_out);
int wt_meta_delete(const char *dir, uint64_t stripe_id);

/* Pick a random existing stripe from the meta directory.
 * Returns stripe_id or 0 if none found. */
uint64_t wt_meta_pick_random(const char *dir, uint32_t seed);

/* ------------------------------------------------------------------ */
/* History (history.c)                                                 */
/* ------------------------------------------------------------------ */

enum wt_action {
	WT_ACTION_CREATE  = 0,
	WT_ACTION_READ    = 1,
	WT_ACTION_WRITE   = 2,
	WT_ACTION_DELETE  = 3,
	WT_ACTION_VERIFY  = 4,
};

void wt_history_append(const char *dir, uint64_t machine_id,
		       enum wt_action action, uint64_t stripe_id,
		       uint32_t seed, bool success);

/* ------------------------------------------------------------------ */
/* Stop mechanism (stop.c)                                             */
/* ------------------------------------------------------------------ */

int wt_stop_init(void);
void wt_stop_fini(void);

/* Signal all threads + create sentinel file. */
void wt_stop_corruption(const char *meta_dir, uint64_t stripe_id,
			int shard_idx, uint32_t expected_crc,
			uint32_t actual_crc, uint64_t machine_id);

/* Check if stop was requested (local flag + sentinel file). */
bool wt_should_stop(const char *meta_dir);

/* ------------------------------------------------------------------ */
/* State machine (state.c)                                             */
/* ------------------------------------------------------------------ */

enum wt_fs_state {
	WT_STATE_EMPTY  = 0,
	WT_STATE_NORMAL = 1,
	WT_STATE_FULL   = 2,
};

enum wt_fs_state wt_state_check(const char *path);

/* Returns the selected action based on state + random weight. */
enum wt_action wt_state_pick_action(enum wt_fs_state state,
				    uint32_t random_val,
				    bool verify_only);

/* ------------------------------------------------------------------ */
/* Actions (actions.c)                                                 */
/* ------------------------------------------------------------------ */

int wt_action_create(const struct wt_config *cfg, uint64_t machine_id,
		     uint32_t *stripe_counter, uint32_t op_seed);

int wt_action_verify(const struct wt_config *cfg, uint64_t machine_id,
		     uint32_t op_seed);

int wt_action_delete(const struct wt_config *cfg, uint64_t machine_id,
		     uint32_t op_seed);

#endif /* _WARDTEST_H */
