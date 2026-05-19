/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <ff.h>

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

#include <zephyr/mp/core/mp.h>
#include <zephyr/mp/zfs/mp_zfilesink.h>
#include <zephyr/mp/zfs/mp_zfilesrc.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define PIPE_ID      0
#define FILE_SRC_ID  1
#define FILE_SINK_ID 2

#define MNT_POINT "/SD:"

#define INPUT_FILE  "test_in.txt"
#define OUTPUT_FILE "test_out.txt"

static FATFS fat_fs;

static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MNT_POINT,
};

static struct mp_pipeline pipe;
static struct mp_zfilesrc filesrc;
static struct mp_zfilesink filesink;

int main(void)
{
	int ret;

	/* Mount the disk */
	ret = fs_mount(&mp);
	if (ret != 0) {
		LOG_ERR("Failed to mount disk (%d)", ret);
		goto err;
	}

	/* Create a file for test */
	{
		struct fs_file_t fh;
		static const char *path = MNT_POINT "/" INPUT_FILE;
		static const char data[] = "test\n";

		fs_file_t_init(&fh);

		ret = fs_open(&fh, path, FS_O_CREATE | FS_O_WRITE);
		if (ret != 0) {
			LOG_ERR("fs_open failed for %s (%d)", path, ret);
			goto err;
		}

		ssize_t wr = fs_write(&fh, data, sizeof(data) - 1);

		if (wr <= 0) {
			LOG_ERR("fs_write returned %u", wr);
			(void)fs_close(&fh);
			goto err;
		}

		ret = fs_close(&fh);
		if (ret != 0) {
			LOG_ERR("fs_close failed (%d)", ret);
			goto err;
		}

		LOG_INF("Wrote %u bytes to %s", wr, path);
	}

	/* Build the pipeline */
	MP_ELEMENT_INIT(&pipe, mp_pipeline_init, PIPE_ID);
	MP_ELEMENT_INIT(&filesrc, mp_zfilesrc_init, FILE_SRC_ID);
	MP_ELEMENT_INIT(&filesink, mp_zfilesink_init, FILE_SINK_ID);

	ret = mp_object_set_properties(MP_OBJECT(&filesrc), PROP_ZFILESRC_PATH,
				       MNT_POINT "/" INPUT_FILE, PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	ret = mp_object_set_properties(MP_OBJECT(&filesink), PROP_ZFILESINK_PATH,
				       MNT_POINT "/" OUTPUT_FILE, PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	/* Add elements to the pipeline - order does not matter */
	ret = mp_bin_add(MP_BIN(&pipe), MP_ELEMENT(&filesrc), MP_ELEMENT(&filesink), NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		goto err;
	}

	/* Link elements together - order does matter */
	ret = mp_element_link(MP_ELEMENT(&filesrc), MP_ELEMENT(&filesink), NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link elements (%d)", ret);
		goto err;
	}

	/* Start the pipeline */
	if (mp_element_set_state(MP_ELEMENT(&pipe), MP_STATE_PLAYING) != MP_STATE_CHANGE_SUCCESS) {
		LOG_ERR("Failed to start pipeline");
		goto err;
	}

	/* Handle message from the pipeline */
	struct mp_bus *bus = mp_element_get_bus(MP_ELEMENT(&pipe));
	struct mp_message *msg = mp_bus_pop_msg(bus, MP_MESSAGE_ERROR | MP_MESSAGE_EOS);

	if (msg != NULL) {
		switch (msg->type) {
		case MP_MESSAGE_ERROR:
			LOG_INF("ERROR message from element %d", msg->src->id);
			break;
		case MP_MESSAGE_EOS:
			LOG_INF("EOS message from element %d", msg->src->id);
			break;
		default:
			LOG_ERR("Unexpected message from element %d", msg->src->id);
			break;
		}
	}
	mp_message_destroy(msg);

	/* Stop/Deinit the pipeline */
	(void)mp_element_set_state(MP_ELEMENT(&pipe), MP_STATE_READY);

	/* Unmount the disk */
	ret = fs_unmount(&mp);
	if (ret != 0) {
		LOG_ERR("Failed to unmount disk (%d)", ret);
	}

	return 0;

err:
	LOG_ERR("Aborting sample");
	return 0;
}
