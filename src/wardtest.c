/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * wardtest — filesystem stress test with EC-based corruption detection.
 *
 * Named for the "ward" — the part of a lock mechanism that prevents
 * the wrong key from turning.  wardtest verifies that data written
 * through NFS comes back unchanged.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wardtest.h"

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Required:\n"
		"  --data PATH        Directory for shard files\n"
		"  --meta PATH        Directory for stripe metadata\n"
		"  --history PATH     Directory for action history\n"
		"\n"
		"Optional:\n"
		"  --iterations N     Number of operations (0 = infinite)\n"
		"  --clients N        Writer threads (default: 1)\n"
		"  --shard-size N     Shard size in bytes (default: 4096)\n"
		"  --k N              Data shards (default: 4)\n"
		"  --m N              Parity shards (default: 1)\n"
		"  --codec TYPE       xor (default) or rs\n"
		"  --verify-only      Read and verify only, no writes\n"
		"  --seed N           Base RNG seed (default: time-based)\n"
		"  --report N         Stats interval in seconds (default: 10)\n"
		"  --help             Show this help\n",
		prog);
}

static struct option long_opts[] = {
	{ "data",       required_argument, 0, 'd' },
	{ "meta",       required_argument, 0, 'm' },
	{ "history",    required_argument, 0, 'H' },
	{ "iterations", required_argument, 0, 'n' },
	{ "clients",    required_argument, 0, 'c' },
	{ "shard-size", required_argument, 0, 's' },
	{ "k",          required_argument, 0, 'k' },
	{ "m",          required_argument, 0, 'M' },
	{ "codec",      required_argument, 0, 'C' },
	{ "verify-only",no_argument,       0, 'V' },
	{ "seed",       required_argument, 0, 'S' },
	{ "report",     required_argument, 0, 'r' },
	{ "help",       no_argument,       0, 'h' },
	{ 0, 0, 0, 0 },
};

static void signal_handler(int sig __attribute__((unused)))
{
	g_stop = 1;
}

static int parse_args(int argc, char **argv, struct wt_config *cfg)
{
	int c;

	/* Defaults */
	memset(cfg, 0, sizeof(*cfg));
	cfg->cfg_iterations = 0;
	cfg->cfg_clients = 1;
	cfg->cfg_shard_size = WT_DEFAULT_SHARD_SIZE;
	cfg->cfg_k = WT_DEFAULT_K;
	cfg->cfg_m = WT_DEFAULT_M;
	cfg->cfg_codec = WT_CODEC_XOR;
	cfg->cfg_report_interval = 10;

	while ((c = getopt_long(argc, argv, "d:m:H:n:c:s:k:M:C:VS:r:h",
				long_opts, NULL)) != -1) {
		switch (c) {
		case 'd':
			strncpy(cfg->cfg_data_dir, optarg,
				sizeof(cfg->cfg_data_dir) - 1);
			break;
		case 'm':
			strncpy(cfg->cfg_meta_dir, optarg,
				sizeof(cfg->cfg_meta_dir) - 1);
			break;
		case 'H':
			strncpy(cfg->cfg_hist_dir, optarg,
				sizeof(cfg->cfg_hist_dir) - 1);
			break;
		case 'n':
			cfg->cfg_iterations = (uint64_t)strtoull(optarg,
								 NULL, 0);
			break;
		case 'c':
			cfg->cfg_clients = atoi(optarg);
			if (cfg->cfg_clients < 1)
				cfg->cfg_clients = 1;
			break;
		case 's':
			cfg->cfg_shard_size = (uint32_t)strtoul(optarg,
								NULL, 0);
			break;
		case 'k':
			cfg->cfg_k = atoi(optarg);
			break;
		case 'M':
			cfg->cfg_m = atoi(optarg);
			break;
		case 'C':
			if (strcmp(optarg, "rs") == 0)
				cfg->cfg_codec = WT_CODEC_RS;
			else
				cfg->cfg_codec = WT_CODEC_XOR;
			break;
		case 'V':
			cfg->cfg_verify_only = true;
			break;
		case 'S':
			cfg->cfg_seed = (uint32_t)strtoul(optarg, NULL, 0);
			break;
		case 'r':
			cfg->cfg_report_interval = atoi(optarg);
			break;
		case 'h':
		default:
			usage(argv[0]);
			return -1;
		}
	}

	if (!cfg->cfg_data_dir[0] || !cfg->cfg_meta_dir[0] ||
	    !cfg->cfg_hist_dir[0]) {
		fprintf(stderr,
			"Error: --data, --meta, and --history are required\n");
		usage(argv[0]);
		return -1;
	}

	if (cfg->cfg_seed == 0)
		cfg->cfg_seed = (uint32_t)time(NULL);

	/* XOR codec only supports m=1 */
	if (cfg->cfg_codec == WT_CODEC_XOR && cfg->cfg_m != 1) {
		fprintf(stderr,
			"Warning: XOR codec forces m=1 (use --codec rs for m>1)\n");
		cfg->cfg_m = 1;
	}

	return 0;
}

