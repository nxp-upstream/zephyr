/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/drivers/video.h>
#include <zephyr/video/controls.h>
#include <zephyr/dt-bindings/video/video-interfaces.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "video_common.h"

#include <fsl_pxp.h>
#ifdef CONFIG_HAS_MCUX_CACHE
#include <fsl_cache.h>
#endif

#define DT_DRV_COMPAT nxp_pxp

LOG_MODULE_REGISTER(mcux_pxp, CONFIG_VIDEO_LOG_LEVEL);

struct mcux_pxp_config {
	PXP_Type *base;
	void (*irq_config_func)(void);
};

struct mcux_pxp_ctrls {
	struct {
		struct video_ctrl rotation;
		struct video_ctrl hflip;
		struct video_ctrl vflip;
	};
};

struct pxp_pixfmt_map {
	uint32_t v_pixfmt;
	pxp_ps_pixel_format_t ps_pixfmt;
	pxp_output_pixel_format_t output_pixfmt;
};

static const struct pxp_pixfmt_map pxp_pixfmt_maps[] = {
	/*
	 * PxP's ARGB8888 format corresponds to both VIDEO_PIX_FMT_BGRX32 and
	 * VIDEO_PIX_FMT_BGRA32. Here, we place VIDEO_PIX_FMT_BGRX32 at 1st position just
	 * to favorite this format.
	 */
	{VIDEO_PIX_FMT_BGRX32, kPXP_PsPixelFormatARGB8888, kPXP_OutputPixelFormatARGB8888},
	{VIDEO_PIX_FMT_BGRA32, kPXP_PsPixelFormatARGB8888, kPXP_OutputPixelFormatARGB8888},
	{VIDEO_PIX_FMT_RGB24, -1, kPXP_OutputPixelFormatRGB888},
	{VIDEO_PIX_FMT_RGB565, kPXP_PsPixelFormatRGB565, kPXP_OutputPixelFormatRGB565},
	{VIDEO_PIX_FMT_GREY, kPXP_PsPixelFormatY8, kPXP_OutputPixelFormatY8},
	{VIDEO_PIX_FMT_UYVY, kPXP_PsPixelFormatUYVY1P422, kPXP_OutputPixelFormatUYVY1P422},
	{VIDEO_PIX_FMT_VYUY, kPXP_PsPixelFormatVYUY1P422, kPXP_OutputPixelFormatVYUY1P422},
	{VIDEO_PIX_FMT_NV12, kPXP_PsPixelFormatYUV2P420, kPXP_OutputPixelFormatYUV2P420},
	{VIDEO_PIX_FMT_NV21, kPXP_PsPixelFormatYVU2P420, kPXP_OutputPixelFormatYVU2P420},
	{VIDEO_PIX_FMT_NV16, kPXP_PsPixelFormatYUV2P422, kPXP_OutputPixelFormatYUV2P422},
	{VIDEO_PIX_FMT_NV61, kPXP_PsPixelFormatYVU2P422, kPXP_OutputPixelFormatYVU2P422},
	/* Three planes, whose addresses say which chroma plane comes first: input only */
	{VIDEO_PIX_FMT_YUV420, kPXP_PsPixelFormatYVU420, -1},
};

#define PXP_MIN_LENGTH  1
#define PXP_MAX_LENGTH  4096
#define PXP_STEP_LENGTH 1

#define PXP_FORMAT_CAP(pixfmt)                                                                     \
	{                                                                                          \
		.pixelformat = pixfmt,                                                             \
		.width_min = PXP_MIN_LENGTH,                                                       \
		.width_max = PXP_MAX_LENGTH,                                                       \
		.height_min = PXP_MIN_LENGTH,                                                      \
		.height_max = PXP_MAX_LENGTH,                                                      \
		.width_step = PXP_STEP_LENGTH,                                                     \
		.height_step = PXP_STEP_LENGTH,                                                    \
	}

