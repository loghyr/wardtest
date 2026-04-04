/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Control file -- shared encoding parameters across all clients.
 *
 * The first writer creates .wardtest_control with the encoding
 * configuration.  Subsequent writers validate their parameters match.
 * Verify-only clients adopt whatever the control file says.
 *
 * Uses O_CREAT|O_EXCL for atomic create-or-read: if the create
 * fails with EEXIST, we read and validate instead.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wardtest.h"

int wt_control_sync(const char *meta_dir, struct wt_config *cfg)
{
	char path[WT_PATH_BUF];
	struct wt_control ctl;

	if (snprintf(path, sizeof(path), "%s/%s",
		     meta_dir, WT_CONTROL_FILE) >= (int)sizeof(path))
		return -ENAMETOOLONG;

	/* Try to create -- if we win the race, write our config */
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd >= 0) {
		memset(&ctl, 0, sizeof(ctl));
		ctl.wc_magic = WT_CONTROL_MAGIC;
		ctl.wc_version = WT_CONTROL_VERSION;
		ctl.wc_k = (uint32_t)cfg->cfg_k;
		ctl.wc_m = (uint32_t)cfg->cfg_m;
		ctl.wc_shard_size = cfg->cfg_shard_size;
		ctl.wc_codec = (uint32_t)cfg->cfg_codec;

		ssize_t wr = write(fd, &ctl, sizeof(ctl));
		if (wr != (ssize_t)sizeof(ctl)) {
			int saved = errno;

			fprintf(stderr, "wardtest: write control: %s\n",
				wr < 0 ? strerror(saved) : "short write");
			if (close(fd) < 0)
				fprintf(stderr, "wardtest: close control: %s\n",
					strerror(errno));
			if (unlink(path) < 0)
				fprintf(stderr, "wardtest: unlink control: %s\n",
					strerror(errno));
			return wr < 0 ? -saved : -EIO;
		}

		if (fsync(fd) < 0) {
			int saved = errno;

			fprintf(stderr, "wardtest: fsync control: %s\n",
				strerror(saved));
			if (close(fd) < 0)
				fprintf(stderr, "wardtest: close control: %s\n",
					strerror(errno));
			return -saved;
		}

		if (close(fd) < 0) {
			fprintf(stderr, "wardtest: close control: %s\n",
				strerror(errno));
			return -errno;
		}

		printf("  control:    created (%s k=%d m=%d shard=%u)\n",
		       cfg->cfg_codec == WT_CODEC_XOR ? "xor" : "rs",
		       cfg->cfg_k, cfg->cfg_m, cfg->cfg_shard_size);
		return 0;
	}

	if (errno != EEXIST)
		return -errno;

	/* File exists -- read and validate */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	ssize_t rd = read(fd, &ctl, sizeof(ctl));
	if (close(fd) < 0)
		fprintf(stderr, "wardtest: close control: %s\n",
			strerror(errno));

	if (rd != (ssize_t)sizeof(ctl)) {
		fprintf(stderr, "wardtest: control file corrupt "
			"(got %zd bytes, expected %zu)\n",
			rd, sizeof(ctl));
		return -EIO;
	}

	if (ctl.wc_magic != WT_CONTROL_MAGIC ||
	    ctl.wc_version != WT_CONTROL_VERSION) {
		fprintf(stderr, "wardtest: control file bad magic/version "
			"(0x%08x/%u)\n", ctl.wc_magic, ctl.wc_version);
		return -EINVAL;
	}

	/* Verify-only clients adopt the control file parameters */
	if (cfg->cfg_verify_only) {
		cfg->cfg_k = (int)ctl.wc_k;
		cfg->cfg_m = (int)ctl.wc_m;
		cfg->cfg_shard_size = ctl.wc_shard_size;
		cfg->cfg_codec = (enum wt_codec)ctl.wc_codec;
		printf("  control:    adopted (%s k=%d m=%d shard=%u)\n",
		       cfg->cfg_codec == WT_CODEC_XOR ? "xor" : "rs",
		       cfg->cfg_k, cfg->cfg_m, cfg->cfg_shard_size);
		return 0;
	}

	/* Writers must match exactly */
	if ((int)ctl.wc_k != cfg->cfg_k ||
	    (int)ctl.wc_m != cfg->cfg_m ||
	    ctl.wc_shard_size != cfg->cfg_shard_size ||
	    (enum wt_codec)ctl.wc_codec != cfg->cfg_codec) {
		fprintf(stderr,
			"wardtest: parameter mismatch with control file\n"
			"  control: codec=%s k=%d m=%d shard=%u\n"
			"  local:   codec=%s k=%d m=%d shard=%u\n"
			"All writers must use the same parameters.\n",
			ctl.wc_codec == WT_CODEC_XOR ? "xor" : "rs",
			(int)ctl.wc_k, (int)ctl.wc_m, ctl.wc_shard_size,
			cfg->cfg_codec == WT_CODEC_XOR ? "xor" : "rs",
			cfg->cfg_k, cfg->cfg_m, cfg->cfg_shard_size);
		return -EINVAL;
	}

	printf("  control:    verified (%s k=%d m=%d shard=%u)\n",
	       cfg->cfg_codec == WT_CODEC_XOR ? "xor" : "rs",
	       cfg->cfg_k, cfg->cfg_m, cfg->cfg_shard_size);
	return 0;
}
