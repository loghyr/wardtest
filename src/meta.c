/* SPDX-FileCopyrightText: 2026 Tom Haynes <loghyr@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Stripe metadata I/O -- one file per stripe in the meta directory.
 * Atomic writes (write-temp/fsync/rename).
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wardtest.h"

static int meta_path(char *buf, size_t bufsz, const char *dir,
		     uint64_t stripe_id)
{
	int n = snprintf(buf, bufsz, "%s/stripe_%016lx.meta",
			 dir, (unsigned long)stripe_id);
	if (n < 0 || (size_t)n >= bufsz)
		return -ENAMETOOLONG;
	return 0;
}

int wt_meta_write(const char *dir, const struct wt_stripe_meta *meta)
{
	char path[WT_PATH_BUF];
	char tmp[WT_PATH_BUF];
	int fd, ret = 0;
	ssize_t n;

	ret = meta_path(path, sizeof(path), dir, meta->wsm_stripe_id);
	if (ret)
		return ret;

	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -ENAMETOOLONG;

	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -errno;

	n = write(fd, meta, sizeof(*meta));
	if (n != (ssize_t)sizeof(*meta)) {
		ret = (n < 0) ? -errno : -EIO;
		goto err;
	}

	if (fsync(fd) < 0) {
		ret = -errno;
		goto err;
	}

	if (close(fd) < 0) {
		ret = -errno;
		if (unlink(tmp) < 0 && errno != ENOENT)
			fprintf(stderr, "wardtest: unlink(%s): %s\n",
				tmp, strerror(errno));
		return ret;
	}

	if (rename(tmp, path) < 0) {
		ret = -errno;
		if (unlink(tmp) < 0 && errno != ENOENT)
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

int wt_meta_read(const char *dir, uint64_t stripe_id,
		 struct wt_stripe_meta *meta_out)
{
	char path[WT_PATH_BUF];
	int fd, ret;
	ssize_t n;

	ret = meta_path(path, sizeof(path), dir, stripe_id);
	if (ret)
		return ret;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	n = read(fd, meta_out, sizeof(*meta_out));

	if (close(fd) < 0)
		fprintf(stderr, "wardtest: close: %s\n", strerror(errno));

	if (n != (ssize_t)sizeof(*meta_out))
		return (n < 0) ? -errno : -EIO;

	if (meta_out->wsm_magic != WT_META_MAGIC ||
	    meta_out->wsm_version != WT_META_VERSION)
		return -EINVAL;

	return 0;
}

int wt_meta_delete(const char *dir, uint64_t stripe_id)
{
	char path[WT_PATH_BUF];
	int ret;

	ret = meta_path(path, sizeof(path), dir, stripe_id);
	if (ret)
		return ret;

	if (unlink(path) < 0)
		return -errno;
	return 0;
}

uint64_t wt_meta_pick_random(const char *dir, uint32_t seed)
{
	DIR *d;
	struct dirent *de;
	uint64_t *ids = NULL;
	uint32_t count = 0;
	uint32_t capacity = 0;

	d = opendir(dir);
	if (!d)
		return 0;

	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, "stripe_", 7) != 0)
			continue;
		if (strstr(de->d_name, ".meta") == NULL)
			continue;
		/* Skip .tmp files */
		if (strstr(de->d_name, ".tmp") != NULL)
			continue;

		uint64_t id;
		if (sscanf(de->d_name, "stripe_%lx.meta",
			   (unsigned long *)&id) != 1)
			continue;

		if (count >= capacity) {
			capacity = capacity ? capacity * 2 : 64;
			uint64_t *tmp = realloc(ids, capacity * sizeof(*ids));
			if (!tmp)
				break;
			ids = tmp;
		}
		ids[count++] = id;
	}

	if (closedir(d) < 0)
		fprintf(stderr, "wardtest: closedir: %s\n", strerror(errno));

	if (count == 0) {
		free(ids);
		return 0;
	}

	uint32_t idx = seed % count;
	uint64_t result = ids[idx];
	free(ids);
	return result;
}
