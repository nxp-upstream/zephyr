/*
 * Copyright (c) 2019, Linaro Limited
 * Copyright 2024, 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_csi

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/video.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/video/video.h>

#include <fsl_csi.h>

#ifdef CONFIG_HAS_MCUX_CACHE
#include <fsl_cache.h>
#endif

/*
 * On RT266x the CSI lives in the MEDIA subsystem, which comes out of boot held
 * in reset with its clock gated. The MIPI CSI-2 receiver is also not wired to
 * the parallel CSI: camera data reaches memory only through
 * CSI2RX -> REFORMATTER -> ISI. With CSI_USE_ISI this driver keeps its video
 * API but captures with ISI channel 0 into the application buffers.
 */
#if defined(CONFIG_SOC_SERIES_IMXRT266X)
#include <fsl_clock.h>
#include <fsl_reset.h>
#include <zephyr/cache.h>
#define CSI_USE_ISI 1
#endif

#include "video_common.h"

struct video_mcux_csi_config {
	CSI_Type *base;
	const struct device *source_dev;
	const struct pinctrl_dev_config *pincfg;
};

struct video_mcux_csi_data {
	const struct device *dev;
	csi_config_t csi_config;
	csi_handle_t csi_handle;
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
	struct k_poll_signal *sig;
#if defined(CSI_USE_ISI)
	struct k_fifo isi_free;           /* queued buffers not yet in the ISI */
	struct video_buffer *isi_slot[2]; /* buffers loaded in ISI BUF1 / BUF2 */
	uint8_t isi_idx;                  /* slot the ISI is currently filling */
#endif
};

static void __frame_done_cb(CSI_Type *base, csi_handle_t *handle, status_t status, void *user_data)
{
	struct video_mcux_csi_data *data = user_data;
	const struct device *dev = data->dev;
	const struct video_mcux_csi_config *config = dev->config;
	enum video_signal_result result = VIDEO_BUF_DONE;
	struct video_buffer *vbuf, *vbuf_first = NULL;
	uint32_t buffer_addr;

	/* IRQ context */

	if (status != kStatus_CSI_FrameDone) {
		return;
	}

	status = CSI_TransferGetFullBuffer(config->base, &(data->csi_handle), &buffer_addr);
	if (status != kStatus_Success) {
		result = VIDEO_BUF_ERROR;
		goto done;
	}

	/* Get matching vbuf by addr */
	while ((vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT))) {
		if ((uint32_t)vbuf->buffer == buffer_addr) {
			break;
		}

		/* should never happen on ordered stream, except on capture
		 * start/restart, requeue the frame and continue looking for
		 * the right buffer.
		 */
		k_fifo_put(&data->fifo_in, vbuf);

		/* prevent infinite loop */
		if (vbuf_first == NULL) {
			vbuf_first = vbuf;
		} else if (vbuf_first == vbuf) {
			vbuf = NULL;
			break;
		}
	}

	if (vbuf == NULL) {
		result = VIDEO_BUF_ERROR;
		goto done;
	}

	vbuf->timestamp = k_uptime_get_32();

#ifdef CONFIG_HAS_MCUX_CACHE
	DCACHE_InvalidateByRange(buffer_addr, vbuf->bytesused);
#endif

	k_fifo_put(&data->fifo_out, vbuf);

done:
	/* Trigger Event */
	if (IS_ENABLED(CONFIG_POLL) && data->sig) {
		k_poll_signal_raise(data->sig, result);
	}

	return;
}

#if defined(CONFIG_VIDEO_MCUX_MIPI_CSI2RX)
/*
 * 4-byte YUV format at the output of the capture path. The RT11xx CSI stores
 * bytes X, Y, U, V (XYUV32); the RT266x ISI stores bytes Y, U, V, X (YUVX32).
 */
#if defined(CSI_USE_ISI)
#define CSI_PIX_FMT_YUV32 VIDEO_PIX_FMT_YUVX32
#else
#define CSI_PIX_FMT_YUV32 VIDEO_PIX_FMT_XYUV32
#endif

