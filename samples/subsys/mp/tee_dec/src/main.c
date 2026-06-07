/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tee pipeline sample demonstrating multi-branch pipelines:
 *
 *   [filesrc] → [jpeg_parser] → [caps_filter] → [tee] → [queue1] → [jpeg_dec] → [disp_sink]
 *                                                     → [queue2] → [filesink]
 */

#include <errno.h>
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#include <ff.h>
#endif

#include <zephyr/drivers/video.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

#include <zephyr/mp/core/mp.h>
#include <zephyr/mp/core/mp_queue.h>
#include <zephyr/mp/core/mp_tee.h>
#include <zephyr/mp/zdisp/mp_zdisp_sink.h>
#include <zephyr/mp/zfs/mp_zfilesink.h>
#include <zephyr/mp/zfs/mp_zfilesrc.h>
#include <zephyr/mp/zjpeg/mp_zjpeg_decoder.h>
#include <zephyr/mp/zjpeg/mp_zjpeg_parser.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs */
#define PIPE_ID        0
#define FILE_SRC_ID    1
#define JPEG_PARSER_ID 2
#define CAPS_FILTER_ID 3
#define TEE_ID         4
#define QUEUE1_ID      5
#define JPEG_DEC_ID    6
#define DISP_SINK_ID   7
#define QUEUE2_ID      8
#define FILE_SINK_ID   9

#define MNT_POINT "/SD:"

#if defined(CONFIG_FAT_FILESYSTEM_ELM)
static FATFS fat_fs;
#endif

static struct fs_mount_t mnt = {
	.type = FS_FATFS,
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
	.fs_data = &fat_fs,
#endif
	.mnt_point = MNT_POINT,
};

static const uint8_t mjpeg[] = {
#include "mjpeg.inc"
};
static const size_t mjpeg_sz = sizeof(mjpeg);

static int embed_test_file(void)
{
	struct fs_dirent ent;
	int ret = fs_stat(CONFIG_FILE_INPUT_PATH, &ent);

	if (ret == 0) {
		return 0;
	}

	if (ret != -ENOENT) {
		LOG_ERR("fs_stat(%s) failed (%d)", CONFIG_FILE_INPUT_PATH, ret);
		return ret;
	}

	if (mjpeg_sz == 0) {
		LOG_ERR("MJPEG test file not available");
		return -ENOENT;
	}

	struct fs_file_t f;

	fs_file_t_init(&f);

	ret = fs_open(&f, CONFIG_FILE_INPUT_PATH, FS_O_CREATE | FS_O_WRITE);
	if (ret != 0) {
		LOG_ERR("fs_open(%s) failed (%d)", CONFIG_FILE_INPUT_PATH, ret);
		return ret;
	}

	ssize_t w = fs_write(&f, mjpeg, mjpeg_sz);

	if (w < 0 || (size_t)w != mjpeg_sz) {
		LOG_ERR("fs_write failed (%d)", (int)w);
		(void)fs_close(&f);
		return (w < 0) ? (int)w : -EIO;
	}

	(void)fs_close(&f);
	LOG_INF("Created %s (%u bytes)", CONFIG_FILE_INPUT_PATH, (unsigned int)mjpeg_sz);
	return 0;
}

static int mount_sd(void)
{
	int ret = fs_mount(&mnt);

	if (ret != 0) {
		LOG_ERR("fs_mount failed (%d)", ret);
		return ret;
	}

	LOG_INF("SDCard mounted at %s", MNT_POINT);
	return embed_test_file();
}

/* Pipeline elements */
static struct mp_pipeline pipe;
static struct mp_zfilesrc filesrc;
static struct mp_zjpeg_parser jpeg_parser;
static struct mp_caps_filter caps_filter;
static struct mp_tee tee;
static struct mp_queue queue1;
static struct mp_zjpeg_decoder jpeg_dec;
static struct mp_zdisp_sink disp_sink;
static struct mp_queue queue2;
static struct mp_zfilesink filesink;

