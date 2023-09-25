/*
 * Copyright (c) 2023, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_isi

#include <zephyr/kernel.h>

#include <fsl_isi_v2.h>
#include <fsl_clock.h>

#include <zephyr/drivers/video.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(isi, CONFIG_VIDEO_LOG_LEVEL);

#include <string.h>

#define ISI_MAX_ACTIVE_BUF 2U

/* Map for the fourcc pixelformat to ISI format. */
struct isi_output_format
{
	uint32_t fourcc;
	isi_output_format_t isi_format;
	uint8_t bpp;
};

/* ISI input/camera config */
struct  isi_input_config
{
	uint32_t pixelformat;
	uint32_t width;
	uint32_t height;
	uint8_t bpp;
};

static const struct isi_output_format isi_output_formats[] = {
	{
		.fourcc		= VIDEO_PIX_FMT_RGB24,
		.isi_format	= kISI_OutputRGB888,
		.bpp = 24,
	},
	{
		.fourcc		= VIDEO_PIX_FMT_RGB565,
		.isi_format	= kISI_OutputRGB565,
		.bpp = 16,
	},
	{
		.fourcc		= VIDEO_PIX_FMT_YUYV,
		.isi_format	= kISI_OutputYUV422_1P8P,
		.bpp = 16,
	},
	{
		.fourcc         = VIDEO_PIX_FMT_BGRA,
		.isi_format     = kISI_OutputARGB8888,
		.bpp = 32,
	}
};

static const struct isi_input_config isi_input_parallel =
{
	.pixelformat = VIDEO_PIX_FMT_UYVY,
	.width = 1280,
	.height = 720,
	.bpp = 16,
};

static const struct isi_input_config isi_input_mipi =
{
	.pixelformat = VIDEO_PIX_FMT_UYVY,
	.width = 1280,
	.height = 800,
	.bpp = 16,
};

static const isi_csc_config_t csc_yuv2rgb = {
	.mode = kISI_CscYCbCr2RGB,
	.A1   = 1.164f,
	.A2   = 0.0f,
	.A3   = 1.596f,
	.B1   = 1.164f,
	.B2   = -0.392f,
	.B3   = -0.813f,
	.C1   = 1.164f,
	.C2   = 2.017f,
	.C3   = 0.0f,
	.D1   = -16,
	.D2   = -128,
	.D3   = -128,
};

static const isi_csc_config_t csc_rgb2yuv = {
	.mode = kISI_CscRGB2YCbCr,
	.A1   = 0.257f,
	.A2   = 0.504f,
	.A3   = 0.098f,
	.B1   = -0.148f,
	.B2   = -0.291f,
	.B3   = 0.439f,
	.C1   = 0.439f,
	.C2   = -0.368f,
	.C3   = -0.071f,
	.D1   = 16,
	.D2   = 128,
	.D3   = 128,
};

struct video_mcux_isi_config {
	ISI_Type *base;
	const struct device *source_dev;
	const struct device *media_axi_clk_dev;
	clock_control_subsys_t media_axi_clk_subsys;
	struct _clock_root_config_t media_axi_clk_cfg;
	const struct device *media_apb_clk_dev;
	clock_control_subsys_t media_apb_clk_subsys;
	struct _clock_root_config_t media_apb_clk_cfg;
};

struct video_mcux_isi_data {
	const struct device *dev;
	isi_config_t isi_config;
	uint32_t output_pixelformat;
	uint16_t output_width;
	uint16_t output_height;
	uint8_t output_bpp;
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;

	volatile uint8_t buffer_index;
	volatile bool is_transfer_started;
	uint32_t drop_frame;
	uint32_t active_buffer[ISI_MAX_ACTIVE_BUF];
	uint8_t active_buf_cnt;
	struct video_buffer *active_vbuf[ISI_MAX_ACTIVE_BUF];

	struct k_poll_signal *signal;
};