K_HEAP_DEFINE(csi_heap, 1000);
static struct video_format_cap *fmts;
/*
 * On i.MX RT11xx SoCs which have MIPI CSI-2 Rx, image data from the camera sensor after passing
 * through the pipeline (MIPI CSI-2 Rx --> Video Mux --> CSI) will be implicitly converted to a
 * 32-bits pixel format. For example, an input in RGB565 or YUYV (2-bytes format) will become a
 * BGRX32 or a 4-byte YUV format (CSI_PIX_FMT_YUV32) respectively, at the output of
 * the CSI.
 */
static inline void video_pix_fmt_convert(struct video_format *fmt, bool isGetFmt)
{
	switch (fmt->pixelformat) {
	case VIDEO_PIX_FMT_BGRX32:
		fmt->pixelformat = isGetFmt ? VIDEO_PIX_FMT_BGRX32 : VIDEO_PIX_FMT_RGB565;
		break;
	case CSI_PIX_FMT_YUV32:
		fmt->pixelformat = isGetFmt ? CSI_PIX_FMT_YUV32 : VIDEO_PIX_FMT_YUYV;
		break;
	case VIDEO_PIX_FMT_RGB565:
		fmt->pixelformat = isGetFmt ? VIDEO_PIX_FMT_BGRX32 : VIDEO_PIX_FMT_RGB565;
		break;
	case VIDEO_PIX_FMT_YUYV:
		fmt->pixelformat = isGetFmt ? CSI_PIX_FMT_YUV32 : VIDEO_PIX_FMT_YUYV;
		break;
	}
}
#endif

#if defined(CSI_USE_ISI)

#define CSI_ISI_IRQN MEDIA_ISI_IRQn

/* ISI CHNL_IMG_CTRL[FORMAT] codes. */
#define CSI_ISI_FMT_XRGB888    0x05U /* bytes B,G,R,X = BGRX32 */
#define CSI_ISI_FMT_YUV444_1P8 0x13U /* bytes Y,U,V,X */

static ISI_Type *const csi_isi = (ISI_Type *)MEDIA__ISI_BASE;
static const struct device *csi_isi_dev;

static void csi_isi_load(uint8_t idx, struct video_buffer *vbuf)
{
	uint32_t addr = (uint32_t)(uintptr_t)vbuf->buffer;

	if (idx == 0U) {
		csi_isi->CHNL[0].OUT_BUF1_ADDR_Y = addr;
		csi_isi->CHNL[0].OUT_BUF1_ADDR_U = 0U;
		csi_isi->CHNL[0].OUT_BUF1_ADDR_V = 0U;
		csi_isi->CHNL[0].OUT_BUF_CTRL ^= ISI_OUT_BUF_CTRL_LOAD_BUF1_ADDR_MASK;
	} else {
		csi_isi->CHNL[0].OUT_BUF2_ADDR_Y = addr;
		csi_isi->CHNL[0].OUT_BUF2_ADDR_U = 0U;
		csi_isi->CHNL[0].OUT_BUF2_ADDR_V = 0U;
		csi_isi->CHNL[0].OUT_BUF_CTRL ^= ISI_OUT_BUF_CTRL_LOAD_BUF2_ADDR_MASK;
	}
}

/*
 * The ISI alternates between BUF1 and BUF2. When a frame is stored, the slot
 * that just completed is handed to the application and refilled with the next
 * free buffer. With no free buffer the slot is parked on the other slot's
 * buffer so capture keeps running; a buffer shared by both slots is never
 * delivered while the other slot can still write into it.
 */
