/*
 * Copyright (c) 2019-22, NXP
 * Copyright (c) 2022, Basalte bv
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_lcdifv3

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <fsl_lcdifv3.h>

#include <zephyr/logging/log.h>
#include <zephyr/irq.h>

#define MCUX_LCDIFV3_FB_NUM 1

LOG_MODULE_REGISTER(display_mcux_lcdifv3, CONFIG_DISPLAY_LOG_LEVEL);

struct mcux_lcdifv3_config {
	LCDIF_Type *base;
	const struct device *disp_pix_clk_dev;
	clock_control_subsys_t disp_pix_clk_subsys;
	struct _clock_root_config_t clk_config;
	const struct device *media_axi_clk_dev;
	clock_control_subsys_t media_axi_clk_subsys;
	struct _clock_root_config_t media_axi_clk_cfg;
	const struct device *media_apb_clk_dev;
	clock_control_subsys_t media_apb_clk_subsys;
	struct _clock_root_config_t media_apb_clk_cfg;
	void (*irq_config_func)(const struct device *dev);
	lcdifv3_buffer_config_t buffer_config;
	lcdifv3_display_config_t display_config;
	enum display_pixel_format pixel_format;
	size_t pixel_bytes;
	size_t fb_bytes;
};

struct mcux_lcdifv3_data {
	uint8_t *fb_ptr;
	uint8_t *fb[MCUX_LCDIFV3_FB_NUM];
	struct k_sem sem;
	uint8_t write_idx;
};

static int mcux_lcdifv3_write(const struct device *dev, const uint16_t x,
			     const uint16_t y,
			     const struct display_buffer_descriptor *desc,
			     const void *buf)
{
	const struct mcux_lcdifv3_config *config = dev->config;
	struct mcux_lcdifv3_data *dev_data = dev->data;

	__ASSERT((config->pixel_bytes * desc->pitch * desc->height) <=
		 desc->buf_size, "Input buffer too small");

	LOG_DBG("W=%d, H=%d, @%d,%d", desc->width, desc->height, x, y);

#if 0
	/* Read Status Registers */
	LCDIF_Type *lcdif = config->base;
	LOG_INF("CTRL: 0x%x\n", lcdif->CTRL.RW);
	LOG_INF("DISP_PARA: 0x%x\n", lcdif->DISP_PARA);
	LOG_INF("DISP_SIZE: 0x%x\n", lcdif->DISP_SIZE);
	LOG_INF("HSYN_PARA: 0x%x\n", lcdif->HSYN_PARA);
	LOG_INF("VSYN_PARA: 0x%x\n", lcdif->VSYN_PARA);
	LOG_INF("VSYN_HSYN_WIDTH: 0x%x\n", lcdif->VSYN_HSYN_WIDTH);
	LOG_INF("INT_STATUS_D0: 0x%x\n", lcdif->INT_STATUS_D0);
	LOG_INF("INT_STATUS_D1: 0x%x\n", lcdif->INT_STATUS_D1);
	LOG_INF("CTRLDESCL_1: 0x%x\n", lcdif->CTRLDESCL_1[0]);
	LOG_INF("CTRLDESCL_3: 0x%x\n", lcdif->CTRLDESCL_3[0]);
	LOG_INF("CTRLDESCL_LOW_4: 0x%x\n", lcdif->CTRLDESCL_LOW_4[0]);
	LOG_INF("CTRLDESCL_HIGH_4: 0x%x\n", lcdif->CTRLDESCL_HIGH_4[0]);
	LOG_INF("CTRLDESCL_5: 0x%x\n", lcdif->CTRLDESCL_5[0]);
#endif
	/* wait for the next frame done */
	k_sem_reset(&dev_data->sem);
	k_sem_take(&dev_data->sem, K_FOREVER);

	LCDIFV3_SetStrideBytes(config->base, desc->pitch);
	LCDIFV3_SetLayerSize(config->base, desc->width, desc->height);
	LCDIFV3_SetLayerBufferAddr(config->base, buf);
	LCDIFV3_TriggerLayerShadowLoad(config->base);

	return 0;
}

static int mcux_lcdifv3_read(const struct device *dev, const uint16_t x,
			    const uint16_t y,
			    const struct display_buffer_descriptor *desc,
			    void *buf)
{
	LOG_ERR("Read not implemented");
	return -ENOTSUP;
}

static void *mcux_lcdifv3_get_framebuffer(const struct device *dev)
{
	struct mcux_lcdifv3_data *dev_data = dev->data;

	return dev_data->fb_ptr;
}

static int mcux_lcdifv3_display_blanking_off(const struct device *dev)
{
	LOG_ERR("Blanking off not implemented");
	return -ENOTSUP;
}

static int mcux_lcdifv3_display_blanking_on(const struct device *dev)
{
	LOG_ERR("Blanking on not implemented");
	return -ENOTSUP;
}

static int mcux_lcdifv3_set_brightness(const struct device *dev,
				      const uint8_t brightness)
{
	LOG_WRN("Set brightness not implemented");
	return -ENOTSUP;
}