#ifdef DEBUG
static void dump_isi_regs(ISI_Type *base)
{
	LOG_DBG("CHNL_CTRL[0x0]: 0x%08x", base->CHNL_CTRL);
	LOG_DBG("CHNL_IMG_CTRL[0x4]: 0x%08x", base->CHNL_IMG_CTRL);
	LOG_DBG("CHNL_OUT_BUF_CTRL[0x8]: 0x%08x", base->CHNL_OUT_BUF_CTRL);
	LOG_DBG("CHNL_IMG_CFG[0x10]: 0x%08x", base->CHNL_IMG_CFG);
	LOG_DBG("CHNL_IER[0x10]: 0x%08x", base->CHNL_IER);
	LOG_DBG("CHNL_SCALE_FACTOR[0x18]: 0x%08x", base->CHNL_SCALE_FACTOR);
	LOG_DBG("CHNL_SCALE_OFFSET[0x1C]: 0x%08x", base->CHNL_SCALE_OFFSET);
	LOG_DBG("CHNL_OUT_BUF1_ADDR_Y[0x70]: 0x%08x", base->CHNL_OUT_BUF1_ADDR_Y);
	LOG_DBG("CHNL_OUT_BUF_PITCH[0x7C]: 0x%08x", base->CHNL_OUT_BUF_PITCH);
	LOG_DBG("CHNL_OUT_BUF2_ADDR_Y[0x8C]: 0x%08x", base->CHNL_OUT_BUF2_ADDR_Y);
	LOG_DBG("CHNL_SCL_IMG_CFG[0x98]: 0x%08x", base->CHNL_SCL_IMG_CFG);
}
#else
static void dump_isi_regs(ISI_Type *base) {}
#endif

bool is_yuv(uint32_t pixelformat)
{
	if ((pixelformat == VIDEO_PIX_FMT_YUYV) || (pixelformat == VIDEO_PIX_FMT_UYVY))
		return true;
	else
		return false;
}

bool is_rgb(uint32_t pixelformat)
{
	if ((pixelformat == VIDEO_PIX_FMT_RGB24) || (pixelformat == VIDEO_PIX_FMT_RGB565)
		|| (pixelformat == VIDEO_PIX_FMT_BGRA))

		return true;
	else
		return false;
}

static int get_isi_output_format(uint32_t fourcc)
{
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(isi_output_formats); i++)
	{
		if (isi_output_formats[i].fourcc == fourcc)
		{
			return i;
		}
	}

	LOG_ERR("pixelformat %c%c%c%c not supported",
				(char)fourcc, (char)(fourcc >> 8),
				(char)(fourcc >> 16), (char)(fourcc >> 24));
	return -1;
}

static void __frame_done_handler(const struct device *dev)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;
	enum video_signal_result result = VIDEO_BUF_DONE;
	struct video_buffer *vbuf = NULL;
	uint32_t buffer_addr;
	uint32_t intStatus;

	intStatus = ISI_GetInterruptStatus(config->base);
	ISI_ClearInterruptStatus(config->base, intStatus);

	if ((uint32_t)kISI_FrameReceivedInterrupt != ((uint32_t)kISI_FrameReceivedInterrupt & intStatus))
	{
		return;
	}

	buffer_addr = data->active_buffer[data->buffer_index];

	if (buffer_addr != data->drop_frame)
	{
		vbuf = data->active_vbuf[data->buffer_index];
		vbuf->timestamp = k_uptime_get_32();
		sys_cache_data_flush_and_invd_range(buffer_addr, (size_t)vbuf->bytesused);
		k_fifo_put(&data->fifo_out, vbuf);
	}


	vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	/*
	* If no available input buffer, then the frame will be droped.
	*/
	if (vbuf == NULL) {
		buffer_addr = data->drop_frame;
		LOG_ERR("No available input buffer, drop frame.");
	}
	else {
		buffer_addr = (uint32_t)vbuf->buffer;
		data->active_vbuf[data->buffer_index] = vbuf;
	}

	data->active_buffer[data->buffer_index] = buffer_addr;
	ISI_SetOutputBufferAddr(config->base, data->buffer_index, buffer_addr, 0, 0);
	data->buffer_index ^= 1U;

	if (IS_ENABLED(CONFIG_POLL) && data->signal) {
		k_poll_signal_raise(data->signal, result);
	}

	return;
}

