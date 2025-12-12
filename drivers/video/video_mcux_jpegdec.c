/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
// #include <zephyr/drivers/clock_control/mcux_clock_control.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/logging/log.h>
#include <fsl_jpegdec.h>
#include "video_ctrls.h"
#include "video_device.h"

#define DT_DRV_COMPAT nxp_mcux_jpegdec

LOG_MODULE_REGISTER(mcux_jpegdec, CONFIG_VIDEO_LOG_LEVEL);

struct video_common_header {
	struct video_format fmt; /* Type, size, pixel format */
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
};

struct video_m2m_common {
	struct video_common_header in; /* Incomming JPEG stream. */
	struct video_common_header out; /* Outgoing decoded image data. */
};

//need at least 2 descriptors' memory allocation as ping-pong buffer strategy
//so the the enqueued in buffer(if any) can be parsed in advance.todo
//context switch or single stream repeat? maybe the latter, #driver_data in video.h
//need to parse header for each time?

struct mcux_jpegdec_data {
	struct video_m2m_common m2m;
	struct k_mutex lock;
	bool is_streaming; /* Whether application set the in/out stream on. */
	bool running; /* Whether decoder is actively decoding. */
	jpegdec_descpt_t decoder_despt;
	/* Whether first incomming frame has been fed. */
	bool first_frame_rcv;
	uint8_t format_idx;
};

struct mcux_jpegdec_config {
	JPEG_DECODER_Type *base;
	void (*irq_config_func)(const struct device *dev);
};

#define MCUX_JPEGDEC_FORMAT_CAP(format)\
	{\
		.pixelformat = (format), .width_min = (64U), .width_max = (0x2000U),\
		.height_min = (64U), .height_max = (0x2000U), .width_step = (16U), .height_step = (16U)\
	}

static const struct video_format_cap mcux_jpegdec_out_all_fmts[] = {
	MCUX_JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_NV12), /* YUV420 format, Y at first plane, UV at second plane */
	MCUX_JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_YUYV), /* Packed YUV422 format */
	MCUX_JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_BGR24), /* 24-bit BGR color format */
	MCUX_JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_YUV24), /* 24-bit 1 planar YUV format */
	MCUX_JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_GREY), /* 8-bit grayscale */
	MCUX_JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_ARGB32), /* YCCK format or any format with 4 color component */
};

/* Empty output format array - dynamically populated based on JPEG header parsing. */
static const struct video_format_cap mcux_jpegdec_out_fmts[2] = {0};

/* JPEG decoder only support JPEG input. */
static const struct video_format_cap mcux_jpegdec_in_fmts[2] = {
	MCUX_JPEGDEC_FORMAT_CAP(VIDEO_PIX_FMT_JPEG), /* JPEG compressed image format */
	{0}
};

struct pixel_map {
	jpegdec_pixel_format_t drv_pixel_format;
	uint32_t vid_pixel_format;
	uint8_t bytes_per_pixel;
};

static const struct pixel_map pixel_map_confs[] = {
	{
		.drv_pixel_format = kJPEGDEC_PixelFormatYUV420,
		.vid_pixel_format = VIDEO_PIX_FMT_NV12,
		.bytes_per_pixel = 1,
	},
	{
		.drv_pixel_format = kJPEGDEC_PixelFormatYUV422,
		.vid_pixel_format = VIDEO_PIX_FMT_YUYV,
		.bytes_per_pixel = 2,
	},
	{
		.drv_pixel_format = kJPEGDEC_PixelFormatRGB,
		.vid_pixel_format = VIDEO_PIX_FMT_BGR24,
		.bytes_per_pixel = 3,
	},
	{
		.drv_pixel_format = kJPEGDEC_PixelFormatYUV444,
		.vid_pixel_format = VIDEO_PIX_FMT_YUV24,
		.bytes_per_pixel = 3,
	},
	{
		.drv_pixel_format = kJPEGDEC_PixelFormatGray,
		.vid_pixel_format = VIDEO_PIX_FMT_GREY,
		.bytes_per_pixel = 1,
	},
	{
		.drv_pixel_format = kJPEGDEC_PixelFormatYCCK,
		.vid_pixel_format = VIDEO_PIX_FMT_ARGB32,
		.bytes_per_pixel = 4,
	},
};

int mcux_jpegdec_get_conf(jpegdec_pixel_format_t drv_pixel_format)
{
	for (size_t i = 0; i < ARRAY_SIZE(pixel_map_confs); i++) {
		if (pixel_map_confs[i].drv_pixel_format == drv_pixel_format) {
			*vid_pixel_format = pixel_map_confs[i].vid_pixel_format;
			return i;
		}
	}

	return -1;
}

