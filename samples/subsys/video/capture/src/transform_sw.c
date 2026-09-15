/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Software colorspace conversion for video sources that do not offer a pixel format the
 * display accepts, on boards without a hardware pixel converter. USB webcams typically
 * expose YUYV and MJPEG only, so convert YUYV to the RGB565 the display drivers take.
 *
 * YUYV and RGB565 are both 16 bits per pixel and every output pixel is written at the
 * offset its input was read from, so the frame is converted in place. That keeps a single
 * frame buffer for the whole pipeline, which matters on parts where one frame is already a
 * large share of the available RAM.
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/video/video.h>

LOG_MODULE_DECLARE(main, CONFIG_LOG_DEFAULT_LEVEL);

/* BT.601 limited range (Y in 16..235) to full range RGB, coefficients scaled by 256 */
#define APP_COEFF_SCALE 256
#define APP_COEFF_Y     298
#define APP_COEFF_R_V   409
#define APP_COEFF_G_U   100
#define APP_COEFF_G_V   208
#define APP_COEFF_B_U   517

/* One YUYV macropixel carries two image pixels over four bytes */
#define APP_YUYV_BYTES_PER_MACROPIXEL 4U

static inline uint16_t app_yuv_to_rgb565(uint8_t y, uint8_t u, uint8_t v)
{
	const int32_t luma = APP_COEFF_Y * ((int32_t)y - 16);
	const int32_t cb = (int32_t)u - 128;
	const int32_t cr = (int32_t)v - 128;
	int32_t r = (luma + APP_COEFF_R_V * cr) / APP_COEFF_SCALE;
	int32_t g = (luma - APP_COEFF_G_U * cb - APP_COEFF_G_V * cr) / APP_COEFF_SCALE;
	int32_t b = (luma + APP_COEFF_B_U * cb) / APP_COEFF_SCALE;

	r = CLAMP(r, 0, UINT8_MAX);
	g = CLAMP(g, 0, UINT8_MAX);
	b = CLAMP(b, 0, UINT8_MAX);

	return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

int app_setup_video_transform(const struct device *const transform_dev,
			      struct video_format *const in_fmt, struct video_format *const out_fmt,
			      struct video_buffer **out_buf)
{
	int ret;

	ARG_UNUSED(transform_dev);
	ARG_UNUSED(out_buf);

	if (in_fmt->pixelformat != VIDEO_PIX_FMT_YUYV) {
		LOG_ERR("Software transform needs a YUYV source, got %s",
			VIDEO_FOURCC_TO_STR(in_fmt->pixelformat));
		return -ENOTSUP;
	}

	*out_fmt = *in_fmt;
	out_fmt->pixelformat = VIDEO_PIX_FMT_RGB565;

	ret = video_estimate_fmt_size(out_fmt);
	if (ret < 0) {
		LOG_ERR("Unable to size the RGB565 output format");
		return ret;
	}

	LOG_INF("Converting YUYV %ux%u to RGB565 in software", out_fmt->width, out_fmt->height);

	return 0;
}

int app_transform_frame(const struct device *const transform_dev, struct video_buffer *in_buf,
			struct video_buffer **out_buf)
{
	uint8_t *const buf = in_buf->buffer;
	uint32_t macropixels;

	ARG_UNUSED(transform_dev);

	/* A frame received incomplete is converted as far as the data goes */
	macropixels = in_buf->bytesused / APP_YUYV_BYTES_PER_MACROPIXEL;

	for (uint32_t i = 0; i < macropixels; i++) {
		const uint32_t off = i * APP_YUYV_BYTES_PER_MACROPIXEL;
		const uint8_t y0 = buf[off];
		const uint8_t u = buf[off + 1];
		const uint8_t y1 = buf[off + 2];
		const uint8_t v = buf[off + 3];

		sys_put_le16(app_yuv_to_rgb565(y0, u, v), &buf[off]);
		sys_put_le16(app_yuv_to_rgb565(y1, u, v), &buf[off + 2]);
	}

	in_buf->bytesused = macropixels * APP_YUYV_BYTES_PER_MACROPIXEL;
	*out_buf = in_buf;

	return 0;
}