static int video_mcux_isi_set_fmt(const struct device *dev,
				  enum video_endpoint_id ep,
				  struct video_format *fmt)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;
	struct video_format camera_fmt;
	isi_output_format_t isi_format;
	int i;

	i = get_isi_output_format(fmt->pixelformat);
	if (i >= 0) {
		data->output_pixelformat = fmt->pixelformat;
		data->output_width = fmt->width;
		data->output_height = fmt->height;
		data->output_bpp = isi_output_formats[i].bpp;
		isi_format = isi_output_formats[i].isi_format;
	} else {
		return -ENOTSUP;
	}

	memset(&camera_fmt, 0, sizeof(camera_fmt));
	if (!strcmp(config->source_dev->name, "pcsi")) {
		camera_fmt.pixelformat = isi_input_parallel.pixelformat;
		camera_fmt.width = isi_input_parallel.width;
		camera_fmt.height = isi_input_parallel.height;
		camera_fmt.pitch = isi_input_parallel.width * isi_input_parallel.bpp / 8;
	} else {
		camera_fmt.pixelformat = isi_input_mipi.pixelformat;
		camera_fmt.width = isi_input_mipi.width;
		camera_fmt.height = isi_input_mipi.height;
		camera_fmt.pitch = isi_input_mipi.width * isi_input_mipi.bpp / 8;
	}

	if (config->source_dev && video_set_format(config->source_dev, ep, &camera_fmt)) {
		return -EIO;
	}

	LOG_INF("input pixelformat: %c%c%c%c, wxh: %dx%d",
			(char)camera_fmt.pixelformat, (char)(camera_fmt.pixelformat >> 8),
			(char)(camera_fmt.pixelformat >> 16), (char)(camera_fmt.pixelformat >> 24),
			camera_fmt.width, camera_fmt.height);

	LOG_INF("output pixelformat: %c%c%c%c, wxh: %dx%d",
			(char)data->output_pixelformat, (char)(data->output_pixelformat >> 8),
			(char)(data->output_pixelformat >> 16), (char)(data->output_pixelformat >> 24),
			data->output_width, data->output_height);

	if (data->output_width > camera_fmt.width || data->output_height > camera_fmt.height) {
		LOG_ERR("Not support upscale");
		return -ENOTSUP;
	}

	data->isi_config.isChannelBypassed = false;
	data->isi_config.inputWidth = camera_fmt.width;
	data->isi_config.inputHeight = camera_fmt.height;
	data->isi_config.outputFormat = isi_format;
	data->isi_config.outputLinePitchBytes = data->output_width * data->output_bpp / 8;

	ISI_Init(config->base);
	ISI_SetConfig(config->base, &data->isi_config);

	/* no flip, crop, alpha insersion */

	/* down scale */
	ISI_SetScalerConfig(config->base,
					data->isi_config.inputWidth, data->isi_config.inputHeight,
					data->output_width, data->output_height);

	/* color space conversion*/
	ISI_EnableColorSpaceConversion(config->base, false);
	if (is_yuv(data->output_pixelformat)) {
		if (is_rgb(camera_fmt.pixelformat)) {
			ISI_SetColorSpaceConversionConfig(config->base, &csc_rgb2yuv);
			ISI_EnableColorSpaceConversion(config->base, true);
		}
	}
	else if (is_rgb(data->output_pixelformat)) {
		if (is_yuv(camera_fmt.pixelformat)) {
			ISI_SetColorSpaceConversionConfig(config->base, &csc_yuv2rgb);
			ISI_EnableColorSpaceConversion(config->base, true);
		}
	}

	data->buffer_index = 0;
	data->is_transfer_started = false;
	data->active_buf_cnt = 0;

	return 0;
}

static int video_mcux_isi_get_fmt(const struct device *dev,
				  enum video_endpoint_id ep,
				  struct video_format *fmt)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;

	if (config->source_dev && video_get_format(config->source_dev, ep, fmt)) {
		return -EIO;
	}

	fmt->pixelformat = data->output_pixelformat;
	fmt->width = data->output_width;
	fmt->height = data->output_height;
	fmt->pitch = data->isi_config.outputLinePitchBytes;

	return 0;
}

static int video_mcux_isi_stream_start(const struct device *dev)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;
	uint8_t i;
	uint32_t buffer_addr;
	struct video_buffer *vbuf;

	LOG_DBG("enter %s", __func__);

	if (data->active_buf_cnt != 2) {
		LOG_ERR("ISI requires at least two frame buffers");
		return -EIO;
	}

	/* Only support single planar for now */
	for (i = 0; i < ISI_MAX_ACTIVE_BUF; i++) {
		buffer_addr = data->active_buffer[i];
		ISI_SetOutputBufferAddr(config->base, i, buffer_addr, 0, 0);
		vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	}

	data->buffer_index = 0;
	data->is_transfer_started = true;

	ISI_ClearInterruptStatus(config->base, (uint32_t)kISI_FrameReceivedInterrupt);
	ISI_EnableInterrupts(config->base, (uint32_t)kISI_FrameReceivedInterrupt);
	ISI_Start(config->base);
	dump_isi_regs(config->base);

	if (config->source_dev && video_stream_start(config->source_dev)) {
		LOG_ERR("isi source dev start stream failed");
		return -EIO;
	}

	return 0;
}