static const struct video_format_cap pxp_out_caps[] = {
	PXP_FORMAT_CAP(VIDEO_PIX_FMT_BGRX32),
	PXP_FORMAT_CAP(VIDEO_PIX_FMT_BGRA32),
	PXP_FORMAT_CAP(VIDEO_PIX_FMT_RGB565),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_RGB24), currently not working
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_GREY),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_UYVY),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_VYUY),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_NV12),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_NV21),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_NV16),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_NV61),
	{0},
};

static const struct video_format_cap pxp_in_caps[] = {
	PXP_FORMAT_CAP(VIDEO_PIX_FMT_BGRX32),
	PXP_FORMAT_CAP(VIDEO_PIX_FMT_BGRA32),
	PXP_FORMAT_CAP(VIDEO_PIX_FMT_RGB565),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_RGB24), currently not working
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_GREY),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_UYVY),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_VYUY),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_NV12),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_NV21),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_NV16),
	// PXP_FORMAT_CAP(VIDEO_PIX_FMT_NV61),
	PXP_FORMAT_CAP(VIDEO_PIX_FMT_YUV420),
	{0},
};

/*
 * The formats one side reaches from a format of the other side. A format
 * is always converted to itself; a YUV input is also converted to the RGB
 * outputs, by the first colour space converter.
 */
static const uint32_t pxp_yuv_in_pixfmts[] = {
	VIDEO_PIX_FMT_YUV420,
};

static const uint32_t pxp_rgb_out_pixfmts[] = {
	VIDEO_PIX_FMT_RGB565,
	VIDEO_PIX_FMT_BGRX32,
};

struct mcux_pxp_data {
	struct mcux_pxp_ctrls ctrls;
	/* Input format: the pitch as the application sees it, the size in pixels */
	uint32_t ps_fmt_pitch;
	uint32_t ps_fmt_width;
	uint32_t ps_fmt_height;
	pxp_ps_buffer_config_t ps_buffer_cfg;
	pxp_output_buffer_config_t output_buffer_cfg;
	struct k_fifo fifo_in_queued;
	struct k_fifo fifo_in_done;
	struct k_fifo fifo_out_queued;
	struct k_fifo fifo_out_done;
	/* Serializes the job start between the enqueue path and the ISR */
	struct k_spinlock lock;
	bool streaming;
};

static int mcux_pxp_start(const struct device *dev);

static void mcux_pxp_isr(const struct device *const dev)
{
	const struct mcux_pxp_config *config = dev->config;
	struct mcux_pxp_data *data = dev->data;
	struct video_buffer *vbuf_in;
	struct video_buffer *vbuf_out;

	PXP_ClearStatusFlags(config->base, kPXP_CompleteFlag);

	data->streaming = false;

	/* The job that just finished was programmed from the two queue heads */
	vbuf_in = k_fifo_get(&data->fifo_in_queued, K_NO_WAIT);
	vbuf_out = k_fifo_get(&data->fifo_out_queued, K_NO_WAIT);
	if (vbuf_in == NULL || vbuf_out == NULL) {
		LOG_ERR("Completion with no job queued");
		return;
	}

	vbuf_out->timestamp = k_uptime_get_32();
	vbuf_out->bytesused = data->output_buffer_cfg.pitchBytes * data->output_buffer_cfg.height;

#ifdef CONFIG_HAS_MCUX_CACHE
	DCACHE_InvalidateByRange((uint32_t)vbuf_out->buffer, vbuf_out->bytesused);
#endif

	k_fifo_put(&data->fifo_in_done, vbuf_in);
	k_fifo_put(&data->fifo_out_done, vbuf_out);

	/* The next job, when both of its buffers are already queued */
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (!k_fifo_is_empty(&data->fifo_in_queued) && !k_fifo_is_empty(&data->fifo_out_queued)) {
		if (mcux_pxp_start(dev) != 0) {
			LOG_ERR("Failed to start the next job");
		}
	}
	k_spin_unlock(&data->lock, key);
}