static void mcux_jpegdec_decode_one_frame(const struct device *dev)
{
	struct mcux_jpegdec_data *data = dev->data;
	struct k_fifo *in_fifo_in = &data->m2m.in.fifo_in;
	struct k_fifo *out_fifo_in = &data->m2m.out.fifo_in;
	struct video_buffer *current_in;
	struct video_buffer *current_out;
	int ret;

	if (k_fifo_is_empty(in_fifo_in) || k_fifo_is_empty(out_fifo_in)) {
		/* Nothing can be done if either input or output queue is empty */
		data->running = false;
		return;
	}

	current_in = k_fifo_get(in_fifo_in, K_NO_WAIT);
	current_out = k_fifo_get(out_fifo_in, K_NO_WAIT);

	/* Update the input/output address and size. */
	data->decoder_despt.config.jpegBufAddr = current_in->buffer;
	data->decoder_despt.config.jpegBufSize = current_in->size;
	data->decoder_despt.config.outputBufAddr0 = (uint32_t)current_out->buffer;
	/*
	 * For VIDEO_PIX_FMT_NV12, we need a second output buffer for the UV plane.
	 * Its address shall be strictly next to the end of Y plane.
	 */
	if (data->m2m.out.fmt.pixelformat == VIDEO_PIX_FMT_NV12) {
		data->decoder_despt.config.outputBufAddr1 = (uint32_t)current_out->buffer
			+ data->m2m.out.fmt.pitch * data->m2m.out.fmt.height;
	}

    JPEGDEC_EnableSlotNextDescpt(config->base, 0);

	data->running = true;
}

static int mcux_jpegdec_get_fmt(const struct device *dev, struct video_format *fmt)
{
	struct mcux_jpegdec_data *data = dev->data;

	*fmt = fmt->type == VIDEO_BUF_TYPE_INPUT ? data->m2m.in.fmt : data->m2m.out.fmt;

	return 0;
}

