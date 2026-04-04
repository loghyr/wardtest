/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Chunk I/O -- read and write shard files with CRC-protected headers.
 *
 * All writes are atomic: write to .tmp, fsync, rename to final name.
 * A crash mid-write leaves only a .tmp file which is cleaned up on
 * restart.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wardtest.h"

void wt_chunk_path(char *buf, size_t bufsz, const char *dir,
		   uint64_t stripe_id, int shard_idx)
{
	int n = snprintf(buf, bufsz, "%s/stripe_%016lx_shard_%02d",
			 dir, (unsigned long)stripe_id, shard_idx);
	if (n < 0 || (size_t)n >= bufsz)
		fprintf(stderr, "wardtest: chunk path truncated\n");
}

int wt_chunk_write(const char *dir, uint64_t stripe_id, int shard_idx,
		   const struct wt_chunk_header *hdr,
		   const void *data, size_t data_len)
{
	char path[WT_PATH_BUF];
	char tmp[WT_PATH_BUF];
	int fd, ret = 0;
	ssize_t n;

	wt_chunk_path(path, sizeof(path), dir, stripe_id, shard_idx);

	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -ENAMETOOLONG;

	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -errno;

	/* Write header */
	n = write(fd, hdr, sizeof(*hdr));
	if (n != (ssize_t)sizeof(*hdr)) {
		ret = (n < 0) ? -errno : -EIO;
		goto err;
	}

	/* Write shard data */
	n = write(fd, data, data_len);
	if (n != (ssize_t)data_len) {
		ret = (n < 0) ? -errno : -EIO;
		goto err;
	}

	if (fsync(fd) < 0) {
		ret = -errno;
		goto err;
	}

	if (close(fd) < 0) {
		ret = -errno;
		/* fd is closed even on error; just clean up tmp */
		if (unlink(tmp) < 0)
			fprintf(stderr, "wardtest: unlink(%s): %s\n",
				tmp, strerror(errno));
		return ret;
	}

	/* Atomic rename */
	if (rename(tmp, path) < 0) {
		ret = -errno;
		if (unlink(tmp) < 0)
			fprintf(stderr, "wardtest: unlink(%s): %s\n",
				tmp, strerror(errno));
		return ret;
	}

	return 0;

err:
	{
		int saved = errno;

		if (close(fd) < 0)
			fprintf(stderr, "wardtest: close: %s\n",
				strerror(errno));
		if (unlink(tmp) < 0 && errno != ENOENT)
			fprintf(stderr, "wardtest: unlink(%s): %s\n",
				tmp, strerror(errno));
		errno = saved;
	}
	return ret;
}

int wt_chunk_read(const char *dir, uint64_t stripe_id, int shard_idx,
		  struct wt_chunk_header *hdr_out,
		  void **data_out, size_t *data_len_out)
{
	char path[WT_PATH_BUF];
	int fd, ret = 0;
	ssize_t n;

	wt_chunk_path(path, sizeof(path), dir, stripe_id, shard_idx);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	/* Read header */
	struct wt_chunk_header hdr;
	n = read(fd, &hdr, sizeof(hdr));
	if (n != (ssize_t)sizeof(hdr)) {
		ret = (n < 0) ? -errno : -EIO;
		goto err;
	}

	/* Validate header */
	if (hdr.wch_magic != WT_CHUNK_MAGIC ||
	    hdr.wch_version != WT_CHUNK_VERSION) {
		ret = -EINVAL;
		goto err;
	}

	/* Allocate and read shard data */
	size_t data_len = hdr.wch_shard_size;
	void *data = malloc(data_len);
	if (!data) {
		ret = -ENOMEM;
		goto err;
	}

	n = read(fd, data, data_len);
	if (n != (ssize_t)data_len) {
		ret = (n < 0) ? -errno : -EIO;
		free(data);
		goto err;
	}

	if (close(fd) < 0)
		fprintf(stderr, "wardtest: close: %s\n", strerror(errno));

	/* Verify CRC */
	uint32_t actual_crc = wt_crc32(data, data_len);
	if (actual_crc != hdr.wch_crc32) {
		/* CRC mismatch -- return the data anyway for diagnostics */
		*hdr_out = hdr;
		*data_out = data;
		*data_len_out = data_len;
		return -EILSEQ; /* signal CRC failure */
	}

	*hdr_out = hdr;
	*data_out = data;
	*data_len_out = data_len;
	return 0;

err:
	if (close(fd) < 0)
		fprintf(stderr, "wardtest: close: %s\n", strerror(errno));
	return ret;
}
