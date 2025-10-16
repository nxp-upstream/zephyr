/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_lcdifv3

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>
#include <fsl_lcdifv3.h>

#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>

#define MCUX_LCDIFV3_FB_NUM 1

LOG_MODULE_REGISTER(display_mcux_lcdifv3, CONFIG_DISPLAY_LOG_LEVEL);

/* Required by DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(_dev)  ((const struct mcux_lcdifv3_config *)(_dev)->config)
#define DEV_DATA(_dev) ((struct mcux_lcdifv3_data *)(_dev)->data)

struct mcux_lcdifv3_config {
	DEVICE_MMIO_NAMED_ROM(reg_base);

	const struct device *disp_pix_clk_dev;
	clock_control_subsys_t disp_pix_clk_subsys;
	uint32_t disp_pix_clk_rate;
	const struct device *media_axi_clk_dev;
	clock_control_subsys_t media_axi_clk_subsys;
	uint32_t media_axi_clk_rate;
	const struct device *media_apb_clk_dev;
	clock_control_subsys_t media_apb_clk_subsys;
	uint32_t media_apb_clk_rate;

	void (*irq_config_func)(const struct device *dev);
	lcdifv3_buffer_config_t buffer_config;
	lcdifv3_display_config_t display_config;
	enum display_pixel_format pixel_format;
	size_t pixel_bytes;
	size_t fb_bytes;
};

struct mcux_lcdifv3_data {
	DEVICE_MMIO_NAMED_RAM(reg_base);
	uint8_t *fb_ptr;
	uint8_t *fb[MCUX_LCDIFV3_FB_NUM];
	struct k_sem sem;
	uint8_t write_idx;
};

static int mcux_lcdifv3_write(const struct device *dev, const uint16_t x, const uint16_t y,
			      const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct mcux_lcdifv3_config *config = dev->config;
	struct mcux_lcdifv3_data *dev_data = dev->data;
	LCDIF_Type *base = (LCDIF_Type *)DEVICE_MMIO_NAMED_GET(dev, reg_base);

	if ((config->pixel_bytes * desc->pitch * desc->height) > desc->buf_size) {
		LOG_ERR("Input buffer too small");
		return -ENOTSUP;
	}

	LOG_DBG("W=%d, H=%d, @%d,%d", desc->width, desc->height, x, y);

	/* Dump LCDIF Registers */
	LOG_DBG("CTRL: 0x%x\n", base->CTRL.RW);
	LOG_DBG("DISP_PARA: 0x%x\n", base->DISP_PARA);
	LOG_DBG("DISP_SIZE: 0x%x\n", base->DISP_SIZE);
	LOG_DBG("HSYN_PARA: 0x%x\n", base->HSYN_PARA);
	LOG_DBG("VSYN_PARA: 0x%x\n", base->VSYN_PARA);
	LOG_DBG("VSYN_HSYN_WIDTH: 0x%x\n", base->VSYN_HSYN_WIDTH);
	LOG_DBG("INT_STATUS_D0: 0x%x\n", base->INT_STATUS_D0);
	LOG_DBG("INT_STATUS_D1: 0x%x\n", base->INT_STATUS_D1);
	LOG_DBG("CTRLDESCL_1: 0x%x\n", base->CTRLDESCL_1[0]);
	LOG_DBG("CTRLDESCL_3: 0x%x\n", base->CTRLDESCL_3[0]);
	LOG_DBG("CTRLDESCL_LOW_4: 0x%x\n", base->CTRLDESCL_LOW_4[0]);
	LOG_DBG("CTRLDESCL_HIGH_4: 0x%x\n", base->CTRLDESCL_HIGH_4[0]);
	LOG_DBG("CTRLDESCL_5: 0x%x\n", base->CTRLDESCL_5[0]);

	/* wait for the next frame done */
	k_sem_reset(&dev_data->sem);

	sys_cache_data_flush_and_invd_range((void *)buf, desc->buf_size);
	LCDIFV3_SetLayerSize(base, 0, desc->width, desc->height);
	LCDIFV3_SetLayerBufferAddr(base, 0, (uint32_t)(uintptr_t)buf);
	LCDIFV3_TriggerLayerShadowLoad(base, 0);

	k_sem_take(&dev_data->sem, K_FOREVER);

	return 0;
}

static int mcux_lcdifv3_read(const struct device *dev, const uint16_t x, const uint16_t y,
			     const struct display_buffer_descriptor *desc, void *buf)
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

static int mcux_lcdifv3_set_brightness(const struct device *dev, const uint8_t brightness)
{
	LOG_WRN("Set brightness not implemented");
	return -ENOTSUP;
}

static int mcux_lcdifv3_set_contrast(const struct device *dev, const uint8_t contrast)
{
	LOG_ERR("Set contrast not implemented");
	return -ENOTSUP;
}

