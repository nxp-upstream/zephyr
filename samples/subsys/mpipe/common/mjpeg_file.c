/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Write the test MJPEG file generated at build time to the path the file
 * source node MJPEG_FILE_SRC plays, before the pipelines of the devicetree
 * are built and played.
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(mjpeg_file, CONFIG_LOG_DEFAULT_LEVEL);

#define MJPEG_FILE_PATH DT_PROP(DT_NODELABEL(MJPEG_FILE_SRC), path)

BUILD_ASSERT(CONFIG_APPLICATION_INIT_PRIORITY < CONFIG_MPIPE_DT_INIT_PRIORITY,
	     "The test file must be written before the pipelines are played");

static int mjpeg_file_init(void)
{
	static const uint8_t mjpeg[] = {
#include "mjpeg.inc"
	};
	struct fs_dirent ent;
	struct fs_file_t file;
	ssize_t written;
	int ret = fs_stat(MJPEG_FILE_PATH, &ent);

	/* A file of the same size is assumed to be the embedded one: keep it */
	if (ret == 0 && ent.size == sizeof(mjpeg)) {
		return 0;
	}

	if (ret != 0 && ret != -ENOENT) {
		LOG_ERR("fs_stat(%s) failed (%d)", MJPEG_FILE_PATH, ret);
		return ret;
	}

	fs_file_t_init(&file);

	ret = fs_open(&file, MJPEG_FILE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		LOG_ERR("fs_open(%s) failed (%d)", MJPEG_FILE_PATH, ret);
		return ret;
	}

	written = fs_write(&file, mjpeg, sizeof(mjpeg));
	(void)fs_close(&file);

	if (written != (ssize_t)sizeof(mjpeg)) {
		LOG_ERR("fs_write failed (%d)", (int)written);
		return (written < 0) ? (int)written : -EIO;
	}

	LOG_INF("Created %s (%u bytes)", MJPEG_FILE_PATH, (unsigned int)sizeof(mjpeg));

	return 0;
}

SYS_INIT(mjpeg_file_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