static int mcux_lcdifv3_set_contrast(const struct device *dev,
				    const uint8_t contrast)
{
	LOG_ERR("Set contrast not implemented");
	return -ENOTSUP;
}

static int mcux_lcdifv3_set_pixel_format(const struct device *dev,
					const enum display_pixel_format
					pixel_format)
{
	LOG_ERR("Set pixel format not implemented");
	return -ENOTSUP;
}

static int mcux_lcdifv3_set_orientation(const struct device *dev,
		const enum display_orientation orientation)
{
	LOG_ERR("Changing display orientation not implemented");
	return -ENOTSUP;
}

static void mcux_lcdifv3_get_capabilities(const struct device *dev,
		struct display_capabilities *capabilities)
{
	const struct mcux_lcdifv3_config *config = dev->config;

	memset(capabilities, 0, sizeof(struct display_capabilities));
	capabilities->x_resolution = config->display_config.panelWidth;
	capabilities->y_resolution = config->display_config.panelHeight;
	capabilities->supported_pixel_formats = config->pixel_format;
	capabilities->current_pixel_format = config->pixel_format;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static void mcux_lcdifv3_isr(const struct device *dev)
{
	const struct mcux_lcdifv3_config *config = dev->config;
	struct mcux_lcdifv3_data *dev_data = dev->data;
	uint32_t status;

	status = LCDIFV3_GetInterruptStatus(config->base, 0);
	LCDIFV3_ClearInterruptStatus(config->base, 0,status);

	k_sem_give(&dev_data->sem);
}

static int mcux_lcdifv3_configure_clock(const struct device *dev)
{
	const struct mcux_lcdifv3_config *config = dev->config;

	return clock_control_configure(config->disp_pix_clk_dev, config->disp_pix_clk_subsys, (void *)&config->clk_config);
}

static int mcux_axi_apb_configure_clock(const struct device *dev)
{
	const struct mcux_lcdifv3_config *config = dev->config;
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

static int mcux_lcdifv3_init(const struct device *dev)
{
	const struct mcux_lcdifv3_config *config = dev->config;
	struct mcux_lcdifv3_data *dev_data = dev->data;
	uint32_t clk_freq;

	dev_data->fb[0] = dev_data->fb_ptr;

	k_sem_init(&dev_data->sem, 1, 1);

	config->irq_config_func(dev);

	/* configure disp_pix_clk */
	if (!device_is_ready(config->disp_pix_clk_dev)) {
		LOG_ERR("cam_pix clock control device not ready\n");
		return -ENODEV;
	}

	mcux_axi_apb_configure_clock(dev);
	mcux_lcdifv3_configure_clock(dev);

	if (clock_control_get_rate(config->disp_pix_clk_dev, config->disp_pix_clk_subsys, &clk_freq)) {
		LOG_ERR("Failed to get disp_pix_clk\n");
		return -EINVAL;
	}
	LOG_INF("disp_pix clock frequency %d", clk_freq);

	lcdifv3_buffer_config_t buffer_config = config->buffer_config;
	/* Set the Pixel format */
	if (config->pixel_format == PIXEL_FORMAT_BGR_565) {
		buffer_config.pixelFormat = kLCDIFV3_PixelFormatRGB565;
	} else if (config->pixel_format == PIXEL_FORMAT_RGB_888) {
		buffer_config.pixelFormat = kLCDIFV3_PixelFormatRGB888;
	} else if (config->pixel_format == PIXEL_FORMAT_ARGB_8888) {
		buffer_config.pixelFormat = kLCDIFV3_PixelFormatARGB8888;
	}

	LCDIFV3_Init(config->base);

	LCDIFV3_SetDisplayConfig(config->base, &config->display_config);

	LCDIFV3_SetLayerBufferConfig(config->base, &buffer_config);
	LCDIFV3_SetLayerSize(config->base, config->display_config.panelWidth, config->display_config.panelHeight);
	LCDIFV3_EnableInterrupts(config->base, 0, kLCDIFV3_VerticalBlankingInterrupt);
	LCDIFV3_EnablePlanePanic(config->base);
	LCDIFV3_EnableDisplay(config->base, true);
	//config->base->DISP_PARA |= (0x2 << 24);
	LCDIFV3_SetLayerBufferAddr(config->base, (uint64_t)dev_data->fb[0]);
	LCDIFV3_TriggerLayerShadowLoad(config->base);

	LOG_INF("%s init succeeded\n", dev->name);

	return 0;
}

static const struct display_driver_api mcux_lcdifv3_api = {
	.blanking_on = mcux_lcdifv3_display_blanking_on,
	.blanking_off = mcux_lcdifv3_display_blanking_off,
	.write = mcux_lcdifv3_write,
	.read = mcux_lcdifv3_read,
	.get_framebuffer = mcux_lcdifv3_get_framebuffer,
	.set_brightness = mcux_lcdifv3_set_brightness,
	.set_contrast = mcux_lcdifv3_set_contrast,
	.get_capabilities = mcux_lcdifv3_get_capabilities,
	.set_pixel_format = mcux_lcdifv3_set_pixel_format,
	.set_orientation = mcux_lcdifv3_set_orientation,
};

#define GET_PIXEL_FORMAT(id)								\
(											\
	(DT_INST_ENUM_IDX(id, pixel_format) == 0) ? PIXEL_FORMAT_BGR_565 :		\
	((DT_INST_ENUM_IDX(id, pixel_format) == 1) ? PIXEL_FORMAT_RGB_888 :		\
	 PIXEL_FORMAT_ARGB_8888)							\
)											\

#define GET_PIXEL_BYTES(id)							\
(										\
	(DT_INST_ENUM_IDX(id, pixel_format) == 0) ? 2 :				\
	((DT_INST_ENUM_IDX(id, pixel_format) == 1) ? 3 :4)			\
)										\

#define MCUX_LCDIFV3_DEVICE_INIT(id)						\
	PINCTRL_DT_INST_DEFINE(id);						\
	static void mcux_lcdifv3_config_func_##id(const struct device *dev);	\
	static const struct mcux_lcdifv3_config mcux_lcdifv3_config_##id = {	\
		.base = (LCDIF_Type *) DT_INST_REG_ADDR(id),			\
		.disp_pix_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(id, 0)),			\
		.disp_pix_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(id, 0, name),	\
		.clk_config = {										\
			.clockOff = false,								\
			.mux = DT_INST_CLOCKS_CELL_BY_IDX(id, 0, mux),					\
			.div = DT_INST_CLOCKS_CELL_BY_IDX(id, 0, div),					\
		},											\
		.media_axi_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(id, 1)),			\
		.media_axi_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(id, 1, name),\
		.media_axi_clk_cfg = {									\
			.clockOff = false,								\
			.mux = DT_INST_CLOCKS_CELL_BY_IDX(id, 1, mux),					\
			.div = DT_INST_CLOCKS_CELL_BY_IDX(id, 1, div),					\
		},											\
		.media_apb_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(id, 2)),			\
		.media_apb_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(id, 2, name),\
		.media_apb_clk_cfg = {									\
			.clockOff = false,								\
			.mux = DT_INST_CLOCKS_CELL_BY_IDX(id, 2, mux),					\
			.div = DT_INST_CLOCKS_CELL_BY_IDX(id, 2, div),					\
		},											\
		.irq_config_func = mcux_lcdifv3_config_func_##id,		\
		.buffer_config = {							\
			.strideBytes = GET_PIXEL_BYTES(id) * DT_INST_PROP(id, width),	\
		},									\
		.display_config = {						\
			.panelWidth = DT_INST_PROP(id, width),			\
			.panelHeight = DT_INST_PROP(id, height),		\
			.hsw = DT_INST_PROP(id, hsync),				\
			.hfp = DT_INST_PROP(id, hfp),				\
			.hbp = DT_INST_PROP(id, hbp),				\
			.vsw = DT_INST_PROP(id, vsync),				\
			.vfp = DT_INST_PROP(id, vfp),				\
			.vbp = DT_INST_PROP(id, vbp),				\
			.lineOrder = kLCDIFV3_LineOrderRGB,			\
			.polarityFlags = DT_INST_PROP(id, polarity),		\
		},								\
		.pixel_format = GET_PIXEL_FORMAT(id),     			\
		.pixel_bytes = GET_PIXEL_BYTES(id),				\
		.fb_bytes = DT_INST_PROP(id, width) * DT_INST_PROP(id, height)	\
			* GET_PIXEL_BYTES(id),					\
	};									\
	static uint8_t __aligned(64) frame_buffer_##id[MCUX_LCDIFV3_FB_NUM	\
			* DT_INST_PROP(id, width)				\
			* DT_INST_PROP(id, height)				\
			* GET_PIXEL_BYTES(id)];					\
	static struct mcux_lcdifv3_data mcux_lcdifv3_data_##id = {		\
		.fb_ptr = frame_buffer_##id,					\
	};									\
	DEVICE_DT_INST_DEFINE(id,						\
			    &mcux_lcdifv3_init,					\
			    NULL,						\
			    &mcux_lcdifv3_data_##id,				\
			    &mcux_lcdifv3_config_##id,				\
			    POST_KERNEL,					\
			    CONFIG_DISPLAY_INIT_PRIORITY,			\
			    &mcux_lcdifv3_api);					\
	static void mcux_lcdifv3_config_func_##id(const struct device *dev)	\
	{									\
		IRQ_CONNECT(DT_INST_IRQN(id),					\
			    DT_INST_IRQ(id, priority),				\
			    mcux_lcdifv3_isr,					\
			    DEVICE_DT_INST_GET(id),				\
			    0);							\
		irq_enable(DT_INST_IRQN(id));					\
	}

DT_INST_FOREACH_STATUS_OKAY(MCUX_LCDIFV3_DEVICE_INIT)