static int mcux_lcdifv3_set_pixel_format(const struct device *dev,
					 const enum display_pixel_format pixel_format)
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
	struct mcux_lcdifv3_data *dev_data = dev->data;
	uint32_t status;
	LCDIF_Type *base = (LCDIF_Type *)DEVICE_MMIO_NAMED_GET(dev, reg_base);

	status = LCDIFV3_GetInterruptStatus(base);
	LCDIFV3_ClearInterruptStatus(base, status);

	k_sem_give(&dev_data->sem);
}

static int mcux_lcdifv3_configure_clock(const struct device *dev)
{
	const struct mcux_lcdifv3_config *config = dev->config;

	clock_control_set_rate(config->disp_pix_clk_dev, config->disp_pix_clk_subsys,
			       (clock_control_subsys_rate_t)(uintptr_t)config->disp_pix_clk_rate);
	return 0;
}

static int mcux_axi_apb_configure_clock(const struct device *dev)
{
	const struct mcux_lcdifv3_config *config = dev->config;
	uint32_t clk_freq;

	/* configure media_axi_clk */
	if (!device_is_ready(config->media_axi_clk_dev)) {
		LOG_ERR("media_axi clock control device not ready");
		return -ENODEV;
	}
	clock_control_set_rate(config->media_axi_clk_dev, config->media_axi_clk_subsys,
			       (clock_control_subsys_rate_t)(uintptr_t)config->media_axi_clk_rate);
	if (clock_control_get_rate(config->media_axi_clk_dev, config->media_axi_clk_subsys,
				   &clk_freq)) {
		return -EINVAL;
	}
	LOG_DBG("media_axi clock frequency %d", clk_freq);

	/* configure media_apb_clk */
	if (!device_is_ready(config->media_apb_clk_dev)) {
		LOG_ERR("media_apb clock control device not ready");
		return -ENODEV;
	}
	clock_control_set_rate(config->media_apb_clk_dev, config->media_apb_clk_subsys,
			       (clock_control_subsys_rate_t)(uintptr_t)config->media_apb_clk_rate);
	if (clock_control_get_rate(config->media_apb_clk_dev, config->media_apb_clk_subsys,
				   &clk_freq)) {
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

	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE | K_MEM_DIRECT_MAP);
	LCDIF_Type *base = (LCDIF_Type *)DEVICE_MMIO_NAMED_GET(dev, reg_base);

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

	if (clock_control_get_rate(config->disp_pix_clk_dev, config->disp_pix_clk_subsys,
				   &clk_freq)) {
		LOG_ERR("Failed to get disp_pix_clk\n");
		return -EINVAL;
	}
	LOG_INF("disp_pix clock frequency %d", clk_freq);

	lcdifv3_buffer_config_t buffer_config = config->buffer_config;
	lcdifv3_display_config_t display_config = config->display_config;
	/* Set the Pixel format */
	if (config->pixel_format == PIXEL_FORMAT_BGR_565) {
		buffer_config.pixelFormat = kLCDIFV3_PixelFormatRGB565;
	} else if (config->pixel_format == PIXEL_FORMAT_RGB_888) {
		buffer_config.pixelFormat = kLCDIFV3_PixelFormatRGB888;
	} else if (config->pixel_format == PIXEL_FORMAT_ARGB_8888) {
		buffer_config.pixelFormat = kLCDIFV3_PixelFormatARGB8888;
	}

	LCDIFV3_Init(base);

	LCDIFV3_SetDisplayConfig(base, &display_config);
	LCDIFV3_EnableDisplay(base, true);
	LCDIFV3_SetLayerBufferConfig(base, 0, &buffer_config);
	LCDIFV3_SetLayerSize(base, 0, display_config.panelWidth, display_config.panelHeight);
	LCDIFV3_EnableLayer(base, 0, true);
	LCDIFV3_EnablePlanePanic(base);
	LCDIFV3_SetLayerBufferAddr(base, 0, (uint64_t)dev_data->fb[0]);
	LCDIFV3_TriggerLayerShadowLoad(base, 0);
	LCDIFV3_EnableInterrupts(base, kLCDIFV3_VerticalBlankingInterrupt);

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

#define GET_PIXEL_FORMAT(id)                                                                       \
	((DT_INST_ENUM_IDX(id, pixel_format) == 0)                                                 \
		 ? PIXEL_FORMAT_BGR_565                                                            \
		 : ((DT_INST_ENUM_IDX(id, pixel_format) == 1) ? PIXEL_FORMAT_RGB_888               \
							      : PIXEL_FORMAT_ARGB_8888))

#define GET_PIXEL_BYTES(id)                                                                        \
	((DT_INST_ENUM_IDX(id, pixel_format) == 0)                                                 \
		 ? 2                                                                               \
		 : ((DT_INST_ENUM_IDX(id, pixel_format) == 1) ? 3 : 4))

#define MCUX_LCDIFV3_DEVICE_INIT(id)                                                               \
	static void mcux_lcdifv3_config_func_##id(const struct device *dev)                        \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(id), DT_INST_IRQ(id, priority), mcux_lcdifv3_isr,         \
			    DEVICE_DT_INST_GET(id), 0);                                            \
		irq_enable(DT_INST_IRQN(id));                                                      \
	}                                                                                          \
	static const struct mcux_lcdifv3_config mcux_lcdifv3_config_##id = {                       \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(id)),                             \
		.disp_pix_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(id, 0)),              \
		.disp_pix_clk_subsys =                                                             \
			(clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(id, 0, name),           \
		.disp_pix_clk_rate = DT_PROP(DT_INST_CHILD(id, display_timings), clock_frequency), \
		.media_axi_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(id, 1)),             \
		.media_axi_clk_subsys =                                                            \
			(clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(id, 1, name),           \
		.media_axi_clk_rate = DT_INST_PROP(id, media_axi_clk_rate),                        \
		.media_apb_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(id, 2)),             \
		.media_apb_clk_subsys =                                                            \
			(clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(id, 2, name),           \
		.media_apb_clk_rate = DT_INST_PROP(id, media_apb_clk_rate),                        \
		.irq_config_func = mcux_lcdifv3_config_func_##id,                                  \
		.buffer_config =                                                                   \
			{                                                                          \
				.strideBytes = GET_PIXEL_BYTES(id) * DT_INST_PROP(id, width),      \
			},                                                                         \
		.display_config =                                                                  \
			{                                                                          \
				.panelWidth = DT_INST_PROP(id, width),                             \
				.panelHeight = DT_INST_PROP(id, height),                           \
				.lineOrder = kLCDIFV3_LineOrderRGBOrYUV,                           \
				.hsw = DT_PROP(DT_INST_CHILD(id, display_timings), hsync_len),     \
				.hfp = DT_PROP(DT_INST_CHILD(id, display_timings), hfront_porch),  \
				.hbp = DT_PROP(DT_INST_CHILD(id, display_timings), hback_porch),   \
				.vsw = DT_PROP(DT_INST_CHILD(id, display_timings), vsync_len),     \
				.vfp = DT_PROP(DT_INST_CHILD(id, display_timings), vfront_porch),  \
				.vbp = DT_PROP(DT_INST_CHILD(id, display_timings), vback_porch),   \
				.polarityFlags =                                                   \
					(DT_PROP(DT_INST_CHILD(id, display_timings), hsync_active) \
						 ? kLCDIFV3_HsyncActiveLow                         \
						 : kLCDIFV3_HsyncActiveHigh) |                     \
					(DT_PROP(DT_INST_CHILD(id, display_timings), vsync_active) \
						 ? kLCDIFV3_VsyncActiveLow                         \
						 : kLCDIFV3_VsyncActiveHigh) |                     \
					(DT_PROP(DT_INST_CHILD(id, display_timings), de_active)    \
						 ? kLCDIFV3_DataEnableActiveLow                    \
						 : kLCDIFV3_DataEnableActiveHigh) |                \
					(DT_PROP(DT_INST_CHILD(id, display_timings),               \
						 pixelclk_active)                                  \
						 ? kLCDIFV3_DriveDataOnRisingClkEdge               \
						 : kLCDIFV3_DriveDataOnFallingClkEdge),            \
			},                                                                         \
		.pixel_format = GET_PIXEL_FORMAT(id),                                              \
		.pixel_bytes = GET_PIXEL_BYTES(id),                                                \
		.fb_bytes =                                                                        \
			DT_INST_PROP(id, width) * DT_INST_PROP(id, height) * GET_PIXEL_BYTES(id),  \
	};                                                                                         \
	static uint8_t                                                                             \
		__aligned(64) frame_buffer_##id[MCUX_LCDIFV3_FB_NUM * DT_INST_PROP(id, width) *    \
						DT_INST_PROP(id, height) * GET_PIXEL_BYTES(id)];   \
	static struct mcux_lcdifv3_data mcux_lcdifv3_data_##id = {                                 \
		.fb_ptr = frame_buffer_##id,                                                       \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(id, &mcux_lcdifv3_init, NULL, &mcux_lcdifv3_data_##id,               \
			      &mcux_lcdifv3_config_##id, POST_KERNEL,                              \
			      CONFIG_DISPLAY_INIT_PRIORITY, &mcux_lcdifv3_api);

DT_INST_FOREACH_STATUS_OKAY(MCUX_LCDIFV3_DEVICE_INIT)
