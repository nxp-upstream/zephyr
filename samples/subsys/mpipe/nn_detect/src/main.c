/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Live detection with overlay: the camera stream is shown with the detected
 * boxes drawn on top, while the detector runs on a second branch.
 *
 * With CONFIG_SAMPLE_OVERLAY_PLANE the display controller blends the boxes
 * over the video: the camera frame reaches plane 0 untouched and the overlay
 * draws each result set into a transparent frame of its own on plane 1.
 *
 *   vid_src -> caps_filter -> tee -> queue_d -> [vid_trans] -> disp_sink (plane 0)
 *                                -> queue_nn -> nn_converter -> queue_tensor
 *                                     -> nn_runner -> nn_decoder -> vid_overlay
 *                                     -> disp_sink (plane 1)
 *
 * Otherwise the overlay draws into the video frame before it is displayed:
 *
 *   vid_src -> caps_filter -> tee -> queue_d -> vid_overlay -> [vid_trans] -> disp_sink
 *                                -> queue_nn -> nn_converter -> queue_tensor
 *                                     -> nn_runner -> nn_decoder -> app_sink
 *
 * where the app_sink callback publishes each result set into a latest-results
 * store and the overlay draw callback renders it, one or two frames late.
 */

#include <string.h>

#include <zephyr/drivers/video.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/video/controls.h>
#include <zephyr/logging/log.h>
#include <zephyr/mpipe/mpipe.h>
#if !defined(CONFIG_SAMPLE_OVERLAY_PLANE)
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#endif
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/base/mpipe_queue.h>
#include <zephyr/mpipe/base/mpipe_tee.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/nn/mpipe_nn_converter.h>
#include <zephyr/mpipe/nn/mpipe_nn_decoder.h>
#include <zephyr/mpipe/nn/mpipe_nn_runner.h>
#include <zephyr/mpipe/vid/mpipe_vid_draw.h>
#include <zephyr/mpipe/vid/mpipe_vid_overlay.h>
#if DT_HAS_CHOSEN(zephyr_videotrans)
#include <zephyr/mpipe/vid/mpipe_vid_transform.h>
#endif
#if !defined(CONFIG_MPIPE_NN_BACKEND_TFLM)
#include <zephyr/mpipe/nn/mpipe_nn_backend_fake.h>
#endif
#include <zephyr/sys/util_macro.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	VID_SRC_ID,
	CAPS_FILTER_ID,
	TEE_ID,
	QUEUE_D_ID,
	OVERLAY_ID,
	VID_TRANS_ID,
	DISP_SINK_ID,
	QUEUE_NN_ID,
	NN_CONVERTER_ID,
	QUEUE_TENSOR_ID,
	NN_RUNNER_ID,
	NN_DECODER_ID,
	APP_SINK_ID,
	OVERLAY_DISP_SINK_ID,
};

/* The frame on the panel: the camera frame, turned where a transform rotates it */
#if CONFIG_VIDEO_ROTATION_ANGLE == 90 || CONFIG_VIDEO_ROTATION_ANGLE == 270
#define PANEL_FRAME_WIDTH  CONFIG_VIDEO_FRAME_HEIGHT
#define PANEL_FRAME_HEIGHT CONFIG_VIDEO_FRAME_WIDTH
#else
#define PANEL_FRAME_WIDTH  CONFIG_VIDEO_FRAME_WIDTH
#define PANEL_FRAME_HEIGHT CONFIG_VIDEO_FRAME_HEIGHT
#endif

#if !defined(CONFIG_SAMPLE_OVERLAY_PLANE)
/* The overlay in the display branch reads what the app_sink publishes here */
static struct mpipe_nn_results_store results_store;
#endif

#if defined(CONFIG_MPIPE_NN_BACKEND_TFLM)

static const uint8_t model_data[] __aligned(16) = {
#include "nn_model.inc"
};

#if defined(CONFIG_TFLM_NXP_NEUTRON)
/* The NPU reads the model and the arena in place: both live in a region it reaches */
#define NPU_SECTION Z_GENERIC_SECTION(CONFIG_SAMPLE_NN_NPU_REGION_NAME)
static uint8_t npu_model[sizeof(model_data)] __aligned(16) NPU_SECTION;
static uint8_t arena[CONFIG_SAMPLE_NN_ARENA_SIZE] __aligned(16) NPU_SECTION;
#else
static uint8_t arena[CONFIG_SAMPLE_NN_ARENA_SIZE] __aligned(16);
#endif