static int mcux_pxp_get_caps(const struct device *dev, struct video_caps *caps)
{
	if (caps->type == VIDEO_BUF_TYPE_INPUT) {
		caps->format_caps = pxp_in_caps;
	} else {
		caps->format_caps = pxp_out_caps;
	}

	caps->min_vbuf_count = 1;
	caps->buf_align = 64;

	return 0;
}

static bool mcux_pxp_pixfmt_in(uint32_t pixfmt, const uint32_t *pixfmts, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (pixfmts[i] == pixfmt) {
			return true;
		}
	}

	return false;
}

/*
 * The format at an index among those the other side reaches from a format:
 * the format itself when the other side has it, then the conversions. Returns
 * 0 past the last one.
 */
static uint32_t mcux_pxp_reachable_pixfmt(uint32_t pixfmt, enum video_buf_type type, uint16_t ind)
{
	const struct video_format_cap *other =
		(type == VIDEO_BUF_TYPE_OUTPUT) ? pxp_in_caps : pxp_out_caps;
	const uint32_t *convertible = pxp_rgb_out_pixfmts;
	size_t count = 0;

	for (; other->pixelformat != 0U; other++) {
		if (other->pixelformat == pixfmt) {
			if (ind == 0U) {
				return pixfmt;
			}
			ind--;
			break;
		}
	}

	if (type == VIDEO_BUF_TYPE_OUTPUT) {
		/* From an RGB output back to the YUV inputs */
		if (mcux_pxp_pixfmt_in(pixfmt, pxp_rgb_out_pixfmts,
				       ARRAY_SIZE(pxp_rgb_out_pixfmts))) {
			convertible = pxp_yuv_in_pixfmts;
			count = ARRAY_SIZE(pxp_yuv_in_pixfmts);
		}
	} else if (mcux_pxp_pixfmt_in(pixfmt, pxp_yuv_in_pixfmts, ARRAY_SIZE(pxp_yuv_in_pixfmts))) {
		/* From a YUV input to the RGB outputs */
		count = ARRAY_SIZE(pxp_rgb_out_pixfmts);
	} else {
		/* An RGB input is only converted to itself */
	}

	return (ind < count) ? convertible[ind] : 0U;
}

/*
 * The capabilities the other side reaches from one, in order of preference:
 * for every reachable format, first the frame size the rotation alone gives,
 * which costs nothing, then any frame size, which the scaler gives. The first
 * half of the indices is therefore what the driver offered before it scaled.
 */
static int mcux_pxp_transform_cap(const struct device *const dev,
				  const struct video_format_cap *const cap,
				  struct video_format_cap *const res_cap,
				  enum video_buf_type direction, uint16_t ind)
{
	struct mcux_pxp_data *data = dev->data;
	struct mcux_pxp_ctrls *ctrls = &data->ctrls;
	uint16_t count = 0;
	uint32_t pixfmt;

	while (mcux_pxp_reachable_pixfmt(cap->pixelformat, direction, count) != 0U) {
		count++;
	}

	if (ind >= 2U * count) {
		return -ERANGE;
	}

	pixfmt = mcux_pxp_reachable_pixfmt(cap->pixelformat, direction, ind % count);

	if (ind >= count) {
		/* Scaled: the size on this side says nothing about the other side */
		*res_cap = (struct video_format_cap)PXP_FORMAT_CAP(pixfmt);
		return 0;
	}

	if (ctrls->rotation.val == 0 || ctrls->rotation.val == 180) {
		*res_cap = *cap;
	} else {
		/* For 90 or 270 deg rotation, swap width and height */
		res_cap->width_min = cap->height_min;
		res_cap->width_max = cap->height_max;
		res_cap->width_step = cap->height_step;

		res_cap->height_min = cap->width_min;
		res_cap->height_max = cap->width_max;
		res_cap->height_step = cap->width_step;
	}

