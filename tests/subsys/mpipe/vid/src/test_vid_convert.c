/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/vid/mpipe_vid_convert.h>

enum {
	PIPE_ID,
	SRC_ID,
	CONVERT_ID,
	SINK_ID,
};

#define TEST_WIDTH       8
#define TEST_HEIGHT      4
#define TEST_PIXELS      (TEST_WIDTH * TEST_HEIGHT)
#define TEST_YUV420_SIZE (TEST_PIXELS * 3 / 2)

BUILD_ASSERT(TEST_PIXELS * 4 <= CONFIG_MPIPE_BASE_APP_SRC_BUF_SZ,
	     "the application source must hold one frame of 32 bits per pixel");

static K_SEM_DEFINE(converted_sem, 0, 1);

/* The frame the converter produced */
static struct {
	uint32_t size;
	uint8_t data[TEST_YUV420_SIZE];
} converted;

struct test_vid_convert_fixture {
	struct mpipe pipeline;
	struct mpipe_app_src src;
	struct mpipe_vid_convert convert;
	struct mpipe_app_sink sink;
};

static void converted_cb(const struct net_buf *buf, void *user_data)
{
	ARG_UNUSED(user_data);

	converted.size = mpipe_buffer_get_meta(buf)->bytes_used;
	if (converted.size <= sizeof(converted.data)) {
		memcpy(converted.data, buf->data, converted.size);
	}

	k_sem_give(&converted_sem);
}

static void *vid_convert_suite_setup(void)
{
	static struct test_vid_convert_fixture fixture;

	return &fixture;
}

static void vid_convert_before(void *f)
{
	static const struct mpipe_app_sink_cb cb = {.fn = converted_cb};
	struct test_vid_convert_fixture *fix = f;

	memset(fix, 0, sizeof(*fix));
	memset(&converted, 0, sizeof(converted));
	k_sem_reset(&converted_sem);

	zassert_ok(mpipe_pipeline_init(&fix->pipeline, PIPE_ID));
	zassert_ok(mpipe_app_src_init(&fix->src, SRC_ID));
	zassert_ok(mpipe_vid_convert_init(&fix->convert, CONVERT_ID));
	zassert_ok(mpipe_app_sink_init(&fix->sink, SINK_ID));
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->sink,
					       MPIPE_PROP_BASE_APP_SINK_CB, &cb,
					       MPIPE_PROP_LIST_END));

	zassert_ok(mpipe_bin_add((struct mpipe_bin *)&fix->pipeline,
				 (struct mpipe_element *)&fix->src,
				 (struct mpipe_element *)&fix->convert,
				 (struct mpipe_element *)&fix->sink, NULL));
	zassert_ok(mpipe_element_link((struct mpipe_element *)&fix->src,
				      (struct mpipe_element *)&fix->convert,
				      (struct mpipe_element *)&fix->sink, NULL));
}

static void vid_convert_after(void *f)
{
	struct test_vid_convert_fixture *fix = f;

	/* Also runs when the test failed before stopping its pipeline */
	(void)mpipe_element_set_state((struct mpipe_element *)&fix->pipeline, MPIPE_STATE_READY);
}

ZTEST_SUITE(test_vid_convert, NULL, vid_convert_suite_setup, vid_convert_before, vid_convert_after,
	    NULL);

static void video_caps(struct mpipe_structure *caps, uint32_t pixfmt)
{
	zassert_ok(mpipe_structure_init_fields(caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					       MPIPE_TYPE_UINT, pixfmt, MPIPE_CAPS_IMAGE_WIDTH,
					       MPIPE_TYPE_UINT, TEST_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT,
					       MPIPE_TYPE_UINT, TEST_HEIGHT, MPIPE_CAPS_END));
}