static int video_mcux_isi_stream_stop(const struct device *dev)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;

	LOG_DBG("enter %s", __func__);

	if (config->source_dev && video_stream_stop(config->source_dev)) {
		LOG_ERR("isi source dev stop stream failed");
		//return -EIO;
	}

	ISI_Stop(config->base);
	ISI_DisableInterrupts(config->base, (uint32_t)kISI_FrameReceivedInterrupt);
	ISI_ClearInterruptStatus(config->base, (uint32_t)kISI_FrameReceivedInterrupt);

	data->is_transfer_started = false;
	data->active_buf_cnt = 0;

	return 0;
}

static int video_mcux_isi_enqueue(const struct device *dev,
				  enum video_endpoint_id ep,
				  struct video_buffer *vbuf)
{
	const struct video_mcux_isi_config *config = dev->config;
	struct video_mcux_isi_data *data = dev->data;
	unsigned int image_size;
	uint32_t interrupts;

	image_size = data->isi_config.outputLinePitchBytes * data->output_height;
	vbuf->bytesused = image_size;

	if (!(data->is_transfer_started)) {
		if (0U == data->drop_frame)
			data->drop_frame = (uint32_t)vbuf->buffer;
		else {
			if (data->active_buf_cnt < ISI_MAX_ACTIVE_BUF) {
				data->active_buffer[data->active_buf_cnt] = (uint32_t)vbuf->buffer;
				data->active_vbuf[data->active_buf_cnt] = vbuf;
				data->active_buf_cnt += 1;
			}
			k_fifo_put(&data->fifo_in, vbuf);
		}
	}
	else {
		/* Disable the interrupt to protect the index information */
		interrupts = ISI_DisableInterrupts(config->base, (uint32_t)kISI_FrameReceivedInterrupt);

		k_fifo_put(&data->fifo_in, vbuf);

		if (0UL != (interrupts & (uint32_t)kISI_FrameReceivedInterrupt))
		{
			ISI_EnableInterrupts(config->base, (uint32_t)kISI_FrameReceivedInterrupt);
		}
	}

	return 0;
}

static int video_mcux_isi_dequeue(const struct device *dev,
				  enum video_endpoint_id ep,
				  struct video_buffer **vbuf,
				  k_timeout_t timeout)
{
	struct video_mcux_isi_data *data = dev->data;

	*vbuf = k_fifo_get(&data->fifo_out, timeout);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

static int video_mcux_isi_get_caps(const struct device *dev,
				  enum video_endpoint_id ep,
				  struct video_caps *caps)
{
	const struct video_mcux_isi_config *config = dev->config;
	const struct video_format_cap *fmt;
	int ret = -ENODEV;

	/* Just forward to input dev for now */
	if (config->source_dev) {
		ret = video_get_caps(config->source_dev, ep, caps);

		fmt = caps->format_caps;
		LOG_INF("pixelformat: %c%c%c%c",
				(char)fmt->pixelformat, (char)(fmt->pixelformat >> 8),
				(char)(fmt->pixelformat >> 16), (char)(fmt->pixelformat >> 24));
	}

	/* NXP ISI request at least 2 buffers before starting */
	caps->min_vbuf_count = 2;

	return ret;
}

#ifdef CONFIG_POLL
static int video_mcux_isi_set_signal(const struct device *dev,
				  enum video_endpoint_id ep,
				  struct k_poll_signal *signal)
{
	struct video_mcux_isi_data *data = dev->data;

	if (data->signal && signal != NULL) {
		return -EALREADY;
	}

	data->signal = signal;

	return 0;
}
#endif

static const struct video_driver_api video_mcux_isi_driver_api = {
	.set_format = video_mcux_isi_set_fmt,
	.get_format = video_mcux_isi_get_fmt,
	.stream_start = video_mcux_isi_stream_start,
	.stream_stop = video_mcux_isi_stream_stop,
	.enqueue = video_mcux_isi_enqueue,
	.dequeue = video_mcux_isi_dequeue,
	.get_caps = video_mcux_isi_get_caps,
#ifdef CONFIG_POLL
	.set_signal = video_mcux_isi_set_signal,
#endif
};

static const struct video_mcux_isi_config video_mcux_isi_config_0 = {
	.base = (ISI_Type *)DT_INST_REG_ADDR(0),
	.source_dev = DEVICE_DT_GET(DT_INST_PHANDLE(0, source)),
	.media_axi_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(0, 0)),
	.media_axi_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(0, 0, name),
	.media_axi_clk_cfg = {
		.clockOff = false,
		.mux = DT_INST_CLOCKS_CELL_BY_IDX(0, 0, mux),
		.div = DT_INST_CLOCKS_CELL_BY_IDX(0, 0, div),
	},
	.media_apb_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(0, 1)),
	.media_apb_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(0, 1, name),
	.media_apb_clk_cfg = {
		.clockOff = false,
		.mux = DT_INST_CLOCKS_CELL_BY_IDX(0, 1, mux),
		.div = DT_INST_CLOCKS_CELL_BY_IDX(0, 1, div),
	},
};