	res_cap->pixelformat = pixfmt;
	res_cap->width_min = MAX(res_cap->width_min, PXP_MIN_LENGTH);
	res_cap->height_min = MAX(res_cap->height_min, PXP_MIN_LENGTH);
	res_cap->width_max = MIN(res_cap->width_max, PXP_MAX_LENGTH);
	res_cap->height_max = MIN(res_cap->height_max, PXP_MAX_LENGTH);

	return 0;
}

static int mcux_pxp_get_fmt(const struct device *dev, struct video_format *fmt)
{
	uint8_t i;
	struct mcux_pxp_data *data = dev->data;

	if (fmt->type == VIDEO_BUF_TYPE_INPUT) {
		/* Find the video format that matches the PXP ps format */
		for (i = 0; i < ARRAY_SIZE(pxp_pixfmt_maps); i++) {
			if (pxp_pixfmt_maps[i].ps_pixfmt == data->ps_buffer_cfg.pixelFormat) {
				fmt->pixelformat = pxp_pixfmt_maps[i].v_pixfmt;
				break;
			}
		}
		if (i == ARRAY_SIZE(pxp_pixfmt_maps)) {
			return -EINVAL;
		}

		fmt->pitch = data->ps_fmt_pitch;
		fmt->width = data->ps_fmt_width;
		fmt->height = data->ps_fmt_height;
	} else {
		/* Find the video format that matches the PXP ps format */
		for (i = 0; i < ARRAY_SIZE(pxp_pixfmt_maps); i++) {
			if (pxp_pixfmt_maps[i].output_pixfmt ==
			    data->output_buffer_cfg.pixelFormat) {
				fmt->pixelformat = pxp_pixfmt_maps[i].v_pixfmt;
				break;
			}
		}
		if (i == ARRAY_SIZE(pxp_pixfmt_maps)) {
			return -EINVAL;
		}

		fmt->pitch = data->output_buffer_cfg.pitchBytes;
		fmt->width = data->output_buffer_cfg.width;
		fmt->height = data->output_buffer_cfg.height;
	}

	return video_estimate_fmt_size(fmt);
}

static int mcux_pxp_set_fmt(const struct device *dev, struct video_format *fmt)
{
	uint8_t sz, i;
	const struct video_format_cap *vfc;
	struct mcux_pxp_data *data = dev->data;
	int ret = video_estimate_fmt_size(fmt);

	if (ret < 0) {
		return ret;
	}

	if (fmt->type == VIDEO_BUF_TYPE_INPUT) {
		vfc = pxp_in_caps;
		sz = ARRAY_SIZE(pxp_in_caps);
	} else {
		vfc = pxp_out_caps;
		sz = ARRAY_SIZE(pxp_out_caps);
	}

	for (i = 0; i < sz; i++) {
		if (vfc[i].pixelformat == fmt->pixelformat) {
			break;
		}
	}

	if (i == sz || !IN_RANGE(fmt->width, PXP_MIN_LENGTH, PXP_MAX_LENGTH) ||
	    !IN_RANGE(fmt->height, PXP_MIN_LENGTH, PXP_MAX_LENGTH)) {
		return -ENOTSUP;
	}

	for (i = 0; i < ARRAY_SIZE(pxp_pixfmt_maps); i++) {
		if (pxp_pixfmt_maps[i].v_pixfmt == fmt->pixelformat) {
			break;
		}
	}

	if (i == ARRAY_SIZE(pxp_pixfmt_maps)) {
		return -ENOTSUP;
	}

	if (fmt->type == VIDEO_BUF_TYPE_INPUT) {
		data->ps_buffer_cfg.pixelFormat = pxp_pixfmt_maps[i].ps_pixfmt;
		/* The pitch of a planar format is the one of its luma plane */
		data->ps_buffer_cfg.pitchBytes =
			(fmt->pixelformat == VIDEO_PIX_FMT_YUV420) ? fmt->width : fmt->pitch;
		data->ps_fmt_pitch = fmt->pitch;
		data->ps_fmt_width = fmt->width;
		data->ps_fmt_height = fmt->height;
	} else {
		data->output_buffer_cfg.pixelFormat =
			pxp_pixfmt_maps[i].output_pixfmt;
		data->output_buffer_cfg.pitchBytes = fmt->pitch;
		data->output_buffer_cfg.width = fmt->width;
		data->output_buffer_cfg.height = fmt->height;
	}

	return 0;
}