int main(void)
{
	int ret;

	ret = mount_sd();
	if (ret != 0) {
		goto err;
	}

	/* Initialize all elements */
	MP_ELEMENT_INIT(&pipe, mp_pipeline_init, PIPE_ID);
	MP_ELEMENT_INIT(&filesrc, mp_zfilesrc_init, FILE_SRC_ID);
	MP_ELEMENT_INIT(&jpeg_parser, mp_zjpeg_parser_init, JPEG_PARSER_ID);
	MP_ELEMENT_INIT(&caps_filter, mp_caps_filter_init, CAPS_FILTER_ID);
	MP_ELEMENT_INIT(&tee, mp_tee_init, TEE_ID);
	MP_ELEMENT_INIT(&queue1, mp_queue_init, QUEUE1_ID);
	MP_ELEMENT_INIT(&jpeg_dec, mp_zjpeg_decoder_init, JPEG_DEC_ID);
	MP_ELEMENT_INIT(&disp_sink, mp_zdisp_sink_init, DISP_SINK_ID);
	MP_ELEMENT_INIT(&queue2, mp_queue_init, QUEUE2_ID);
	MP_ELEMENT_INIT(&filesink, mp_zfilesink_init, FILE_SINK_ID);

	/* Set properties */
	ret = mp_object_set_properties(MP_OBJECT(&filesrc), PROP_ZFILESRC_PATH,
				       CONFIG_FILE_INPUT_PATH, PROP_LIST_END);
	if (ret < 0) {
		LOG_ERR("Failed to set filesrc properties (%d)", ret);
		goto err;
	}

	ret = mp_object_set_properties(MP_OBJECT(&filesink), PROP_ZFILESINK_PATH,
				       CONFIG_FILE_OUTPUT_PATH, PROP_LIST_END);
	if (ret < 0) {
		LOG_ERR("Failed to set filesink properties (%d)", ret);
		goto err;
	}

	/* Set caps filter to constrain negotiation with JPEG format + resolution */
	{
		struct mp_caps *caps = mp_caps_new(
			MP_MEDIA_VIDEO, MP_CAPS_PIXEL_FORMAT, MP_TYPE_UINT, VIDEO_PIX_FMT_JPEG,
			MP_CAPS_IMAGE_WIDTH, MP_TYPE_UINT, CONFIG_JPEG_IMAGE_WIDTH,
			MP_CAPS_IMAGE_HEIGHT, MP_TYPE_UINT, CONFIG_JPEG_IMAGE_HEIGHT, MP_CAPS_END);

		if (caps == NULL) {
			LOG_ERR("Failed to create caps");
			goto err;
		}

		ret = mp_object_set_properties(MP_OBJECT(&caps_filter), PROP_CAPS, caps,
					       PROP_LIST_END);
		mp_caps_unref(caps);
		if (ret < 0) {
			LOG_ERR("Failed to set caps_filter properties (%d)", ret);
			goto err;
		}
	}

	/* Add all elements to the pipeline bin */
	ret = mp_bin_add(MP_BIN(&pipe), MP_ELEMENT(&filesrc), MP_ELEMENT(&jpeg_parser),
			 MP_ELEMENT(&caps_filter), MP_ELEMENT(&tee), MP_ELEMENT(&queue1),
			 MP_ELEMENT(&jpeg_dec), MP_ELEMENT(&disp_sink), MP_ELEMENT(&queue2),
			 MP_ELEMENT(&filesink), NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		goto err;
	}

	/* Branch 1: filesrc → jpeg_parser → caps_filter → tee → queue1 → jpeg_dec → disp_sink */
	ret = mp_element_link(MP_ELEMENT(&filesrc), MP_ELEMENT(&jpeg_parser),
			      MP_ELEMENT(&caps_filter), MP_ELEMENT(&tee), MP_ELEMENT(&queue1),
			      MP_ELEMENT(&jpeg_dec), MP_ELEMENT(&disp_sink), NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link branch 1 (%d)", ret);
		goto err;
	}

	/* Branch 2: tee (2nd srcpad) → queue2 → filesink */
	ret = mp_element_link(MP_ELEMENT(&tee), MP_ELEMENT(&queue2), MP_ELEMENT(&filesink), NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link branch 2 (%d)", ret);
		goto err;
	}

	LOG_INF("Pipeline linked. Starting playback...");

	/* Start the pipeline */
	if (mp_element_set_state(MP_ELEMENT(&pipe), MP_STATE_PLAYING) != MP_STATE_CHANGE_SUCCESS) {
		LOG_ERR("Failed to start pipeline");
		goto err;
	}

	/* Wait for EOS or ERROR on the bus */
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
		mp_message_destroy(msg);
	}

	/* Stop the pipeline */
	(void)mp_element_set_state(MP_ELEMENT(&pipe), MP_STATE_READY);

	ret = fs_unmount(&mnt);
	if (ret != 0) {
		LOG_ERR("fs_unmount failed (%d)", ret);
	}

	LOG_INF("Done.");

	return 0;

err:
	LOG_ERR("Aborting sample");
	return 0;
}