#else /* fake backend */

static const struct mpipe_nn_fake_model fake_model_cfg = {
	.in_w = 96,
	.in_h = 96,
	.channels = 1,
	.type = MPIPE_NN_TENSOR_TYPE_INT8,
	.num_classes = 0,
	.num_boxes = 2,
};

static uint8_t arena[64];

#endif /* CONFIG_MPIPE_NN_BACKEND_TFLM */

static void log_results(const struct mpipe_nn_results *res)
{
	if (res->count > 0U) {
		LOG_INF("Frame %u: %u box(es), best score %d%% (class %u)", res->seq, res->count,
			(int)(res->boxes[0].score * 100.0f), res->boxes[0].class_id);
	} else if ((res->seq % 64U) == 0U) {
		/* A heartbeat while nothing is detected */
		LOG_INF("Frame %u: no box", res->seq);
	}
}

/*
 * A box in the camera frame, as the detector sees it, placed in the frame
 * drawn: the camera frame itself, or the panel's frame after the rotation the
 * transform applies to the video, which the plane never goes through.
 */
static void box_to_rect(const struct mpipe_nn_box *box, uint16_t width, uint16_t height,
			bool rotated, struct mpipe_vid_rect *rect)
{
	float x0 = box->xmin;
	float y0 = box->ymin;
	float x1 = box->xmax;
	float y1 = box->ymax;

	if (rotated) {
		switch (CONFIG_VIDEO_ROTATION_ANGLE) {
		case 90:
			x0 = 1.0f - box->ymax;
			x1 = 1.0f - box->ymin;
			y0 = box->xmin;
			y1 = box->xmax;
			break;
		case 180:
			x0 = 1.0f - box->xmax;
			x1 = 1.0f - box->xmin;
			y0 = 1.0f - box->ymax;
			y1 = 1.0f - box->ymin;
			break;
		case 270:
			x0 = box->ymin;
			x1 = box->ymax;
			y0 = 1.0f - box->xmax;
			y1 = 1.0f - box->xmin;
			break;
		default:
			break;
		}
	}

	rect->x0 = (int32_t)(x0 * (float)width);
	rect->y0 = (int32_t)(y0 * (float)height);
	rect->x1 = (int32_t)(x1 * (float)width);
	rect->y1 = (int32_t)(y1 * (float)height);
}

/*
 * Render a result set: on a plane, the one that just arrived from the
 * decoder, in the thread of the inference branch; in the frame, the latest
 * published one, in the thread of the display branch.
 */
static void draw_cb(uint8_t *frame, uint16_t width, uint16_t height, uint32_t pixfmt,
		    const struct net_buf *in, void *user_data)
{
	struct mpipe_nn_results res;

	ARG_UNUSED(user_data);

#if defined(CONFIG_SAMPLE_OVERLAY_PLANE)
	if (mpipe_buffer_get_meta(in)->bytes_used < sizeof(res)) {
		return;
	}
	memcpy(&res, in->data, sizeof(res));
	log_results(&res);
#else
	ARG_UNUSED(in);
	if (mpipe_nn_results_store_get(&results_store, &res) != 0) {
		return;
	}
#endif
	if (res.type != MPIPE_NN_RESULTS_DETECTION) {
		return;
	}

	for (uint8_t i = 0; i < res.count; i++) {
		struct mpipe_vid_rect rect;

		box_to_rect(&res.boxes[i], width, height, IS_ENABLED(CONFIG_SAMPLE_OVERLAY_PLANE),
			    &rect);
		(void)mpipe_vid_draw_rect(frame, width, height, pixfmt, &rect, 0xFF00FF00, 2);
	}
}

#if !defined(CONFIG_SAMPLE_OVERLAY_PLANE)
/* Runs in the inference branch thread: publish the boxes for the overlay */
static void results_cb(const struct net_buf *buf, void *user_data)
{
	const struct mpipe_nn_results *res =
		(const struct mpipe_nn_results *)(const void *)buf->data;

	ARG_UNUSED(user_data);

	if (mpipe_buffer_get_meta(buf)->bytes_used < sizeof(*res)) {
		return;
	}

	mpipe_nn_results_store_put(&results_store, res);
	log_results(res);
}
#endif