static struct video_mcux_isi_data video_mcux_isi_data_0;

static void video_mcux_isi_isr(const struct device *dev)
{
	__frame_done_handler(dev);
}

static int video_mcux_isi_configure_clock(const struct device *dev)
{
	const struct video_mcux_isi_config *config = dev->config;
	enum clock_control_status clk_status;
	uint32_t clk_freq;
	int ret;

	/* configure media_axi_clk */
	if (!device_is_ready(config->media_axi_clk_dev)) {
		LOG_ERR("media_axi clock control device not ready");
		return -ENODEV;
	}

	clock_control_configure(config->media_axi_clk_dev, config->media_axi_clk_subsys,
			&config->media_axi_clk_cfg);

	clk_status = clock_control_get_status(config->media_axi_clk_dev, config->media_axi_clk_subsys);
	if (clk_status != CLOCK_CONTROL_STATUS_ON) {
		if (clk_status == CLOCK_CONTROL_STATUS_OFF) {
			ret = clock_control_on(config->media_axi_clk_dev, config->media_axi_clk_subsys);
			if (ret) {
				LOG_ERR("media_axi clock can't be enabled");
				return ret;
			}
		}
		else
			return -EINVAL;
	}

	if (clock_control_get_rate(config->media_axi_clk_dev, config->media_axi_clk_subsys, &clk_freq)) {
		return -EINVAL;
	}
	LOG_DBG("media_axi clock frequency %d", clk_freq);

	/* configure media_apb_clk */
	if (!device_is_ready(config->media_apb_clk_dev)) {
		LOG_ERR("media_apb clock control device not ready");
		return -ENODEV;
	}

	clock_control_configure(config->media_apb_clk_dev, config->media_apb_clk_subsys,
			&config->media_apb_clk_cfg);

	clk_status = clock_control_get_status(config->media_apb_clk_dev, config->media_apb_clk_subsys);
	if (clk_status != CLOCK_CONTROL_STATUS_ON) {
		if (clk_status == CLOCK_CONTROL_STATUS_OFF) {
			ret = clock_control_on(config->media_apb_clk_dev, config->media_apb_clk_subsys);
			if (ret) {
				LOG_ERR("media_apb clock can't be enabled");
				return ret;
			}
		}
		else
			return -EINVAL;
	}

	if (clock_control_get_rate(config->media_apb_clk_dev, config->media_apb_clk_subsys, &clk_freq)) {
		return -EINVAL;
	}
	LOG_DBG("media_apb clock frequency %d", clk_freq);

	return 0;
}

static int video_mcux_isi_init_0(const struct device *dev)
{
	struct video_mcux_isi_data *data = dev->data;
	const struct video_mcux_isi_config *config = dev->config;
	int ret;

	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority),
		    video_mcux_isi_isr, DEVICE_DT_INST_GET(0), 0);

	irq_enable(DT_INST_IRQN(0));

	data->dev = dev;

	/* check if there is any input device */
	if (!device_is_ready(config->source_dev)) {
		LOG_ERR("input device %s not ready", config->source_dev->name);
		LOG_ERR("%s init failed", dev->name);
		return -ENODEV;
	}

	ret = video_mcux_isi_configure_clock(dev);
	if (ret) {
		LOG_ERR("%s configure clock failed", dev->name);
		return ret;
	}

	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);

	ISI_GetDefaultConfig(&data->isi_config);


	LOG_INF("%s init succeeded, source from %s", dev->name, config->source_dev->name);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, &video_mcux_isi_init_0,
			NULL, &video_mcux_isi_data_0,
			&video_mcux_isi_config_0,
			POST_KERNEL, CONFIG_VIDEO_MCUX_ISI_INIT_PRIORITY,
			&video_mcux_isi_driver_api);
