/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util_macro.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/mpipe_message.h>
#include <zephyr/mpipe/utils/mpipe_player.h>

#include <zephyr/mpipe/aud/mpipe_aud.h>
#include <zephyr/mpipe/aud/mpipe_aud_src.h>
#include <zephyr/mpipe/aud/mpipe_aud_i2s_codec_sink.h>
#include <zephyr/mpipe/aud/mpipe_aud_gain.h>
#include <zephyr/mpipe/aud/mpipe_aud_dmic_src.h>
#include <zephyr/mpipe/aud/mpipe_aud_buffer_pool.h>

#include <zephyr/drivers/video.h>
#include <zephyr/video/controls.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>
#if DT_HAS_CHOSEN(zephyr_jpegdec) || DT_HAS_CHOSEN(zephyr_videotrans)
#include <zephyr/mpipe/vid/mpipe_vid_transform.h>
#endif
#if DT_HAS_CHOSEN(zephyr_jpegdec)
#include <zephyr/mpipe/vid/mpipe_vid_convert.h>
#endif

#if defined(CONFIG_MPIPE_BASE_CAPS_FILTER)
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#endif

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * Element IDs only need to be unique within a single pipeline. Each pipeline
 * has its own enum starting at 0.
 */
enum {
	AUD_PIPE_ID,
	AUD_DMIC_SRC_ID,
	AUD_CAPS_FILTER_ID,
	AUD_GAIN_ID,
	AUD_I2S_SINK_ID,
};

/*
 * WORKAROUND: Direct memory slab management in application code.
 *
 * TODO: Normally, applications should not set this because they do not need to
 * know about the memory slab audio buffers implementation.
 *
 * The __nocache attribute ensures this memory is not cached, which is required
 * for DMA operations used by audio hardware.
 */
__nocache struct k_mem_slab aud_mem_slab;

static struct mpipe aud_pipe;
static struct mpipe_player aud_player;
static struct mpipe_aud_dmic_src aud_source;
static struct mpipe_aud_gain aud_gain;
static struct mpipe_aud_i2s_codec_sink aud_sink;

#if defined(CONFIG_MPIPE_BASE_CAPS_FILTER)
static struct mpipe_caps_filter aud_caps_filter;
#endif

enum {
	VID_PIPE_ID = 1,
	VID_SRC_ID,
	VID_CAPS_FILTER_ID,
	VID_JPEG_DEC_ID,
	VID_CONV_ID,
	VID_TRANS_ID,
	VID_DISP_SINK_ID,
};

static struct mpipe vid_pipe;
static struct mpipe_player vid_player;
static struct mpipe_vid_src vid_src;
static struct mpipe_disp_sink disp_sink;

#if defined(CONFIG_MPIPE_BASE_CAPS_FILTER)
static struct mpipe_caps_filter vid_caps_filter;
#endif
#if (DT_HAS_CHOSEN(zephyr_jpegdec))
static struct mpipe_vid_transform jpeg_dec;
static struct mpipe_vid_convert vid_conv;
#endif
#if (DT_HAS_CHOSEN(zephyr_videotrans))
static struct mpipe_vid_transform vid_trans;
#endif