static int mcux_pxp_set_ctrl(const struct device *dev, uint32_t id)
{
	const struct mcux_pxp_config *const config = dev->config;
	struct mcux_pxp_data *data = dev->data;
	struct mcux_pxp_ctrls *ctrls = &data->ctrls;
	pxp_flip_mode_t flip = ctrls->hflip.val | ctrls->vflip.val;
	pxp_rotate_degree_t rotate = kPXP_Rotate0;

	ARG_UNUSED(id);

	switch (ctrls->rotation.val) {
	case 0:
		rotate = kPXP_Rotate0;
		break;
	case 90:
		rotate = kPXP_Rotate90;
		break;
	case 180:
		rotate = kPXP_Rotate180;
		break;
	case 270:
		rotate = kPXP_Rotate270;
		break;
	}

	/*
	 * The rotation of the output buffer, behind the scaler and the colour
	 * space converter: the rotation of the process surface works with
	 * neither of them.
	 */
	PXP_SetRotateConfig(config->base, kPXP_RotateOutputBuffer, rotate, flip);

	return 0;
}

static int mcux_pxp_set_stream(const struct device *dev, bool enable, enum video_buf_type type)
{
	return 0;
}

/*
 * Program both surfaces from the heads of the two queues and run one job.
 * The addresses are written here, not at enqueue, so several buffers may
 * wait in either queue; the PXP has a single pair of surface registers.
 */
static int mcux_pxp_start(const struct device *dev)
{
	const struct mcux_pxp_config *const config = dev->config;
	struct mcux_pxp_data *data = dev->data;
	struct video_buffer *vbuf_in = k_fifo_peek_head(&data->fifo_in_queued);
	struct video_buffer *vbuf_out = k_fifo_peek_head(&data->fifo_out_queued);
	struct mcux_pxp_ctrls *ctrls = &data->ctrls;
	pxp_output_buffer_config_t output_cfg;
	bool swapped = ctrls->rotation.val == 90 || ctrls->rotation.val == 270;
	uint32_t luma_size = data->ps_fmt_width * data->ps_fmt_height;
	/* The frame the scaler produces, which the rotation then turns into the output */
	uint32_t scaled_width =
		swapped ? data->output_buffer_cfg.height : data->output_buffer_cfg.width;
	uint32_t scaled_height =
		swapped ? data->output_buffer_cfg.width : data->output_buffer_cfg.height;

	if (vbuf_in == NULL || vbuf_out == NULL) {
		return -EINVAL;
	}

	if (data->ps_fmt_width == 0U || data->ps_fmt_height == 0U || scaled_width == 0U ||
	    scaled_height == 0U) {
		return -EINVAL;
	}

	data->ps_buffer_cfg.swapByte = false;
	data->ps_buffer_cfg.bufferAddr = (uint32_t)vbuf_in->buffer;

	if (data->ps_buffer_cfg.pixelFormat == kPXP_PsPixelFormatYVU420) {
		/* VIDEO_PIX_FMT_YUV420: the U plane follows the luma plane, then the V plane */
		data->ps_buffer_cfg.bufferAddrU = (uint32_t)vbuf_in->buffer + luma_size;
		data->ps_buffer_cfg.bufferAddrV =
			(uint32_t)vbuf_in->buffer + luma_size + (luma_size / 4U);
		PXP_SetCsc1Mode(config->base, kPXP_Csc1YCbCr2RGB);
		PXP_EnableCsc1(config->base, true);
	} else {
		data->ps_buffer_cfg.bufferAddrU = 0U;
		data->ps_buffer_cfg.bufferAddrV = 0U;
		PXP_EnableCsc1(config->base, false);
	}

	PXP_SetProcessSurfaceBufferConfig(config->base, &data->ps_buffer_cfg);
	PXP_SetProcessSurfaceScaler(config->base, (uint16_t)data->ps_fmt_width,
				    (uint16_t)data->ps_fmt_height, (uint16_t)scaled_width,
				    (uint16_t)scaled_height);

	/*
	 * The output buffer is described as the rotation finds it, before it
	 * turns the frame: only the pitch is the one of the frame in memory.
	 */
	output_cfg = data->output_buffer_cfg;
	output_cfg.interlacedMode = kPXP_OutputProgressive;
	output_cfg.buffer0Addr = (uint32_t)vbuf_out->buffer;
	output_cfg.buffer1Addr = 0U;
	output_cfg.width = (uint16_t)scaled_width;
	output_cfg.height = (uint16_t)scaled_height;

	PXP_SetOutputBufferConfig(config->base, &output_cfg);
	/* We only support a process surface that covers the full buffer */
	PXP_SetProcessSurfacePosition(config->base, 0U, 0U, (uint16_t)(scaled_width - 1U),
				      (uint16_t)(scaled_height - 1U));

	data->streaming = true;
	PXP_Start(config->base);

	return 0;
}