static void csi_isi_isr(const void *arg)
{
	ARG_UNUSED(arg);
	struct video_mcux_csi_data *data = csi_isi_dev->data;
	uint32_t sts = csi_isi->CHNL[0].STS;
	struct video_buffer *done, *next, *other;
	uint8_t idx;

	csi_isi->CHNL[0].STS = sts; /* W1C */

	if ((sts & ISI_STS_FRM_STRD_MASK) == 0U) {
		return;
	}

	idx = data->isi_idx;
	done = data->isi_slot[idx];
	other = data->isi_slot[idx ^ 1U];
	next = k_fifo_get(&data->isi_free, K_NO_WAIT);

	if (next != NULL) {
		data->isi_slot[idx] = next;
		csi_isi_load(idx, next);
	} else if ((done != NULL) && (done != other) && (other != NULL)) {
		data->isi_slot[idx] = other;
		csi_isi_load(idx, other);
	} else {
		done = NULL;
	}

	if ((done != NULL) && (done != other)) {
		done->timestamp = k_uptime_get_32();
		sys_cache_data_invd_range(done->buffer, done->bytesused);
		k_fifo_put(&data->fifo_out, done);
		if (IS_ENABLED(CONFIG_POLL) && data->sig) {
			k_poll_signal_raise(data->sig, VIDEO_BUF_DONE);
		}
	}

	data->isi_idx ^= 1U;
}

static int csi_isi_set_fmt(const struct device *dev, struct video_format *fmt,
			   struct video_format *src_fmt)
{
	const struct video_mcux_csi_config *config = dev->config;
	struct video_mcux_csi_data *data = dev->data;
	uint32_t pitch = fmt->width * 4U;
	uint32_t code = (fmt->pixelformat == VIDEO_PIX_FMT_BGRX32) ? CSI_ISI_FMT_XRGB888
								   : CSI_ISI_FMT_YUV444_1P8;

	data->csi_config.bytesPerPixel = 4U;
	data->csi_config.linePitch_Bytes = pitch;
	data->csi_config.width = fmt->width;
	data->csi_config.height = fmt->height;

	RESET_PeripheralReset((reset_ip_name_t)kModCon_MEDIA_ISI);
	CLOCK_EnableClock(kCLOCK_MEDIA_isi);

	/* Clock first, then channel reset, then configuration. */
	csi_isi->CHNL[0].CTRL = ISI_CTRL_CLK_EN(1U);
	csi_isi->CHNL[0].CTRL |= ISI_CTRL_SW_RST(1U);
	k_busy_wait(2);
	csi_isi->CHNL[0].CTRL &= ~ISI_CTRL_SW_RST_MASK;

	/* Bypass (no scaler/CSC), pixel link port 0 (MIPI), memory only, VC 0. */
	csi_isi->CHNL[0].CTRL = ISI_CTRL_CLK_EN(1U) | ISI_CTRL_CHNL_BYPASS(1U) | ISI_CTRL_SRC(0U) |
				ISI_CTRL_SRC_TYPE(0U) | ISI_CTRL_DEST(0U) | ISI_CTRL_VC_ID_0(0U) |
				ISI_CTRL_CHAIN_BUF(0U);
	csi_isi->CHNL[0].IMG_CTRL = ISI_IMG_CTRL_FORMAT(code) | ISI_IMG_CTRL_CSC_BYP(1U);
	csi_isi->CHNL[0].IMG_CFG = ISI_IMG_CFG_WIDTH(fmt->width) | ISI_IMG_CFG_HEIGHT(fmt->height);
	csi_isi->CHNL[0].SCALE_FACTOR = 0x10001000U;
	csi_isi->CHNL[0].SCALE_OFFSET = 0U;
	csi_isi->CHNL[0].SCL_IMG_CFG =
		ISI_SCL_IMG_CFG_WIDTH(fmt->width) | ISI_SCL_IMG_CFG_HEIGHT(fmt->height);
	csi_isi->CHNL[0].OUT_BUF_PITCH = ISI_OUT_BUF_PITCH_LINE_PITCH(pitch);
	csi_isi->CHNL[0].OUT_BUF_MAX_SIZE_Y = 0U;
	csi_isi->CHNL[0].OUT_BUF_MAX_SIZE_U = 0U;
	csi_isi->CHNL[0].OUT_BUF_MAX_SIZE_V = 0U;
	csi_isi->CHNL[0].IER = 0U;
	csi_isi->CHNL[0].STS = 0xFFFFFFFFU;

