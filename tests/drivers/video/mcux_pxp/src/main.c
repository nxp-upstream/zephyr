/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/video/controls.h>
#include <zephyr/video/video.h>
#include <zephyr/ztest.h>

#define IN_WIDTH  320U
#define IN_HEIGHT 240U

/* The PXP and the display read the buffers by DMA */
#define BUFFER_ALIGN 64U

/* How far a colour component may be from the expected one, out of 255 */
#define COLOR_TOLERANCE 24

/* The largest output frame of the suite: 720x960 RGB565 */
#define OUT_SIZE_MAX (3U * IN_HEIGHT * 3U * IN_WIDTH * 2U)

static const struct device *const pxp = DEVICE_DT_GET(DT_CHOSEN(zephyr_videotrans));

/* One input and one output buffer serve every conversion */
static struct video_buffer *in_buf;
static struct video_buffer *out_buf;

/* The four quadrants of the input frame, clockwise from the top left one */
enum quadrant {
	TOP_LEFT,
	TOP_RIGHT,
	BOTTOM_RIGHT,
	BOTTOM_LEFT,
	QUADRANT_NUM,
};

struct color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	/* BT.601, limited range */
	uint8_t y;
	uint8_t u;
	uint8_t v;
};

/* One colour per quadrant, so the orientation of the output shows */
static const struct color colors[QUADRANT_NUM] = {
	[TOP_LEFT] = {.r = 255, .g = 0, .b = 0, .y = 81, .u = 90, .v = 240},
	[TOP_RIGHT] = {.r = 0, .g = 255, .b = 0, .y = 145, .u = 54, .v = 34},
	[BOTTOM_RIGHT] = {.r = 255, .g = 255, .b = 255, .y = 235, .u = 128, .v = 128},
	[BOTTOM_LEFT] = {.r = 0, .g = 0, .b = 255, .y = 41, .u = 240, .v = 110},
};

static enum quadrant quadrant_at(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	if (y < height / 2U) {
		return (x < width / 2U) ? TOP_LEFT : TOP_RIGHT;
	}

	return (x < width / 2U) ? BOTTOM_LEFT : BOTTOM_RIGHT;
}

static void fill_input(uint8_t *frame, uint32_t pixfmt)
{
	uint8_t *u_plane = frame + (IN_WIDTH * IN_HEIGHT);
	uint8_t *v_plane = u_plane + ((IN_WIDTH / 2U) * (IN_HEIGHT / 2U));

	for (uint32_t y = 0; y < IN_HEIGHT; y++) {
		for (uint32_t x = 0; x < IN_WIDTH; x++) {
			const struct color *color = &colors[quadrant_at(x, y, IN_WIDTH, IN_HEIGHT)];

			if (pixfmt == VIDEO_PIX_FMT_BGRX32) {
				uint8_t *pixel = &frame[4U * ((y * IN_WIDTH) + x)];

				pixel[0] = color->b;
				pixel[1] = color->g;
				pixel[2] = color->r;
				pixel[3] = 0xFF;
				continue;
			}

			frame[(y * IN_WIDTH) + x] = color->y;
			u_plane[((y / 2U) * (IN_WIDTH / 2U)) + (x / 2U)] = color->u;
			v_plane[((y / 2U) * (IN_WIDTH / 2U)) + (x / 2U)] = color->v;
		}
	}
}

static void read_output(const uint8_t *frame, uint32_t pixfmt, uint32_t width, uint32_t x,
			uint32_t y, struct color *color)
{
	if (pixfmt == VIDEO_PIX_FMT_BGRX32) {
		const uint8_t *pixel = &frame[4U * ((y * width) + x)];

		color->b = pixel[0];
		color->g = pixel[1];
		color->r = pixel[2];
	} else {
		uint16_t pixel = sys_get_le16(&frame[2U * ((y * width) + x)]);

		color->r = (uint8_t)(((pixel >> 11) & 0x1FU) * 255U / 31U);
		color->g = (uint8_t)(((pixel >> 5) & 0x3FU) * 255U / 63U);
		color->b = (uint8_t)((pixel & 0x1FU) * 255U / 31U);
	}
}

/* The quadrant of the input whose colour the output has at a position, QUADRANT_NUM if none */
static enum quadrant quadrant_seen(const uint8_t *frame, uint32_t pixfmt, uint32_t width,
				   uint32_t x, uint32_t y)
{
	struct color seen;

	read_output(frame, pixfmt, width, x, y, &seen);

	for (enum quadrant q = TOP_LEFT; q < QUADRANT_NUM; q++) {
		if (abs((int)seen.r - (int)colors[q].r) <= COLOR_TOLERANCE &&
		    abs((int)seen.g - (int)colors[q].g) <= COLOR_TOLERANCE &&
		    abs((int)seen.b - (int)colors[q].b) <= COLOR_TOLERANCE) {
			return q;
		}
	}

	TC_PRINT("no quadrant has the colour %u %u %u at %u,%u\n", seen.r, seen.g, seen.b, x, y);

	return QUADRANT_NUM;
}