/* Give one frame of 32 bits per pixel to the converter and wait for its YUV 4:2:0 frame */
static void convert_to_yuv420(struct test_vid_convert_fixture *fix, uint32_t pixfmt,
			      const uint8_t *frame)
{
	struct mpipe_element *pipe = (struct mpipe_element *)&fix->pipeline;
	struct mpipe_structure caps;

	video_caps(&caps, pixfmt);
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->src,
					       MPIPE_PROP_BASE_APP_SRC_CAPS, &caps,
					       MPIPE_PROP_LIST_END));
	video_caps(&caps, VIDEO_PIX_FMT_YUV420);
	zassert_ok(mpipe_object_set_properties((struct mpipe_object *)&fix->sink,
					       MPIPE_PROP_BASE_APP_SINK_CAPS, &caps,
					       MPIPE_PROP_LIST_END));

	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_PLAYING));
	zassert_ok(mpipe_app_src_push(&fix->src, frame, TEST_PIXELS * 4, K_SECONDS(1)));
	zassert_ok(k_sem_take(&converted_sem, K_SECONDS(2)), "no converted frame");
	zassert_ok(mpipe_element_set_state(pipe, MPIPE_STATE_READY));

	zassert_equal(converted.size, TEST_YUV420_SIZE, "%u bytes", converted.size);
}

/* The left half of the frame in pure red and the right half in pure blue, in BT.601 */
ZTEST_F(test_vid_convert, test_bgrx32_to_yuv420)
{
	static uint8_t frame[TEST_PIXELS * 4];
	const uint8_t *u_plane = &converted.data[TEST_PIXELS];
	const uint8_t *v_plane = &u_plane[TEST_PIXELS / 4];

	for (uint32_t i = 0; i < TEST_PIXELS; i++) {
		bool red = (i % TEST_WIDTH) < (TEST_WIDTH / 2);

		frame[(4 * i) + 0] = red ? 0 : 255;
		frame[(4 * i) + 1] = 0;
		frame[(4 * i) + 2] = red ? 255 : 0;
		frame[(4 * i) + 3] = 0;
	}

	convert_to_yuv420(fixture, VIDEO_PIX_FMT_BGRX32, frame);

	for (uint32_t i = 0; i < TEST_PIXELS; i++) {
		bool red = (i % TEST_WIDTH) < (TEST_WIDTH / 2);

		zassert_equal(converted.data[i], red ? 82 : 41, "luma %u is %u", i,
			      converted.data[i]);
	}

	for (uint32_t i = 0; i < TEST_PIXELS / 4; i++) {
		bool red = (i % (TEST_WIDTH / 2)) < (TEST_WIDTH / 4);

		zassert_equal(u_plane[i], red ? 90 : 240, "U %u is %u", i, u_plane[i]);
		zassert_equal(v_plane[i], red ? 240 : 110, "V %u is %u", i, v_plane[i]);
	}
}

/* Every pixel with its own luma and chroma: the luma is kept, the chroma is a mean of four */
ZTEST_F(test_vid_convert, test_vuyx32_to_yuv420)
{
	static uint8_t frame[TEST_PIXELS * 4];
	const uint8_t *u_plane = &converted.data[TEST_PIXELS];
	const uint8_t *v_plane = &u_plane[TEST_PIXELS / 4];

	for (uint32_t y = 0; y < TEST_HEIGHT; y++) {
		for (uint32_t x = 0; x < TEST_WIDTH; x++) {
			uint8_t *pixel = &frame[4 * ((y * TEST_WIDTH) + x)];

			pixel[0] = (uint8_t)(200 - (3 * x) - (8 * y));
			pixel[1] = (uint8_t)(64 + (2 * x) + (16 * y));
			pixel[2] = (uint8_t)(16 + x + (8 * y));
			pixel[3] = 0xFF;
		}
	}

	convert_to_yuv420(fixture, VIDEO_PIX_FMT_VUYX32, frame);

	for (uint32_t i = 0; i < TEST_PIXELS; i++) {
		zassert_equal(converted.data[i], frame[(4 * i) + 2], "luma %u is %u", i,
			      converted.data[i]);
	}

	for (uint32_t y = 0; y < TEST_HEIGHT / 2; y++) {
		for (uint32_t x = 0; x < TEST_WIDTH / 2; x++) {
			const uint8_t *top = &frame[4 * ((2 * y * TEST_WIDTH) + (2 * x))];
			const uint8_t *bottom = top + (4 * TEST_WIDTH);
			uint32_t i = (y * (TEST_WIDTH / 2)) + x;
			uint32_t u = (top[1] + top[5] + bottom[1] + bottom[5] + 2) / 4;
			uint32_t v = (top[0] + top[4] + bottom[0] + bottom[4] + 2) / 4;

			zassert_equal(u_plane[i], u, "U %u is %u, not %u", i, u_plane[i], u);
			zassert_equal(v_plane[i], v, "V %u is %u, not %u", i, v_plane[i], v);
		}
	}
}