	if (config->source_dev && video_set_format(config->source_dev, src_fmt)) {
		return -EIO;
	}

	fmt->pitch = pitch;
	fmt->size = pitch * fmt->height;

	return 0;
}

static int csi_isi_set_stream(const struct device *dev, bool enable, enum video_buf_type type)
{
	const struct video_mcux_csi_config *config = dev->config;
	struct video_mcux_csi_data *data = dev->data;
	struct video_buffer *vbuf;

	if (enable) {
		struct video_buffer *s0 = k_fifo_get(&data->isi_free, K_NO_WAIT);
		struct video_buffer *s1 = k_fifo_get(&data->isi_free, K_NO_WAIT);

		if ((s0 == NULL) || (s1 == NULL)) {
			if (s0 != NULL) {
				k_fifo_put(&data->isi_free, s0);
			}
			return -ENOMEM;
		}

		data->isi_slot[0] = s0;
		data->isi_slot[1] = s1;
		data->isi_idx = 0U;
		csi_isi_load(0U, s0);
		csi_isi_load(1U, s1);

		csi_isi->CHNL[0].STS = 0xFFFFFFFFU;
		csi_isi->CHNL[0].IER = ISI_IER_FRM_RCVD_EN(1U);
		irq_enable(CSI_ISI_IRQN);
		csi_isi->CHNL[0].CTRL |= ISI_CTRL_CHNL_EN(1U);

		if (config->source_dev && video_stream_start(config->source_dev, type)) {
			return -EIO;
		}
	} else {
		if (config->source_dev && video_stream_stop(config->source_dev, type)) {
			return -EIO;
		}

		csi_isi->CHNL[0].CTRL &= ~ISI_CTRL_CHNL_EN_MASK;
		csi_isi->CHNL[0].IER = 0U;
		irq_disable(CSI_ISI_IRQN);

		/* Hand every buffer back to the application. */
		for (int i = 0; i < 2; i++) {
			if (data->isi_slot[i] != NULL) {
				k_fifo_put(&data->fifo_out, data->isi_slot[i]);
				data->isi_slot[i] = NULL;
			}
		}
		while ((vbuf = k_fifo_get(&data->isi_free, K_NO_WAIT)) != NULL) {
			k_fifo_put(&data->fifo_out, vbuf);
		}
	}

	return 0;
}

#endif /* CSI_USE_ISI */

static int video_mcux_csi_set_fmt(const struct device *dev, struct video_format *fmt)
{
	const struct video_mcux_csi_config *config = dev->config;
	struct video_mcux_csi_data *data = dev->data;
	status_t ret;
	struct video_format format = *fmt;

	data->csi_config.bytesPerPixel = video_bits_per_pixel(fmt->pixelformat) / BITS_PER_BYTE;
	data->csi_config.linePitch_Bytes = fmt->width * data->csi_config.bytesPerPixel;
#if defined(CONFIG_VIDEO_MCUX_MIPI_CSI2RX)
	if (fmt->pixelformat != VIDEO_PIX_FMT_BGRX32 && fmt->pixelformat != CSI_PIX_FMT_YUV32) {
		return -ENOTSUP;
	}
	video_pix_fmt_convert(&format, false);
#if defined(CSI_USE_ISI)
	return csi_isi_set_fmt(dev, fmt, &format);
#endif
	data->csi_config.dataBus = kCSI_DataBus24Bit;
#else
	data->csi_config.dataBus = kCSI_DataBus8Bit;
#endif
	data->csi_config.polarityFlags = kCSI_HsyncActiveHigh | kCSI_DataLatchOnRisingEdge;
	data->csi_config.workMode = kCSI_GatedClockMode; /* use VSYNC, HSYNC, and PIXCLK */
	data->csi_config.useExtVsync = true;
	data->csi_config.height = fmt->height;
	data->csi_config.width = fmt->width;

	ret = CSI_Init(config->base, &data->csi_config);
	if (ret != kStatus_Success) {
		return -EIO;
	}

	ret = CSI_TransferCreateHandle(config->base, &data->csi_handle, __frame_done_cb, data);
	if (ret != kStatus_Success) {
		return -EIO;
	}

	if (config->source_dev && video_set_format(config->source_dev, &format)) {
		return -EIO;
	}

	fmt->pitch = data->csi_config.linePitch_Bytes;
	fmt->size = fmt->pitch * fmt->height;

	return 0;
}