/*
 * Convert one frame and check the colour in the middle of each quadrant of
 * the output: the quadrant of the input found there must be the expected one
 * turned by a multiple of a quarter turn, the same multiple for the four of
 * them. The multiple is returned, so that the caller tells the rotations
 * apart; it is 0 for a frame that is not rotated.
 */
static uint32_t convert(uint32_t in_pixfmt, uint32_t out_pixfmt, uint32_t out_width,
			uint32_t out_height, int rotation)
{
	struct video_format in_fmt = {
		.type = VIDEO_BUF_TYPE_INPUT,
		.pixelformat = in_pixfmt,
		.width = IN_WIDTH,
		.height = IN_HEIGHT,
	};
	struct video_format out_fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = out_pixfmt,
		.width = out_width,
		.height = out_height,
	};
	struct video_control ctrl = {.id = VIDEO_CID_ROTATE, .val = rotation};
	struct video_buffer *done = &(struct video_buffer){.type = VIDEO_BUF_TYPE_OUTPUT};
	struct video_buffer *done_in = &(struct video_buffer){.type = VIDEO_BUF_TYPE_INPUT};
	uint32_t turns = QUADRANT_NUM;
	bool ok = true;

	zassert_ok(video_set_ctrl(pxp, &ctrl));
	zassert_ok(video_set_format(pxp, &in_fmt));
	zassert_ok(video_set_format(pxp, &out_fmt));

	zassert_true(out_fmt.size <= OUT_SIZE_MAX);

	fill_input(in_buf->buffer, in_pixfmt);
	memset(out_buf->buffer, 0x5A, out_fmt.size);
	in_buf->type = VIDEO_BUF_TYPE_INPUT;
	in_buf->bytesused = in_fmt.size;
	out_buf->type = VIDEO_BUF_TYPE_OUTPUT;

	zassert_ok(video_enqueue(pxp, out_buf));
	zassert_ok(video_enqueue(pxp, in_buf));
	zassert_ok(video_dequeue(pxp, &done, K_SECONDS(1)), "no output frame");
	zassert_ok(video_dequeue(pxp, &done_in, K_SECONDS(1)),
		   "the input buffer did not come back");
	zassert_equal_ptr(done, out_buf);
	zassert_equal(done->bytesused, out_fmt.size, "%u bytes for a frame of %u", done->bytesused,
		      out_fmt.size);

	TC_PRINT("%c%c%c%c %ux%u -> %c%c%c%c %ux%u, %d degrees\n", (char)in_pixfmt,
		 (char)(in_pixfmt >> 8), (char)(in_pixfmt >> 16), (char)(in_pixfmt >> 24), IN_WIDTH,
		 IN_HEIGHT, (char)out_pixfmt, (char)(out_pixfmt >> 8), (char)(out_pixfmt >> 16),
		 (char)(out_pixfmt >> 24), out_width, out_height, rotation);

	for (enum quadrant q = TOP_LEFT; q < QUADRANT_NUM; q++) {
		/* The middle of the quadrant, in the output frame */
		uint32_t x =
			(q == TOP_LEFT || q == BOTTOM_LEFT) ? out_width / 4U : 3U * out_width / 4U;
		uint32_t y =
			(q == TOP_LEFT || q == TOP_RIGHT) ? out_height / 4U : 3U * out_height / 4U;
		enum quadrant seen = quadrant_seen(done->buffer, out_pixfmt, out_width, x, y);
		struct color color;

		read_output(done->buffer, out_pixfmt, out_width, x, y, &color);
		TC_PRINT("  quadrant %d: %u %u %u\n", q, color.r, color.g, color.b);

		if (seen >= QUADRANT_NUM) {
			ok = false;
			continue;
		}

		/* The input quadrant found here is this one, some quarter turns back */
		if (turns == QUADRANT_NUM) {
			turns = (q + QUADRANT_NUM - seen) % QUADRANT_NUM;
		}
		if ((seen + turns) % QUADRANT_NUM != q) {
			ok = false;
		}
	}

	zassert_true(ok, "the quadrants of the output are not those of the input, turned");

	return turns;
}

static void *pxp_setup(void)
{
	zassert_true(device_is_ready(pxp));

	in_buf = video_buffer_aligned_alloc(IN_WIDTH * IN_HEIGHT * 4U, BUFFER_ALIGN, K_NO_WAIT);
	out_buf = video_buffer_aligned_alloc(OUT_SIZE_MAX, BUFFER_ALIGN, K_NO_WAIT);
	zassert_not_null(in_buf);
	zassert_not_null(out_buf);

	return NULL;
}

ZTEST_SUITE(mcux_pxp, NULL, pxp_setup, NULL, NULL, NULL);