static int audio_pipeline_build(void)
{
	int gain_val = 90; /* Set gain to 90% (0.9x amplification) */
	int ret;

	ret = mpipe_pipeline_init(&aud_pipe, AUD_PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_aud_dmic_src_init(&aud_source, AUD_DMIC_SRC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_aud_gain_init(&aud_gain, AUD_GAIN_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_aud_i2s_codec_sink_init(&aud_sink, AUD_I2S_SINK_ID);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&aud_source,
					  MPIPE_PROP_AUD_SRC_SLAB_PTR, &aud_mem_slab,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		LOG_ERR("Failed to set properties for audio source element");
		return ret;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&aud_sink, MPIPE_PROP_AUD_SINK_SLAB_PTR, &aud_mem_slab,
#if (defined(CONFIG_USE_I2S_TARGET_CODEC_CONTROLLER) && CONFIG_USE_I2S_TARGET_CODEC_CONTROLLER == 1)
		MPIPE_PROP_AUD_SINK_CLK_ROLE, MPIPE_AUD_I2S_TARGET_CODEC_CONTROLLER,
#endif
		MPIPE_PROP_LIST_END);
	if (ret < 0) {
		LOG_ERR("Failed to set properties for audio sink element");
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&aud_gain,
					  MPIPE_PROP_AUD_TRANSFORM_GAIN, &gain_val,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		LOG_ERR("Failed to set properties for gain element");
		return ret;
	}

#if defined(CONFIG_MPIPE_BASE_CAPS_FILTER)
	ret = mpipe_caps_filter_init(&aud_caps_filter, AUD_CAPS_FILTER_ID);
	if (ret < 0) {
		return ret;
	}

	struct mpipe_structure aud_caps;

	ret = mpipe_structure_init_fields(
		&aud_caps, MPIPE_MEDIA_AUDIO_PCM, MPIPE_CAPS_FRAME_INTERVAL, MPIPE_TYPE_UINT, 10000,
		MPIPE_CAPS_NUM_OF_CHANNEL, MPIPE_TYPE_UINT, 2, MPIPE_CAPS_END);
	if (ret != 0) {
		LOG_ERR("Failed to create audio caps");
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&aud_caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &aud_caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		LOG_ERR("Failed to set properties for audio caps filter element");
		return ret;
	}
#endif /* CONFIG_MPIPE_BASE_CAPS_FILTER */

	/* clang-format off */
	/* Add elements to the pipeline - order does not matter */
	ret = mpipe_bin_add((struct mpipe_bin *)&aud_pipe,
			(struct mpipe_element *)&aud_source,
			IF_ENABLED(CONFIG_MPIPE_BASE_CAPS_FILTER,
				   ((struct mpipe_element *)&aud_caps_filter,))
			(struct mpipe_element *)&aud_gain,
			(struct mpipe_element *)&aud_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add audio elements (%d)", ret);
		return ret;
	}

	/* Link elements together - order does matter */
	ret = mpipe_element_link((struct mpipe_element *)&aud_source,
			IF_ENABLED(CONFIG_MPIPE_BASE_CAPS_FILTER,
				   ((struct mpipe_element *)&aud_caps_filter,))
			(struct mpipe_element *)&aud_gain,
			(struct mpipe_element *)&aud_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link audio elements (%d)", ret);
		return ret;
	}
	/* clang-format on */

	return 0;
}

static int video_pipeline_build(void)
{
	int ret;

	ret = mpipe_pipeline_init(&vid_pipe, VID_PIPE_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_vid_src_init(&vid_src, VID_SRC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_disp_sink_init(&disp_sink, VID_DISP_SINK_ID);
	if (ret < 0) {
		return ret;
	}

	struct video_rect __maybe_unused crop = {
		CONFIG_VIDEO_SOURCE_CROP_LEFT, CONFIG_VIDEO_SOURCE_CROP_TOP,
		CONFIG_VIDEO_SOURCE_CROP_WIDTH, CONFIG_VIDEO_SOURCE_CROP_HEIGHT};

	/* clang-format off */
	ret = mpipe_object_set_properties((struct mpipe_object *)&vid_src,
		COND_CODE_0(CONFIG_PROP_NUM_BUFS, (),
			    (MPIPE_PROP_SRC_NUM_BUFS, CONFIG_PROP_NUM_BUFS,))
		COND_CODE_0(CONFIG_VIDEO_SOURCE_CROP_WIDTH, (), (MPIPE_PROP_VID_CROP, &crop,))
		IF_ENABLED(CONFIG_VIDEO_CTRL_HFLIP, (VIDEO_CID_HFLIP, CONFIG_VIDEO_CTRL_HFLIP,))
		IF_ENABLED(CONFIG_VIDEO_CTRL_VFLIP, (VIDEO_CID_VFLIP, CONFIG_VIDEO_CTRL_VFLIP,))
		MPIPE_PROP_LIST_END);
	/* clang-format on */
	if (ret < 0) {
		return ret;
	}

#if defined(CONFIG_MPIPE_BASE_CAPS_FILTER)
	ret = mpipe_caps_filter_init(&vid_caps_filter, VID_CAPS_FILTER_ID);
	if (ret < 0) {
		return ret;
	}

	/* clang-format off */
	struct mpipe_structure vid_caps;
	struct mpipe_value pixfmt;

	ret = mpipe_structure_init_fields(&vid_caps, MPIPE_MEDIA_VIDEO,
		COND_CODE_0(CONFIG_VIDEO_FRAME_WIDTH,
			(), (MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_WIDTH,))
		COND_CODE_0(CONFIG_VIDEO_FRAME_HEIGHT,
			(), (MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_HEIGHT,))
		COND_CODE_0(CONFIG_VIDEO_FRAME_RATE, (),
			(MPIPE_CAPS_FRAME_INTERVAL, MPIPE_TYPE_UINT,
			 MPIPE_FRAME_INTERVAL_FROM_FPS(CONFIG_VIDEO_FRAME_RATE),))
		MPIPE_CAPS_END);
	/* clang-format on */
	if (ret != 0) {
		return ret;
	}

	if (strcmp(CONFIG_VIDEO_PIXEL_FORMAT, "") != 0) {
		mpipe_value_set(&pixfmt, MPIPE_TYPE_UINT,
				VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_PIXEL_FORMAT));
		mpipe_structure_append_value(&vid_caps, MPIPE_CAPS_PIXEL_FORMAT, &pixfmt);
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&vid_caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &vid_caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}
#endif /* CONFIG_MPIPE_BASE_CAPS_FILTER */

#if (DT_HAS_CHOSEN(zephyr_jpegdec))
	ret = mpipe_vid_transform_init(&jpeg_dec, VID_JPEG_DEC_ID);
	if (ret < 0) {
		return ret;
	}
	ret = mpipe_vid_convert_init(&vid_conv, VID_CONV_ID);
	if (ret < 0) {
		return ret;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&jpeg_dec, MPIPE_PROP_VID_DEVICE,
					  DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_jpegdec)),
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		return ret;
	}
#endif /* DT_HAS_CHOSEN(zephyr_jpegdec) */

#if (DT_HAS_CHOSEN(zephyr_videotrans))
	ret = mpipe_vid_transform_init(&vid_trans, VID_TRANS_ID);
	if (ret < 0) {
		return ret;
	}

	/* clang-format off */
	ret = mpipe_object_set_properties((struct mpipe_object *)&vid_trans,
		COND_CODE_0(CONFIG_VIDEO_ROTATION_ANGLE,
			(), (VIDEO_CID_ROTATE, CONFIG_VIDEO_ROTATION_ANGLE,)) MPIPE_PROP_LIST_END);
	/* clang-format on */
	if (ret < 0) {
		return ret;
	}
#endif /* DT_HAS_CHOSEN(zephyr_videotrans) */

	/* clang-format off */
	/* Add elements to the pipeline - order does not matter */
	ret = mpipe_bin_add((struct mpipe_bin *)&vid_pipe,
			(struct mpipe_element *)&vid_src,
			IF_ENABLED(CONFIG_MPIPE_BASE_CAPS_FILTER,
				   ((struct mpipe_element *)&vid_caps_filter,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_jpegdec),
				   ((struct mpipe_element *)&jpeg_dec,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_jpegdec),
				   ((struct mpipe_element *)&vid_conv,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_videotrans),
				   ((struct mpipe_element *)&vid_trans,))
			(struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add video elements (%d)", ret);
		return ret;
	}

	/* Link elements together - order does matter */
	ret = mpipe_element_link((struct mpipe_element *)&vid_src,
			IF_ENABLED(CONFIG_MPIPE_BASE_CAPS_FILTER,
				   ((struct mpipe_element *)&vid_caps_filter,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_jpegdec),
				   ((struct mpipe_element *)&jpeg_dec,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_jpegdec),
				   ((struct mpipe_element *)&vid_conv,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_videotrans),
				   ((struct mpipe_element *)&vid_trans,))
			(struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link video elements (%d)", ret);
		return ret;
	}
	/* clang-format on */

	return 0;
}

int main(void)
{
	int ret = 0;

	ret = audio_pipeline_build();
	if (ret < 0) {
		goto err;
	}

	ret = video_pipeline_build();
	if (ret < 0) {
		goto err;
	}

	/*
	 * Give each pipeline its own player. Each player registers with
	 * the mpipe_player layer, so the shell commands and single-letter
	 * shortcuts (p/s/r/q) drive every pipeline at once.
	 */
	ret = mpipe_player_init(&aud_player, &aud_pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init audio player (%d)", ret);
		goto err_deinit;
	}

	ret = mpipe_player_init(&vid_player, &vid_pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init video player (%d)", ret);
		goto err_deinit;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&aud_pipe, MPIPE_PROP_PIPELINE_THREAD_PRIORITY,
		&(intptr_t){CONFIG_APP_AUDIO_PIPELINE_THREAD_PRIORITY}, MPIPE_PROP_LIST_END);
	if (ret != 0) {
		LOG_ERR("Failed to set audio pipeline thread priority (%d)", ret);
		goto err_deinit;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&vid_pipe, MPIPE_PROP_PIPELINE_THREAD_PRIORITY,
		&(intptr_t){CONFIG_APP_VIDEO_PIPELINE_THREAD_PRIORITY}, MPIPE_PROP_LIST_END);
	if (ret != 0) {
		LOG_ERR("Failed to set video pipeline thread priority (%d)", ret);
		goto err_deinit;
	}

	LOG_INF("Starting dual pipeline (aud prio %d, vid prio %d)",
		CONFIG_APP_AUDIO_PIPELINE_THREAD_PRIORITY,
		CONFIG_APP_VIDEO_PIPELINE_THREAD_PRIORITY);

	// (void)mpipe_player_play(&aud_player);
	// (void)mpipe_player_play(&vid_player);

	(void)mpipe_player_wait_quit(&aud_player);
	(void)mpipe_player_deinit(&aud_player);

	(void)mpipe_player_wait_quit(&vid_player);
	(void)mpipe_player_deinit(&vid_player);

	LOG_INF("Done.");
	return 0;

err_deinit:
	(void)mpipe_player_deinit(&aud_player);
	(void)mpipe_player_deinit(&vid_player);
err:
	LOG_ERR("Aborting sample");
	return ret != 0 ? ret : -EIO;
}