static int video_mcux_csi_get_fmt(const struct device *dev, struct video_format *fmt)
{
	const struct video_mcux_csi_config *config = dev->config;

	if (config->source_dev && !video_get_format(config->source_dev, fmt)) {
#if defined(CONFIG_VIDEO_MCUX_MIPI_CSI2RX)
		video_pix_fmt_convert(fmt, true);
#endif

		return video_estimate_fmt_size(fmt);
	}

	return -EIO;
}

static int video_mcux_csi_set_stream(const struct device *dev, bool enable,
				     enum video_buf_type type)
{
	const struct video_mcux_csi_config *config = dev->config;
	struct video_mcux_csi_data *data = dev->data;
	status_t ret;

#if defined(CSI_USE_ISI)
	ARG_UNUSED(ret);
	return csi_isi_set_stream(dev, enable, type);
#endif
	if (enable) {
		ret = CSI_TransferStart(config->base, &data->csi_handle);
		if (ret != kStatus_Success) {
			return -EIO;
		}

		if (config->source_dev && video_stream_start(config->source_dev, type)) {
			return -EIO;
		}
	} else {
		if (config->source_dev && video_stream_stop(config->source_dev, type)) {
			return -EIO;
		}

		ret = CSI_TransferStop(config->base, &data->csi_handle);
		if (ret != kStatus_Success) {
			return -EIO;
		}
	}

	return 0;
}

static int video_mcux_csi_flush(const struct device *dev, bool cancel)
{
	const struct video_mcux_csi_config *config = dev->config;
	struct video_mcux_csi_data *data = dev->data;
	struct video_buf *vbuf;
	uint32_t buffer_addr;
	status_t ret;

	if (!cancel) {
		/* wait for all buffer to be processed */
		do {
			k_sleep(K_MSEC(1));
		} while (!k_fifo_is_empty(&data->fifo_in));
	} else {
		/* Flush driver output queue */
		do {
			ret = CSI_TransferGetFullBuffer(config->base, &(data->csi_handle),
							&buffer_addr);
		} while (ret == kStatus_Success);

		while ((vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT))) {
			k_fifo_put(&data->fifo_out, vbuf);
			if (IS_ENABLED(CONFIG_POLL) && data->sig) {
				k_poll_signal_raise(data->sig, VIDEO_BUF_ABORTED);
			}
		}
	}

	return 0;
}

static int video_mcux_csi_enqueue(const struct device *dev, struct video_buffer *vbuf)
{
	const struct video_mcux_csi_config *config = dev->config;
	struct video_mcux_csi_data *data = dev->data;
	unsigned int to_read;
	status_t ret;

	to_read = data->csi_config.linePitch_Bytes * data->csi_config.height;
	vbuf->bytesused = to_read;
	vbuf->line_offset = 0;

#if defined(CSI_USE_ISI)
	ARG_UNUSED(config);
	ARG_UNUSED(ret);
	/* The ISI ISR picks free buffers from here when a slot completes. */
	k_fifo_put(&data->isi_free, vbuf);
	return 0;
#endif

	ret = CSI_TransferSubmitEmptyBuffer(config->base, &data->csi_handle,
					    (uint32_t)vbuf->buffer);
	if (ret != kStatus_Success) {
		return -EIO;
	}

	k_fifo_put(&data->fifo_in, vbuf);

	return 0;
}