int main(void)
{
	static struct mpipe pipe;
	static struct mpipe_vid_src vid_src;
	static struct mpipe_caps_filter caps_filter;
	static struct mpipe_tee tee;
	static struct mpipe_queue queue_d;
	static struct mpipe_queue queue_nn;
	static struct mpipe_queue queue_tensor;
	static struct mpipe_vid_overlay overlay;
#if DT_HAS_CHOSEN(zephyr_videotrans)
	static struct mpipe_vid_transform vid_trans;
#endif
	static struct mpipe_disp_sink disp_sink;
	static struct mpipe_nn_converter nn_converter;
	static struct mpipe_nn_runner nn_runner;
	static struct mpipe_nn_decoder nn_decoder;
#if defined(CONFIG_SAMPLE_OVERLAY_PLANE)
	static struct mpipe_disp_sink overlay_disp_sink;
#else
	static struct mpipe_app_sink app_sink;
#endif
	static struct mpipe_player player;
#if defined(CONFIG_SAMPLE_OVERLAY_PLANE)
	const int video_plane = 0;
	const int overlay_plane = 1;
	static const enum display_blend_mode overlay_blend = DISPLAY_BLEND_COVERAGE;
	static const enum mpipe_vid_overlay_mode overlay_mode = MPIPE_VID_OVERLAY_MODE_PLANE;
	static const uint16_t overlay_width = PANEL_FRAME_WIDTH;
	static const uint16_t overlay_height = PANEL_FRAME_HEIGHT;
#else
	static const struct mpipe_app_sink_cb cb = {.fn = results_cb, .user_data = NULL};
#endif
#if defined(CONFIG_TFLM_NXP_NEUTRON)
	static const struct mpipe_nn_model model = {.data = npu_model, .size = sizeof(npu_model)};
#elif defined(CONFIG_MPIPE_NN_BACKEND_TFLM)
	static const struct mpipe_nn_model model = {.data = model_data, .size = sizeof(model_data)};
#else
	static const struct mpipe_nn_model model = {.data = (const uint8_t *)&fake_model_cfg,
						    .size = sizeof(fake_model_cfg)};
#endif
	static const struct mpipe_nn_arena arena_desc = {.data = arena, .size = sizeof(arena)};
	static const uint8_t nn_queue_size = 1;
	static const enum mpipe_base_queue_leak nn_queue_leak = MPIPE_BASE_QUEUE_LEAK_OLDEST;
	static const uint8_t display_queue_size = 1;
	static const int frame_queue_priority = CONFIG_MPIPE_THREAD_DEFAULT_PRIORITY - 1;
	static const int nn_thread_priority = K_LOWEST_APPLICATION_THREAD_PRIO;
	static const float threshold = (float)CONFIG_SAMPLE_NN_SCORE_THRESHOLD / 100.0f;
	static const uint8_t decode_type = MPIPE_NN_RESULTS_DETECTION;
	static const struct mpipe_vid_overlay_cb draw = {.fn = draw_cb, .user_data = NULL};
	int ret;

#if defined(CONFIG_TFLM_NXP_NEUTRON)
	memcpy(npu_model, model_data, sizeof(npu_model));
#endif
#if !defined(CONFIG_SAMPLE_OVERLAY_PLANE)
	mpipe_nn_results_store_init(&results_store);
#endif

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_vid_src_init(&vid_src, VID_SRC_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_caps_filter_init(&caps_filter, CAPS_FILTER_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_tee_init(&tee, TEE_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_queue_init(&queue_d, QUEUE_D_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_queue_init(&queue_nn, QUEUE_NN_ID);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_queue_init(&queue_tensor, QUEUE_TENSOR_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_vid_overlay_init(&overlay, OVERLAY_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_disp_sink_init(&disp_sink, DISP_SINK_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_nn_converter_init(&nn_converter, NN_CONVERTER_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_nn_runner_init(&nn_runner, NN_RUNNER_ID);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_nn_decoder_init(&nn_decoder, NN_DECODER_ID);
	if (ret < 0) {
		goto err;
	}
#if defined(CONFIG_SAMPLE_OVERLAY_PLANE)
	ret = mpipe_disp_sink_init(&overlay_disp_sink, OVERLAY_DISP_SINK_ID);
#else
	ret = mpipe_app_sink_init(&app_sink, APP_SINK_ID);
#endif
	if (ret < 0) {
		goto err;
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
		goto err;
	}

	/* clang-format off */
	struct mpipe_structure caps;
	struct mpipe_value pixfmt;

	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_VIDEO,
		COND_CODE_0(CONFIG_VIDEO_FRAME_WIDTH,
			(), (MPIPE_CAPS_IMAGE_WIDTH, MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_WIDTH,))
		COND_CODE_0(CONFIG_VIDEO_FRAME_HEIGHT,
			(), (MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_HEIGHT,))
		MPIPE_CAPS_END);
	/* clang-format on */
	if (ret != 0) {
		goto err;
	}

	if (strcmp(CONFIG_VIDEO_PIXEL_FORMAT, "") != 0) {
		ret = mpipe_value_set(&pixfmt, MPIPE_TYPE_UINT,
				      VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_PIXEL_FORMAT));
		if (ret < 0) {
			goto err;
		}

		ret = mpipe_structure_append_value(&caps, MPIPE_CAPS_PIXEL_FORMAT, &pixfmt);
		if (ret < 0) {
			goto err;
		}
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	/*
	 * Two single-slot queues dropping the oldest: the converter runs one priority
	 * above the streaming threads so it releases the shared frame before the overlay
	 * draws in it, the runner at the lowest priority so a slow model starves nothing
	 */
	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&queue_nn, MPIPE_PROP_BASE_QUEUE_SIZE, &nn_queue_size,
		MPIPE_PROP_BASE_QUEUE_LEAK, &nn_queue_leak, MPIPE_PROP_BASE_QUEUE_THREAD_PRIORITY,
		&frame_queue_priority, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&queue_tensor, MPIPE_PROP_BASE_QUEUE_SIZE, &nn_queue_size,
		MPIPE_PROP_BASE_QUEUE_LEAK, &nn_queue_leak, MPIPE_PROP_BASE_QUEUE_THREAD_PRIORITY,
		&nn_thread_priority, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	/* The display branch queue: one slot, so the camera pool stays within the CSI's four */
	ret = mpipe_object_set_properties((struct mpipe_object *)&queue_d,
					  MPIPE_PROP_BASE_QUEUE_SIZE, &display_queue_size,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

#ifdef CONFIG_SAMPLE_NN_INPUT_SHIFT
	static const int32_t zero_point = CONFIG_SAMPLE_NN_INPUT_ZERO_POINT;

	ret = mpipe_object_set_properties((struct mpipe_object *)&nn_converter,
					  MPIPE_PROP_NN_CONVERTER_ZERO_POINT, &zero_point,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}
#endif

	ret = mpipe_object_set_properties((struct mpipe_object *)&nn_runner, MPIPE_PROP_NN_MODEL,
					  &model, MPIPE_PROP_NN_ARENA, &arena_desc,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	ret = mpipe_object_set_properties(
		(struct mpipe_object *)&nn_decoder, MPIPE_PROP_NN_DECODER_TYPE, &decode_type,
		MPIPE_PROP_NN_DECODER_THRESHOLD, &threshold, MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}

	/* clang-format off */
	ret = mpipe_object_set_properties((struct mpipe_object *)&overlay,
		MPIPE_PROP_VID_OVERLAY_DRAW_CB, &draw,
		IF_ENABLED(CONFIG_SAMPLE_OVERLAY_PLANE,
			   (MPIPE_PROP_VID_OVERLAY_MODE, &overlay_mode,
			    MPIPE_PROP_VID_OVERLAY_WIDTH, &overlay_width,
			    MPIPE_PROP_VID_OVERLAY_HEIGHT, &overlay_height,))
		MPIPE_PROP_LIST_END);
	/* clang-format on */
	if (ret < 0) {
		goto err;
	}

#if defined(CONFIG_SAMPLE_OVERLAY_PLANE)
	/* The video on the background plane, the boxes blended over it by their pixel alpha */
	ret = mpipe_object_set_properties((struct mpipe_object *)&disp_sink,
					  MPIPE_PROP_DISP_SINK_PLANE, &video_plane,
					  MPIPE_PROP_LIST_END);
	if (ret < 0) {
		goto err;
	}
	ret = mpipe_object_set_properties((struct mpipe_object *)&overlay_disp_sink,
					  MPIPE_PROP_DISP_SINK_PLANE, &overlay_plane,
					  MPIPE_PROP_DISP_SINK_BLEND_MODE, &overlay_blend,
					  MPIPE_PROP_LIST_END);
#else
	ret = mpipe_object_set_properties((struct mpipe_object *)&app_sink,
					  MPIPE_PROP_BASE_APP_SINK_CB, &cb, MPIPE_PROP_LIST_END);
#endif
	if (ret < 0) {
		goto err;
	}

#if DT_HAS_CHOSEN(zephyr_videotrans)
	ret = mpipe_vid_transform_init(&vid_trans, VID_TRANS_ID);
	if (ret < 0) {
		goto err;
	}

	/* clang-format off */
	ret = mpipe_object_set_properties((struct mpipe_object *)&vid_trans,
		COND_CODE_0(CONFIG_VIDEO_ROTATION_ANGLE,
			(), (VIDEO_CID_ROTATE, CONFIG_VIDEO_ROTATION_ANGLE,)) MPIPE_PROP_LIST_END);
	/* clang-format on */
	if (ret < 0) {
		goto err;
	}
#endif

	/* clang-format off */
	/* Add elements to the pipeline - order does not matter */
	ret = mpipe_bin_add((struct mpipe_bin *)&pipe,
			(struct mpipe_element *)&vid_src,
			(struct mpipe_element *)&caps_filter,
			(struct mpipe_element *)&tee,
			(struct mpipe_element *)&queue_d,
			(struct mpipe_element *)&overlay,
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_videotrans),
				   ((struct mpipe_element *)&vid_trans,))
			(struct mpipe_element *)&disp_sink,
			(struct mpipe_element *)&queue_nn,
			(struct mpipe_element *)&nn_converter,
			(struct mpipe_element *)&queue_tensor,
			(struct mpipe_element *)&nn_runner,
			(struct mpipe_element *)&nn_decoder,
			COND_CODE_1(CONFIG_SAMPLE_OVERLAY_PLANE,
				    ((struct mpipe_element *)&overlay_disp_sink),
				    ((struct mpipe_element *)&app_sink)), NULL);
	if (ret < 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		goto err;
	}

	/* Branch 1: camera to display; in the frame, the boxes are drawn before the transform */
	ret = mpipe_element_link((struct mpipe_element *)&vid_src,
			(struct mpipe_element *)&caps_filter,
			(struct mpipe_element *)&tee,
			(struct mpipe_element *)&queue_d,
			IF_DISABLED(CONFIG_SAMPLE_OVERLAY_PLANE,
				    ((struct mpipe_element *)&overlay,))
			IF_ENABLED(DT_HAS_CHOSEN(zephyr_videotrans),
				   ((struct mpipe_element *)&vid_trans,))
			(struct mpipe_element *)&disp_sink, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link branch 1 (%d)", ret);
		goto err;
	}

	/* Branch 2: tee (2nd src pad) to the detector; on a plane, the boxes end here */
	ret = mpipe_element_link((struct mpipe_element *)&tee,
			(struct mpipe_element *)&queue_nn,
			(struct mpipe_element *)&nn_converter,
			(struct mpipe_element *)&queue_tensor,
			(struct mpipe_element *)&nn_runner,
			(struct mpipe_element *)&nn_decoder,
			COND_CODE_1(CONFIG_SAMPLE_OVERLAY_PLANE,
				    ((struct mpipe_element *)&overlay,
				     (struct mpipe_element *)&overlay_disp_sink),
				    ((struct mpipe_element *)&app_sink)), NULL);
	if (ret < 0) {
		LOG_ERR("Failed to link branch 2 (%d)", ret);
		goto err;
	}
	/* clang-format on */

	LOG_INF("Pipeline linked.");

	ret = mpipe_player_init(&player, &pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init player (%d)", ret);
		goto err;
	}

	(void)mpipe_player_play(&player);
	(void)mpipe_player_wait_quit(&player);
	(void)mpipe_player_deinit(&player);

	LOG_INF("Done.");

	return 0;

err:
	LOG_ERR("Aborting sample");
	return 0;
}