/*
 * Register or log completion in the clients file.
 * Best-effort — failures logged to stderr but don't stop the test.
 */
static void clients_log(const char *meta_dir, uint64_t mid,
			const char *status, uint64_t iter)
{
	char clients_path[WT_PATH_BUF];
	char host[256];
	char line[256];
	struct timespec ts;

	if (gethostname(host, sizeof(host)) < 0) {
		fprintf(stderr, "wardtest: gethostname: %s\n",
			strerror(errno));
		strncpy(host, "unknown", sizeof(host) - 1);
	}
	host[sizeof(host) - 1] = '\0';

	if (snprintf(clients_path, sizeof(clients_path), "%s/%s",
		     meta_dir, WT_CLIENTS_FILE) >= (int)sizeof(clients_path)) {
		fprintf(stderr, "wardtest: clients path truncated\n");
		return;
	}

	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		fprintf(stderr, "wardtest: clock_gettime: %s\n",
			strerror(errno));
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
	}

	int n;
	if (iter > 0)
		n = snprintf(line, sizeof(line),
			     "%-8s 0x%016lx %s %d %ld.%09ld "
			     "iterations=%lu\n",
			     status, (unsigned long)mid, host,
			     (int)getpid(), (long)ts.tv_sec,
			     (long)ts.tv_nsec, (unsigned long)iter);
	else
		n = snprintf(line, sizeof(line),
			     "%-8s 0x%016lx %s %d %ld.%09ld\n",
			     status, (unsigned long)mid, host,
			     (int)getpid(), (long)ts.tv_sec,
			     (long)ts.tv_nsec);

	if (n < 0 || (size_t)n >= sizeof(line)) {
		fprintf(stderr, "wardtest: clients line truncated\n");
		return;
	}

	int cfd = open(clients_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (cfd < 0) {
		fprintf(stderr, "wardtest: open(%s): %s\n",
			clients_path, strerror(errno));
		return;
	}

	ssize_t wr = write(cfd, line, (size_t)n);
	if (wr != (ssize_t)n)
		fprintf(stderr, "wardtest: write clients: %s\n",
			wr < 0 ? strerror(errno) : "short write");

	if (close(cfd) < 0)
		fprintf(stderr, "wardtest: close clients: %s\n",
			strerror(errno));
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                       */
/* ------------------------------------------------------------------ */

static void *worker_thread(void *arg)
{
	struct wt_worker *w = arg;
	const struct wt_config *cfg = w->ww_cfg;
	uint32_t op_seed = w->ww_seed;

	while (!wt_should_stop(cfg->cfg_meta_dir)) {
		if (cfg->cfg_iterations > 0 &&
		    w->ww_iterations >= cfg->cfg_iterations)
			break;

		/* Advance seed deterministically */
		op_seed = op_seed * 1103515245 + 12345;

		enum wt_fs_state state = wt_state_check(cfg->cfg_data_dir);
		enum wt_action action = wt_state_pick_action(
			state, op_seed, cfg->cfg_verify_only);

		int ret = 0;

		switch (action) {
		case WT_ACTION_CREATE:
			ret = wt_action_create(cfg, w->ww_machine_id,
					       &w->ww_stripe_counter,
					       op_seed);
			break;
		case WT_ACTION_READ:
		case WT_ACTION_VERIFY:
			ret = wt_action_verify(cfg, w->ww_machine_id,
					       op_seed);
			break;
		case WT_ACTION_WRITE:
			/* Modify = delete + create with new seed */
			wt_action_delete(cfg, w->ww_machine_id, op_seed);
			op_seed = op_seed * 1103515245 + 12345;
			ret = wt_action_create(cfg, w->ww_machine_id,
					       &w->ww_stripe_counter,
					       op_seed);
			break;
		case WT_ACTION_DELETE:
			ret = wt_action_delete(cfg, w->ww_machine_id,
					       op_seed);
			break;
		}

		if (ret == -EILSEQ) {
			w->ww_ret = 2;
			break;
		}

		w->ww_stats[action]++;
		w->ww_iterations++;
	}

	return NULL;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
	struct wt_config cfg;

	if (parse_args(argc, argv, &cfg) < 0)
		return 1;

	/* Signal handling */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		fprintf(stderr, "wardtest: sigaction(SIGINT): %s\n",
			strerror(errno));
		return 1;
	}
	if (sigaction(SIGTERM, &sa, NULL) < 0) {
		fprintf(stderr, "wardtest: sigaction(SIGTERM): %s\n",
			strerror(errno));
		return 1;
	}
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		fprintf(stderr, "wardtest: signal(SIGPIPE): %s\n",
			strerror(errno));
		return 1;
	}

	if (wt_stop_init() < 0) {
		fprintf(stderr, "Failed to init stop mechanism\n");
		return 1;
	}

	uint64_t mid = wt_machine_id();
	int nclients = cfg.cfg_clients;

	printf("wardtest starting\n");
	printf("  machine_id: 0x%016lx\n", (unsigned long)mid);
	printf("  data:       %s\n", cfg.cfg_data_dir);
	printf("  meta:       %s\n", cfg.cfg_meta_dir);
	printf("  history:    %s\n", cfg.cfg_hist_dir);
	printf("  codec:      %s  k=%d m=%d  shard=%u bytes\n",
	       cfg.cfg_codec == WT_CODEC_XOR ? "xor" : "rs",
	       cfg.cfg_k, cfg.cfg_m, cfg.cfg_shard_size);
	printf("  iterations: %lu  clients: %d  verify-only: %s\n",
	       (unsigned long)cfg.cfg_iterations, nclients,
	       cfg.cfg_verify_only ? "yes" : "no");
	printf("  seed:       0x%08x\n", cfg.cfg_seed);

	/* Check for previous corruption stop */
	if (wt_should_stop(cfg.cfg_meta_dir)) {
		fprintf(stderr,
			"Error: %s/%s exists — previous corruption detected.\n"
			"Investigate and remove the file to restart.\n",
			cfg.cfg_meta_dir, WT_STOP_FILE);
		wt_stop_fini();
		return 2;
	}

	/* Sync encoding parameters with control file */
	if (wt_control_sync(cfg.cfg_meta_dir, &cfg) < 0) {
		wt_stop_fini();
		return 1;
	}

	/* Register with clients file */
	clients_log(cfg.cfg_meta_dir, mid, "RUNNING", 0);

	/*
	 * Per-iteration limit: when cfg_iterations > 0, divide evenly
	 * across workers so the total doesn't exceed the requested count.
	 */
	uint64_t per_worker_iter = 0;
	if (cfg.cfg_iterations > 0)
		per_worker_iter = cfg.cfg_iterations / (uint64_t)nclients;

	/* Allocate worker contexts */
	struct wt_worker *workers = calloc((size_t)nclients,
					   sizeof(struct wt_worker));
	if (!workers) {
		fprintf(stderr, "wardtest: out of memory for workers\n");
		wt_stop_fini();
		return 1;
	}

	/* Initialize workers with deterministic per-thread seeds.
	 * Each worker gets a unique seed derived from the base seed
	 * and its index, so their RNG streams don't overlap. */
	for (int i = 0; i < nclients; i++) {
		workers[i].ww_cfg = &cfg;
		workers[i].ww_machine_id = mid;
		workers[i].ww_id = i;
		workers[i].ww_seed = cfg.cfg_seed ^ (uint32_t)(i * 2654435761U);
		workers[i].ww_stripe_counter = (uint32_t)i * 100000;
		workers[i].ww_iterations = 0;
		workers[i].ww_ret = 0;

		/* Override per-worker iteration limit */
		struct wt_config *wcfg = (struct wt_config *)workers[i].ww_cfg;
		(void)wcfg; /* iterations handled via per_worker_iter */
	}

	/* If using per-worker iteration limits, set them on a copy */
	struct wt_config worker_cfg = cfg;
	if (per_worker_iter > 0)
		worker_cfg.cfg_iterations = per_worker_iter;

	for (int i = 0; i < nclients; i++)
		workers[i].ww_cfg = &worker_cfg;

	printf("wardtest: running with %d worker%s...\n",
	       nclients, nclients > 1 ? "s" : "");

	/* Launch worker threads */
	for (int i = 0; i < nclients; i++) {
		int err = pthread_create(&workers[i].ww_thread, NULL,
					 worker_thread, &workers[i]);
		if (err) {
			fprintf(stderr,
				"wardtest: pthread_create[%d]: %s\n",
				i, strerror(err));
			/* Signal existing workers to stop */
			g_stop = 1;
			nclients = i;
			break;
		}
	}

	/* Main thread: periodic stats reporting */
	time_t last_report = time(NULL);

	while (!g_stop) {
		struct timespec sleep_ts = { .tv_sec = 1, .tv_nsec = 0 };
		nanosleep(&sleep_ts, NULL);

		time_t now = time(NULL);
		if (now - last_report >= cfg.cfg_report_interval) {
			uint64_t total_iter = 0;
			uint64_t total_stats[5] = { 0 };

			for (int i = 0; i < nclients; i++) {
				total_iter += workers[i].ww_iterations;
				for (int j = 0; j < 5; j++)
					total_stats[j] += workers[i].ww_stats[j];
			}

			enum wt_fs_state state =
				wt_state_check(cfg.cfg_data_dir);

			printf("[%ld] iter=%lu create=%lu read=%lu "
			       "write=%lu delete=%lu verify=%lu "
			       "state=%s clients=%d\n",
			       (long)now, (unsigned long)total_iter,
			       (unsigned long)total_stats[WT_ACTION_CREATE],
			       (unsigned long)(total_stats[WT_ACTION_READ] +
					       total_stats[WT_ACTION_VERIFY]),
			       (unsigned long)total_stats[WT_ACTION_WRITE],
			       (unsigned long)total_stats[WT_ACTION_DELETE],
			       (unsigned long)total_stats[WT_ACTION_VERIFY],
			       state == WT_STATE_EMPTY  ? "EMPTY" :
			       state == WT_STATE_FULL   ? "FULL" :
							  "NORMAL",
			       nclients);
			last_report = now;
		}

		/* Check if all workers have finished (iteration limit) */
		bool all_done = true;
		for (int i = 0; i < nclients; i++) {
			if (workers[i].ww_iterations <
			    worker_cfg.cfg_iterations ||
			    worker_cfg.cfg_iterations == 0) {
				all_done = false;
				break;
			}
		}
		if (all_done && worker_cfg.cfg_iterations > 0)
			break;
	}

	/* Join all workers */
	for (int i = 0; i < nclients; i++) {
		int err = pthread_join(workers[i].ww_thread, NULL);
		if (err)
			fprintf(stderr,
				"wardtest: pthread_join[%d]: %s\n",
				i, strerror(err));
	}

	/* Aggregate final stats */
	uint64_t total_iter = 0;
	uint64_t total_stats[5] = { 0 };
	int exit_code = 0;

	for (int i = 0; i < nclients; i++) {
		total_iter += workers[i].ww_iterations;
		for (int j = 0; j < 5; j++)
			total_stats[j] += workers[i].ww_stats[j];
		if (workers[i].ww_ret != 0)
			exit_code = workers[i].ww_ret;
	}

	/* Log completion */
	clients_log(cfg.cfg_meta_dir, mid, "DONE", total_iter);

	printf("\nwardtest: finished (%lu iterations, %d workers)\n",
	       (unsigned long)total_iter, nclients);
	printf("  create=%lu read=%lu write=%lu delete=%lu verify=%lu\n",
	       (unsigned long)total_stats[WT_ACTION_CREATE],
	       (unsigned long)(total_stats[WT_ACTION_READ] +
			       total_stats[WT_ACTION_VERIFY]),
	       (unsigned long)total_stats[WT_ACTION_WRITE],
	       (unsigned long)total_stats[WT_ACTION_DELETE],
	       (unsigned long)total_stats[WT_ACTION_VERIFY]);

	if (g_stop_reason)
		printf("  STOPPED: corruption detected\n");

	free(workers);
	wt_stop_fini();
	return exit_code ? exit_code : (g_stop_reason ? 2 : 0);
}