static int video_mcux_csi_dequeue(const struct device *dev, struct video_buffer **vbuf,
				  k_timeout_t timeout)
{
	struct video_mcux_csi_data *data = dev->data;

	*vbuf = k_fifo_get(&data->fifo_out, timeout);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

static int video_mcux_csi_get_caps(const struct device *dev, struct video_caps *caps)
{
	const struct video_mcux_csi_config *config = dev->config;
	int err = -ENODEV;

	/* Just forward to source dev for now */
	if (config->source_dev) {
		err = video_get_caps(config->source_dev, caps);
#if defined(CONFIG_VIDEO_MCUX_MIPI_CSI2RX)
		/*
		 * On i.MX RT11xx SoCs which have MIPI CSI-2 Rx, image data from the camera sensor
		 * after passing through the pipeline (MIPI CSI-2 Rx --> Video Mux --> CSI) will be
		 * implicitly converted to a 32-bits pixel format. For example, an input in RGB565
		 * or YUYV (2-bytes format) will become BGRX32 or CSI_PIX_FMT_YUV32 (4-bytes format)
		 * respectively, at the output of the CSI. So, we change the pixel formats of the
		 * source caps to reflect this.
		 */
		int ind = 0;

		while (caps->format_caps[ind].pixelformat) {
			ind++;
		}
		k_heap_free(&csi_heap, fmts);
		fmts = k_heap_alloc(&csi_heap, (ind + 1) * sizeof(struct video_format_cap),
				    K_FOREVER);

		for (int i = 0; i <= ind; i++) {
			memcpy(&fmts[i], &caps->format_caps[i], sizeof(fmts[i]));
			if (fmts[i].pixelformat == VIDEO_PIX_FMT_RGB565) {
				fmts[i].pixelformat = VIDEO_PIX_FMT_BGRX32;
			} else if (fmts[i].pixelformat == VIDEO_PIX_FMT_YUYV) {
				fmts[i].pixelformat = CSI_PIX_FMT_YUV32;
			}
		}
		caps->format_caps = fmts;
#endif
	}

	/* NXP MCUX CSI request at least 2 buffer before starting */
	caps->min_vbuf_count = 2;

	/* no source dev */
	return err;
}

/*
 * CSI_DriverIRQHandler() only exists on parts whose CSI instance is called
 * plain "CSI". RT266x names it MEDIA__CSI, so dispatch through the
 * instance-indexed common handler there.
 */
#if defined(CSI)
extern void CSI_DriverIRQHandler(void);
#else
extern void CSI_CommonDriverIRQHandler(uint32_t instance);
#endif

static void video_mcux_csi_isr(const void *p)
{
	ARG_UNUSED(p);
#if defined(CSI)
	CSI_DriverIRQHandler();
#else
	CSI_CommonDriverIRQHandler(0);
#endif
}

static int video_mcux_csi_init(const struct device *dev)
{
	const struct video_mcux_csi_config *config = dev->config;
	struct video_mcux_csi_data *data = dev->data;
	int err;

	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);
#if defined(CSI_USE_ISI)
	k_fifo_init(&data->isi_free);
#endif

	CSI_GetDefaultConfig(&data->csi_config);

	/* check if there is any source device (video ctrl device)
	 * the device is not yet initialized so we only check if it exists
	 */
	if (config->source_dev == NULL) {
		return -ENODEV;
	}

	/*
	 * With a MIPI CSI-2 source there are no parallel pins and the node has
	 * no pinctrl-0, so only apply a state when there is one.
	 */
	if (DT_INST_NODE_HAS_PROP(0, pinctrl_0)) {
		err = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
		if (err) {
			return err;
		}
	}

	return 0;
}

