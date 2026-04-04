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
#include <getopt.h>
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

int main(int argc, char **argv)
{
	struct wt_config cfg;

	if (parse_args(argc, argv, &cfg) < 0)
		return 1;

	/* Signal handling */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	if (wt_stop_init() < 0) {
		fprintf(stderr, "Failed to init stop mechanism\n");
		return 1;
	}

	uint64_t mid = wt_machine_id();

	printf("wardtest starting\n");
	printf("  machine_id: 0x%016lx\n", (unsigned long)mid);
	printf("  data:       %s\n", cfg.cfg_data_dir);
	printf("  meta:       %s\n", cfg.cfg_meta_dir);
	printf("  history:    %s\n", cfg.cfg_hist_dir);
	printf("  codec:      %s  k=%d m=%d  shard=%u bytes\n",
	       cfg.cfg_codec == WT_CODEC_XOR ? "xor" : "rs",
	       cfg.cfg_k, cfg.cfg_m, cfg.cfg_shard_size);
	printf("  iterations: %lu  clients: %d  verify-only: %s\n",
	       (unsigned long)cfg.cfg_iterations, cfg.cfg_clients,
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

	/*
	 * TODO: implement action loop and thread pool.
	 * For now, just demonstrate the infrastructure works.
	 */
	printf("wardtest: infrastructure initialized successfully\n");
	printf("wardtest: action loop not yet implemented\n");

	wt_stop_fini();
	return 0;
}