static int mcux_pxp_enqueue(const struct device *dev, struct video_buffer *vbuf)
{
	struct mcux_pxp_data *data = dev->data;

#ifdef CONFIG_HAS_MCUX_CACHE
	DCACHE_CleanByRange((uint32_t)vbuf->buffer, vbuf->size);
#endif

	if (vbuf->type == VIDEO_BUF_TYPE_INPUT) {
		k_fifo_put(&data->fifo_in_queued, vbuf);
	} else {
		k_fifo_put(&data->fifo_out_queued, vbuf);
	}

	/* One job at a time: start when both queues hold a buffer and none runs */
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	int ret = 0;

	if (!data->streaming && !k_fifo_is_empty(&data->fifo_in_queued) &&
	    !k_fifo_is_empty(&data->fifo_out_queued)) {
		ret = mcux_pxp_start(dev);
	}
	k_spin_unlock(&data->lock, key);

	return ret;
}

static int mcux_pxp_dequeue(const struct device *dev, struct video_buffer **vbuf,
			    k_timeout_t timeout)
{
	struct mcux_pxp_data *data = dev->data;

	switch ((*vbuf)->type) {
	case VIDEO_BUF_TYPE_INPUT:
		*vbuf = k_fifo_get(&data->fifo_in_done, timeout);
		break;
	case VIDEO_BUF_TYPE_OUTPUT:
		*vbuf = k_fifo_get(&data->fifo_out_done, timeout);
		break;
	default:
		return -ENOTSUP;
	}

	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

/*
 * Give back the buffers that wait for a job: move them to the queues that
 * video_dequeue() reads. A buffer left in a queue of the driver after the
 * stream stops would be used again by the next stream, after the application
 * has freed it.
 */
static int mcux_pxp_flush(const struct device *dev, bool cancel)
{
	struct mcux_pxp_data *data = dev->data;
	struct video_buffer *vbuf;

	if (!cancel) {
		/* Wait until every pair of queued buffers has been processed */
		while (data->streaming || (!k_fifo_is_empty(&data->fifo_in_queued) &&
					   !k_fifo_is_empty(&data->fifo_out_queued))) {
			k_sleep(K_MSEC(1));
		}

		return 0;
	}

	/* The interrupt of the running job takes the heads of the two queues */
	while (data->streaming) {
		k_sleep(K_MSEC(1));
	}

	while ((vbuf = k_fifo_get(&data->fifo_in_queued, K_NO_WAIT)) != NULL) {
		k_fifo_put(&data->fifo_in_done, vbuf);
	}

	while ((vbuf = k_fifo_get(&data->fifo_out_queued, K_NO_WAIT)) != NULL) {
		k_fifo_put(&data->fifo_out_done, vbuf);
	}

	return 0;
}

static DEVICE_API(video, mcux_pxp_driver_api) = {
	.get_caps = mcux_pxp_get_caps,
	.transform_cap = mcux_pxp_transform_cap,
	.get_format = mcux_pxp_get_fmt,
	.set_format = mcux_pxp_set_fmt,
	.set_ctrl = mcux_pxp_set_ctrl,
	.set_stream = mcux_pxp_set_stream,
	.flush = mcux_pxp_flush,
	.enqueue = mcux_pxp_enqueue,
	.dequeue = mcux_pxp_dequeue,
};

static int mcux_pxp_init(const struct device *const dev)
{
	const struct mcux_pxp_config *const config = dev->config;
	struct mcux_pxp_data *data = dev->data;
	struct mcux_pxp_ctrls *ctrls = &data->ctrls;
	int ret;

	k_fifo_init(&data->fifo_in_queued);
	k_fifo_init(&data->fifo_out_queued);
	k_fifo_init(&data->fifo_in_done);
	k_fifo_init(&data->fifo_out_done);

	PXP_Init(config->base);

	PXP_SetProcessSurfaceBackGroundColor(config->base, 0U);
	/* Disable alpha surface and CSC1 */
	PXP_SetAlphaSurfacePosition(config->base, 0xFFFFU, 0xFFFFU, 0U, 0U);
	PXP_EnableCsc1(config->base, false);
	PXP_EnableInterrupts(config->base, kPXP_CompleteInterruptEnable);

	config->irq_config_func();

	ret = video_init_ctrl(
		&ctrls->rotation, dev, VIDEO_CID_ROTATE,
		(struct video_ctrl_range){.min = 0, .max = 270, .step = 90, .def = 0});
	if (ret) {
		return ret;
	}

	ret = video_init_ctrl(&ctrls->hflip, dev, VIDEO_CID_HFLIP,
			      (struct video_ctrl_range){.min = 0, .max = 1, .step = 1, .def = 0});
	if (ret) {
		return ret;
	}

	ret = video_init_ctrl(&ctrls->vflip, dev, VIDEO_CID_VFLIP,
			      (struct video_ctrl_range){.min = 0, .max = 1, .step = 1, .def = 0});
	if (ret) {
		return ret;
	}

	/* The cluster is in manual mode while the rotation is 0 */
	return video_auto_cluster_ctrl(&ctrls->rotation, 3, 0, false);
}

#define MCUX_PXP_INIT(inst)                                                                        \
	static void mcux_pxp_irq_config_##inst(void)                                               \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority), mcux_pxp_isr,         \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQN(inst));                                                    \
	}                                                                                          \
                                                                                                   \
	static const struct mcux_pxp_config mcux_pxp_config_##inst = {                             \
		.base = (PXP_Type *)DT_INST_REG_ADDR(inst),                                        \
		.irq_config_func = mcux_pxp_irq_config_##inst,                                     \
	};                                                                                         \
                                                                                                   \
	static struct mcux_pxp_data mcux_pxp_data_##inst;                                          \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &mcux_pxp_init, NULL, &mcux_pxp_data_##inst,                   \
			      &mcux_pxp_config_##inst, POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY,    \
			      &mcux_pxp_driver_api);                                               \
                                                                                                   \
	VIDEO_DEVICE_DEFINE(pxp_##inst, DEVICE_DT_INST_GET(inst), NULL);

DT_INST_FOREACH_STATUS_OKAY(MCUX_PXP_INIT)