#ifdef CONFIG_POLL
static int video_mcux_csi_set_signal(const struct device *dev, struct k_poll_signal *sig)
{
	struct video_mcux_csi_data *data = dev->data;

	if (data->sig && sig != NULL) {
		return -EALREADY;
	}

	data->sig = sig;

	return 0;
}
#endif

static int video_mcux_csi_set_frmival(const struct device *dev, struct video_frmival *frmival)
{
	const struct video_mcux_csi_config *config = dev->config;

	return video_set_frmival(config->source_dev, frmival);
}

static int video_mcux_csi_get_frmival(const struct device *dev, struct video_frmival *frmival)
{
	const struct video_mcux_csi_config *config = dev->config;

	return video_get_frmival(config->source_dev, frmival);
}

static int video_mcux_csi_enum_frmival(const struct device *dev, struct video_frmival_enum *fie)
{
	const struct video_mcux_csi_config *config = dev->config;
	const struct video_format *fie_fmt = fie->format;
	int ret;

#if defined(CONFIG_VIDEO_MCUX_MIPI_CSI2RX)
	struct video_format converted_fmt = *fie->format;

	video_pix_fmt_convert(&converted_fmt, false);
	fie->format = &converted_fmt;
#endif

	ret = video_enum_frmival(config->source_dev, fie);
	fie->format = fie_fmt;

	return ret;
}

static DEVICE_API(video, video_mcux_csi_driver_api) = {
	.set_format = video_mcux_csi_set_fmt,
	.get_format = video_mcux_csi_get_fmt,
	.set_stream = video_mcux_csi_set_stream,
	.flush = video_mcux_csi_flush,
	.enqueue = video_mcux_csi_enqueue,
	.dequeue = video_mcux_csi_dequeue,
	.get_caps = video_mcux_csi_get_caps,
	.set_frmival = video_mcux_csi_set_frmival,
	.get_frmival = video_mcux_csi_get_frmival,
	.enum_frmival = video_mcux_csi_enum_frmival,
#ifdef CONFIG_POLL
	.set_signal = video_mcux_csi_set_signal,
#endif
};

#if 1 /* Unique Instance */
PINCTRL_DT_INST_DEFINE(0);

#define SOURCE_DEV(n) DEVICE_DT_GET(DT_NODE_REMOTE_DEVICE(DT_INST_ENDPOINT_BY_ID(n, 0, 0)))

static const struct video_mcux_csi_config video_mcux_csi_config_0 = {
	.base = (CSI_Type *)DT_INST_REG_ADDR(0),
	.source_dev = SOURCE_DEV(0),
	.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
};

static struct video_mcux_csi_data video_mcux_csi_data_0;

static int video_mcux_csi_init_0(const struct device *dev)
{
	struct video_mcux_csi_data *data = dev->data;

#if defined(CONFIG_SOC_SERIES_IMXRT266X)
	/* Release the block from reset and ungate it before touching a register. */
	RESET_PeripheralReset((reset_ip_name_t)kModCon_MEDIA_CSI);
	CLOCK_EnableClock(kCLOCK_MEDIA_csi);
#endif

	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), video_mcux_csi_isr, NULL, 0);

	irq_enable(DT_INST_IRQN(0));

	data->dev = dev;

#if defined(CSI_USE_ISI)
	csi_isi_dev = dev;
	IRQ_CONNECT(CSI_ISI_IRQN, DT_INST_IRQ(0, priority), csi_isi_isr, NULL, 0);
#endif

	return video_mcux_csi_init(dev);
}

DEVICE_DT_INST_DEFINE(0, &video_mcux_csi_init_0, NULL, &video_mcux_csi_data_0,
		      &video_mcux_csi_config_0, POST_KERNEL, CONFIG_VIDEO_MCUX_CSI_INIT_PRIORITY,
		      &video_mcux_csi_driver_api);

VIDEO_DEVICE_DEFINE(csi, DEVICE_DT_INST_GET(0), SOURCE_DEV(0));

#endif
