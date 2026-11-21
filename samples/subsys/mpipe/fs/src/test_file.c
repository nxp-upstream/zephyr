/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Write the file the pipeline of the devicetree copies, before it is built
 * and played.
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(test_file, CONFIG_LOG_DEFAULT_LEVEL);

#define INPUT_PATH DT_PROP(DT_NODELABEL(file_src), path)

BUILD_ASSERT(CONFIG_APPLICATION_INIT_PRIORITY < CONFIG_MPIPE_DT_INIT_PRIORITY,
	     "The test file must be written before the pipelines are played");

static int test_file_init(void)
{
	static const char data[] = "test\n";
	struct fs_file_t file;
	ssize_t written;
	int ret;

	fs_file_t_init(&file);

	ret = fs_open(&file, INPUT_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		LOG_ERR("fs_open(%s) failed (%d)", INPUT_PATH, ret);
		return ret;
	}

	written = fs_write(&file, data, sizeof(data) - 1U);
	(void)fs_close(&file);

	if (written != (ssize_t)(sizeof(data) - 1U)) {
		LOG_ERR("fs_write failed (%d)", (int)written);
		return (written < 0) ? (int)written : -EIO;
	}

	LOG_INF("Wrote %d bytes to %s", (int)written, INPUT_PATH);

	return 0;
}

SYS_INIT(test_file_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