static int mcux_jpegdec_set_fmt(const struct device *dev, struct video_format *fmt)
{
	struct mcux_jpegdec_data *data = dev->data;
	bool is_input = (vbuf->type == VIDEO_BUF_TYPE_INPUT) ? true : false;
	int ret = 0;

	/*
	 * The input pixel format can only be VIDEO_PIX_FMT_JPEG, and the output
	 * format cannot be changed since the decoder does not have CSC. The output
	 * format is obtained after parsing the first enqueued input frame buffer.
	 * If the first frame is not received yet, we cannot validate the output format.
	 */
	if (!data->first_frame_rcv) {
		if (fmt->type == VIDEO_BUF_TYPE_OUTPUT) {
			LOG_WRN("Output format cannot be set and verified before first
				input frame is received");
			return -EAGAIN;
		}
	}

	/* Validate the settings */
	if ((fmt->width != data->m2m.in.fmt.width) || (fmt->height != data->m2m.in.fmt.height)) {
		LOG_ERR("The input/output width/height are fixed and cannot be configured.");
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (is_input) {
		if (fmt->pixelformat != data->m2m.in.fmt.pixelformat) {
			LOG_ERR("The input format can only be JPEG.");
			return -EINVAL;
		}
	} else {
		if (fmt->pixelformat != data->m2m.out.fmt.pixelformat) {
			LOG_ERR("JPEG decoder does not support CSC, the output format can not be changed.");
			return -EINVAL;
		}

		if (fmt->pitch < data->m2m.out.fmt.pitch) {
			LOG_ERR("The pitch cannot be smaller than the line size.");
			return -EINVAL;
		} else {
			/* Update pitch value. */
			data->m2m.out.fmt.pitch = fmt->pitch;
		}
	}

	if (data->is_streaming) {
		ret = -EBUSY;
		goto out;
	}

	data->decoder_despt.config.outBufPitch = data->m2m.out.fmt.pitch;

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

static int mcux_jpegdec_set_stream(const struct device *dev, bool enable, enum video_buf_type type)
{
	struct mcux_jpegdec_data *data = dev->data;
	int ret = 0;

	ARG_UNUSED(type);

	k_mutex_lock(&data->lock, K_FOREVER);

	if (enable == data->is_streaming) {
		ret = -EALREADY;
		goto out;
	}

	data->is_streaming = enable;

	/* Start decoding if streaming is on otherwise stop the decoder. */
	if (enable) {
		mcux_jpegdec_decode_one_frame(dev);
	} else {
		data->running = false;
	}

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

static int mcux_jpegdec_enqueue(const struct device *dev, struct video_buffer *vbuf)
{
	struct mcux_jpegdec_data *data = dev->data;
	bool is_input = (vbuf->type == VIDEO_BUF_TYPE_INPUT) ? true : false;
	struct video_common_header *common =
		vbuf->type == VIDEO_BUF_TYPE_INPUT ? &data->m2m.in : &data->m2m.out;

	/*
	 * The decoder cannot perform CSC, which means if the JPEG file
	 * is encoded in YUV420 format, the output will be YUV420 format.
	 * So the output format cannot be configured, output format and
	 * capability can only be got after the first frame of JPEG is
	 * fed to the decoder.
	 */
	if (is_input) {
		/* Check if it is the first frame, if so parse the JPEG header and obtain the frame into. */
		if (!data->first_frame_rcv) {
			/* Feed the input buffer to the decoder for header parsing. */
			data->decoder_despt.config.jpegBufAddr = vbuf->buffer;//todo
			data->decoder_despt.config.jpegBufSize = vbuf->size;//todo
			if (JPEGDEC_ParseHeader(&data->decoder_despt.config) == kStatus_JPEGDEC_NotSupported) {
				LOG_ERR("JPEG format not supported");
				return -ENOTSUP;
			}
	
			/* Set output pixel format, width and height based on parsed JPEG header */
			data->format_idx = mcux_jpegdec_get_conf(data->decoder_despt.config.pixelFormat);
			data->m2m.out.fmt.pixelformat = pixel_map_confs[index].vid_pixel_format;
			data->m2m.out.fmt.width = data->decoder_despt.config.width;
			data->m2m.out.fmt.height = data->decoder_despt.config.height;
			/*
			 * Calculate the default pitch and buffer size. They can be updated later
			 * if application calls set_fmt to update pitch.
			 */
			data->m2m.out.fmt.pitch = pixel_map_confs[index].bytes_per_pixel * data->m2m.out.fmt.width;
			if (data->m2m.out.fmt.pixelformat == VIDEO_PIX_FMT_NV12) {
				/*
				 * For NV12 2-planner format, the Y planner is 1BPP and UV planner is 0.5BPP.
				 * But the same pitch configuration applies to the 2 planners, so double the buffer size.
				 */
				data->m2m.out.fmt.size = data->m2m.out.fmt.pitch * data->m2m.out.fmt.height * 2U;
			} else {
				data->m2m.out.fmt.size = data->m2m.out.fmt.pitch * data->m2m.out.fmt.height;
			}	
			data->decoder_despt.config.outBufPitch = data->m2m.out.fmt.pitch;
	
			/*
			 * The input width/height is the same as output. JPEG files do not have pitch
			 * and the size can be different for each file due to compression ratio, so will not set them.
			 */
			data->m2m.in.fmt.width = data->m2m.out.fmt.width;
			data->m2m.in.fmt.height = data->m2m.out.fmt.height;
	
			data->first_frame_rcv = true;
		}
	} else {
		/* First input frame has been received, the output buffer can be verified and enqueued. */
		if (data->first_frame_rcv) {
			/* Check if the output buffer size is sufficient */
			if (vbuf->size < data->m2m.out.fmt.size) {//todo
				LOG_ERR("Output buffer size is insufficient");
				return -EINVAL;
			}
		} else {
			LOG_WRN("Output buffer cannot be enqueued before first input buffer is received");
			return -EAGAIN;
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	k_fifo_put(&common->fifo_in, vbuf);

	/*
	 * If the streaming is on but the decoder is not running due to lack of in/out buffer,
	 * start to decode new frame.
	 */
	if (!data->running && data->is_streaming) {
		mcux_jpegdec_decode_one_frame(dev);
	}

	k_mutex_unlock(&data->lock);

	return 0;
}

static int mcux_jpegdec_dequeue(const struct device *dev, struct video_buffer **vbuf,
			      k_timeout_t timeout)
{
	struct mcux_jpegdec_data *data = dev->data;
	struct video_common_header *common =
		(*vbuf)->type == VIDEO_BUF_TYPE_INPUT ? &data->m2m.in : &data->m2m.out;

	*vbuf = k_fifo_get(&common->fifo_out, timeout);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

static int mcux_jpegdec_get_caps(const struct device *dev, struct video_caps *caps)//done
{
	if (caps->type == VIDEO_BUF_TYPE_OUTPUT) {
		/* Assign the format capability based on parsed JPEG header. */
		mcux_jpegdec_out_fmts[0] = mcux_jpegdec_out_all_fmts[data->format_idx];
		caps->format_caps = mcux_jpegdec_out_fmts;
	} else {
		caps->format_caps = mcux_jpegdec_in_fmts;
	}

	caps->min_vbuf_count = 1;

	return 0;
}

static DEVICE_API(video, mcux_jpegdec_driver_api) = {
	/* mandatory callbacks */
	.set_format = mcux_jpegdec_set_fmt,
	.get_format = mcux_jpegdec_get_fmt,
	.set_stream = mcux_jpegdec_set_stream, /* Start or stop streaming on the video device */
	.get_caps = mcux_jpegdec_get_caps,
	/* optional callbacks */
	.enqueue = mcux_jpegdec_enqueue, /* Enqueue a buffer in the driver’s incoming queue */
	.dequeue = mcux_jpegdec_dequeue, /* Dequeue a buffer from the driver’s outgoing queue */
	//maybe todo
	.flush = ;
	.set_signal = ;
};

static int mcux_jpegdec_init(const struct device *dev)
{
	const struct mcux_jpegdec_config *config = dev->config;
	struct mcux_jpegdec_data *data = dev->data;
	jpegdec_config_t init_config;
	int ret;

	/* Initialise default input / output formats */
	k_mutex_init(&data->lock);
	k_fifo_init(&data->m2m.in.fifo_in);
	k_fifo_init(&data->m2m.in.fifo_out);
	k_fifo_init(&data->m2m.out.fifo_in);
	k_fifo_init(&data->m2m.out.fifo_out);

	/* The in/out format types */
	data->m2m.in.fmt.type = VIDEO_BUF_TYPE_INPUT;
	data->m2m.out.fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	data->m2m.in.fmt.pixelformat = VIDEO_PIX_FMT_JPEG;

    /* Init JPEG decoder module. */
    JPEGDEC_GetDefaultConfig(&init_config);
    init_config.slots = kJPEGDEC_Slot0; /* Enable only one slot. */
    JPEGDEC_Init(config->base, &init_config);

	JPEGDEC_EnableInterrupts(config->base, 0U, kJPEGDEC_DecodeCompleteFlag | kJPEGDEC_ErrorFlags);

	/* Link the descriptor to itself so no need to set the descriptor address every time. */
	data->decoder_despt.nextDescptAddr = (uint32_t)&(data->decoder_despt);

	/* Assign this descriptor to slot 0. */
	JPEGDEC_SetSlotNextDescpt(config->base, 0, data->decoder_despt);

	/* Run IRQ init */
	config->irq_config_func(dev);

	LOG_DBG("%s initialized", dev->name);

	return 0;
}

static void mcux_jpegdec_isr(const struct device *dev)
{
	const struct mcux_jpegdec_config *config = dev->config;
	struct mcux_jpegdec_data *data = dev->data;
	video_buffer *current_out = k_fifo_get(&data->m2m.out.fifo_in, K_NO_WAIT);

	/* Decode complete. */
    if ((JPEGDEC_GetStatusFlags(config->base, 0) & kJPEGDEC_DecodeCompleteFlag) != 0U)
    {
        JPEGDEC_ClearStatusFlags(config->base, 0, kJPEGDEC_DecodeCompleteFlag);
		current_out->bytesused = data->m2m.out.fmt.size;
    }
	
    /* Error occur, this output bufer is broken, no valid data can be used. */
    if ((JPEGDEC_GetStatusFlags(config->base, 0) & kJPEGDEC_ErrorFlags) != 0U)
    {
		JPEGDEC_ClearStatusFlags(config->base, 0, kJPEGDEC_ErrorFlags);
		current_out->bytesused = 0;
    }

	mcux_jpegdec_decode_one_frame(dev);
}

#define MCUX_JPEGDEC_INIT(n)							\
	static void mcux_jpegdec_irq_config_##n(const struct device *dev)		\
	{									\
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority),		\
			    mcux_jpegdec_isr, DEVICE_DT_INST_GET(n), 0);		\
		irq_enable(DT_INST_IRQN(n));					\
	}									\
	static struct mcux_jpegdec_data mcux_jpegdec_data_##n = {			\
		.decoder_despt.config.clearStreamBuf = true,\
		.decoder_despt.config.autoStart = true,\
	};									\
	static const struct mcux_jpegdec_config mcux_jpegdec_config_##n = {		\
		.base = (JPEG_DECODER_Type *) DT_INST_REG_ADDR(n),\
		.irq_config_func = mcux_jpegdec_irq_config_##n,			\
	};									\
	DEVICE_DT_INST_DEFINE(n, &mcux_jpegdec_init,				\
			      NULL, &mcux_jpegdec_data_##n,			\
			      &mcux_jpegdec_config_##n,				\
			      POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY,		\
			      &mcux_jpegdec_driver_api);				\
	VIDEO_DEVICE_DEFINE(jpegdec_##n, DEVICE_DT_INST_GET(n), NULL);

DT_INST_FOREACH_STATUS_OKAY(MCUX_JPEGDEC_INIT)