/* What the driver did before it converted and scaled: a copy and a rotation */
ZTEST(mcux_pxp, test_same_format)
{
	zassert_equal(convert(VIDEO_PIX_FMT_BGRX32, VIDEO_PIX_FMT_BGRX32, IN_WIDTH, IN_HEIGHT, 0),
		      0);
	zassert_not_equal(
		convert(VIDEO_PIX_FMT_BGRX32, VIDEO_PIX_FMT_BGRX32, IN_HEIGHT, IN_WIDTH, 90), 0);
}

/* Planar YUV 4:2:0 to RGB, at the same size */
ZTEST(mcux_pxp, test_yuv_to_rgb)
{
	zassert_equal(convert(VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_RGB565, IN_WIDTH, IN_HEIGHT, 0),
		      0);
	zassert_equal(convert(VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_BGRX32, IN_WIDTH, IN_HEIGHT, 0),
		      0);
}

/* Larger and smaller than the input */
ZTEST(mcux_pxp, test_scale)
{
	zassert_equal(convert(VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_RGB565, 2U * IN_WIDTH,
			      2U * IN_HEIGHT, 0),
		      0);
	zassert_equal(convert(VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_RGB565, IN_WIDTH / 2U,
			      IN_HEIGHT / 2U, 0),
		      0);
	zassert_equal(convert(VIDEO_PIX_FMT_BGRX32, VIDEO_PIX_FMT_BGRX32, 2U * IN_WIDTH,
			      2U * IN_HEIGHT, 0),
		      0);
}

/* The rotation with one of the two other functions */
ZTEST(mcux_pxp, test_rotate_and_one)
{
	zassert_not_equal(
		convert(VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_RGB565, IN_HEIGHT, IN_WIDTH, 90), 0);
	zassert_not_equal(convert(VIDEO_PIX_FMT_BGRX32, VIDEO_PIX_FMT_BGRX32, 2U * IN_HEIGHT,
				  2U * IN_WIDTH, 90),
			  0);
}

/* Everything at once: a decoded 320x240 frame fills a 720x1280 portrait panel */
ZTEST(mcux_pxp, test_convert_scale_rotate)
{

	uint32_t quarter = convert(VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_RGB565, 3U * IN_HEIGHT,
				   3U * IN_WIDTH, 90);
	uint32_t three_quarters = convert(VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_RGB565,
					  3U * IN_HEIGHT, 3U * IN_WIDTH, 270);

	zassert_true(quarter == 1U || quarter == 3U, "90 degrees turned the frame %u times",
		     quarter);
	zassert_equal(three_quarters, QUADRANT_NUM - quarter,
		      "270 degrees turned the frame %u times, 90 degrees %u times", three_quarters,
		      quarter);
	zassert_equal(convert(VIDEO_PIX_FMT_YUV420, VIDEO_PIX_FMT_RGB565, 2U * IN_WIDTH,
			      2U * IN_HEIGHT, 180),
		      2U);
}

/* The capabilities one side reaches from the other */
ZTEST(mcux_pxp, test_transform_cap)
{
	const struct video_format_cap yuv = {
		.pixelformat = VIDEO_PIX_FMT_YUV420,
		.width_min = IN_WIDTH,
		.width_max = IN_WIDTH,
		.height_min = IN_HEIGHT,
		.height_max = IN_HEIGHT,
	};
	struct video_control ctrl = {.id = VIDEO_CID_ROTATE, .val = 0};
	struct video_format_cap res;

	zassert_ok(video_set_ctrl(pxp, &ctrl));

	/* The same size first, for each of the two RGB formats */
	zassert_ok(video_transform_cap(pxp, &yuv, &res, VIDEO_BUF_TYPE_INPUT, 0));
	zassert_equal(res.pixelformat, VIDEO_PIX_FMT_RGB565);
	zassert_equal(res.width_min, IN_WIDTH);
	zassert_equal(res.width_max, IN_WIDTH);
	zassert_ok(video_transform_cap(pxp, &yuv, &res, VIDEO_BUF_TYPE_INPUT, 1));
	zassert_equal(res.pixelformat, VIDEO_PIX_FMT_BGRX32);
	zassert_equal(res.height_max, IN_HEIGHT);

	/* Then any size */
	zassert_ok(video_transform_cap(pxp, &yuv, &res, VIDEO_BUF_TYPE_INPUT, 2));
	zassert_equal(res.pixelformat, VIDEO_PIX_FMT_RGB565);
	zassert_true(res.width_max > IN_WIDTH && res.width_min < IN_WIDTH);
	zassert_ok(video_transform_cap(pxp, &yuv, &res, VIDEO_BUF_TYPE_INPUT, 3));
	zassert_equal(res.pixelformat, VIDEO_PIX_FMT_BGRX32);
	zassert_not_ok(video_transform_cap(pxp, &yuv, &res, VIDEO_BUF_TYPE_INPUT, 4));
}
