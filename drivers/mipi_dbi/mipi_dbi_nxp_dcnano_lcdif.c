/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_dcnano_lcdif_dbi

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/dt-bindings/mipi_dbi/mipi_dbi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <fsl_lcdif.h>

LOG_MODULE_REGISTER(mipi_dbi_nxp_dcnano_lcdif, CONFIG_DISPLAY_LOG_LEVEL);

struct mcux_dcnano_lcdif_dbi_data {
	struct k_sem transfer_done;
	bool first_write;
};

struct mcux_dcnano_lcdif_dbi_config {
	LCDIF_Type *base;
	void (*irq_config_func)(const struct device *dev);
	lcdif_dbi_config_t dbi_config;
	lcdif_panel_config_t panel_config;
	const struct pinctrl_dev_config *pincfg;
	const struct gpio_dt_spec reset;
};

static void mcux_dcnano_lcdif_dbi_isr(const struct device *dev)
{
	const struct mcux_dcnano_lcdif_dbi_config *config = dev->config;
	struct mcux_dcnano_lcdif_dbi_data *lcdif_data = dev->data;
	uint32_t status;

	status = LCDIF_GetAndClearInterruptPendingFlags(config->base);

	if (0 != (status & kLCDIF_Display0FrameDoneInterrupt)) {
		k_sem_give(&lcdif_data->transfer_done);
	}
}

static int mcux_dcnano_lcdif_dbi_init(const struct device *dev)
{
	const struct mcux_dcnano_lcdif_dbi_config *config = dev->config;
	struct mcux_dcnano_lcdif_dbi_data *lcdif_data = dev->data;

#if !CONFIG_MIPI_DSI_MCUX_DCNANO_DBI
	int ret;

	/* Pin control is optional when MIPI-DSI interface is used. */
	ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret) {
		return ret;
	}
#endif

	LCDIF_Init(config->base);

	if (config->dbi_config.type == 0xFF) {
		LOG_ERR("Bus type not supported.");
		return -ENODEV;
	}

	LCDIF_DbiModeSetConfig(config->base, 0, &config->dbi_config);

	LCDIF_SetPanelConfig(config->base, 0, &config->panel_config);

	LCDIF_EnableInterrupts(config->base, kLCDIF_Display0FrameDoneInterrupt);

	config->irq_config_func(dev);

	k_sem_init(&lcdif_data->transfer_done, 0, 1);

	LOG_DBG("%s device init complete", dev->name);

	return 0;
}

static int mipi_dbi_dcnano_lcdif_write_display(const struct device *dev,
						   const struct mipi_dbi_config *dbi_config,
						   const uint8_t *framebuf,
						   struct display_buffer_descriptor *desc,
						   enum display_pixel_format pixfmt)
{
	const struct mcux_dcnano_lcdif_dbi_config *config = dev->config;
	struct mcux_dcnano_lcdif_dbi_data *lcdif_data = dev->data;
	int ret = 0;
	uint8_t bytes_per_pixel = 0U;

	/* The bus of dbi_config shall be set in init. */
	ARG_UNUSED(dbi_config);

	lcdif_fb_config_t fbConfig;

	LCDIF_FrameBufferGetDefaultConfig(&fbConfig);

	fbConfig.enable		= true;
	fbConfig.inOrder	= kLCDIF_PixelInputOrderARGB;
	fbConfig.rotateFlipMode	= kLCDIF_Rotate0;
	switch (pixfmt) {
	case PIXEL_FORMAT_RGB_888:
		fbConfig.format = kLCDIF_PixelFormatRGB888;
		bytes_per_pixel = 3U;
		break;
	case PIXEL_FORMAT_ARGB_8888:
		fbConfig.format = kLCDIF_PixelFormatARGB8888;
		bytes_per_pixel = 4U;
		break;
	case PIXEL_FORMAT_BGR_565:
		fbConfig.inOrder = kLCDIF_PixelInputOrderABGR;
	case PIXEL_FORMAT_RGB_565:
		fbConfig.format = kLCDIF_PixelFormatRGB565;
		bytes_per_pixel = 2U;
		break;
	default:
		LOG_ERR("Bus tyoe not supported.");
		ret = -ENODEV;
		break;
	}

	if (ret) {
		return ret;
	}

	fbConfig.alpha.enable	 = false;
	fbConfig.colorkey.enable = false;
	fbConfig.topLeftX	 = 0U;
	fbConfig.topLeftY	 = 0U;
	fbConfig.width		 = desc->width;
	fbConfig.height = desc->height;

	LCDIF_SetFrameBufferConfig(config->base, 0, &fbConfig);

	if (bytes_per_pixel == 3U) {
		/* For RGB888 the stride shall be calculated as
		 * 4 bytes per pixel instead of 3.
		 */
		LCDIF_SetFrameBufferStride(config->base, 0, 4U * desc->pitch);
	} else {
		LCDIF_SetFrameBufferStride(config->base, 0, bytes_per_pixel * desc->pitch);
	}

	LCDIF_SetFrameBufferPosition(config->base, 0U, 0U, 0U, desc->width,
								desc->height);

	LCDIF_DbiSelectArea(config->base, 0, 0, 0, desc->width - 1U,
						desc->height - 1U, false);

	LCDIF_SetFrameBufferAddr(config->base, 0, (uint32_t)framebuf);

	/* Send command: Memory write start. */
	if (desc->first_write) {
		LCDIF_DbiSendCommand(config->base, 0, MIPI_DCS_WRITE_MEMORY_START);
	} else {
		LCDIF_DbiSendCommand(config->base, 0, MIPI_DCS_WRITE_MEMORY_CONTINUE);
	}

	/* Enable DMA and send out data. */
	LCDIF_DbiWriteMem(config->base, 0);

	/* Wait for transfer done. */
	k_sem_take(&lcdif_data->transfer_done, K_FOREVER);

	return 0;
}

static int mipi_dbi_dcnano_lcdif_command_write(const struct device *dev,
						   const struct mipi_dbi_config *dbi_config,
						   uint8_t cmd, const uint8_t *data_buf,
						   size_t len)
{
	const struct mcux_dcnano_lcdif_dbi_config *config = dev->config;

	/* The bus of dbi_config shall be set in init. */
	ARG_UNUSED(dbi_config);

	LCDIF_DbiSendCommand(config->base, 0U, cmd);

	if (len != 0U) {
		LCDIF_DbiSendData(config->base, 0U, data_buf, len);
	}

	return kStatus_Success;
}

static int mipi_dbi_dcnano_lcdif_reset(const struct device *dev, k_timeout_t delay)
{
	int ret;

	const struct mcux_dcnano_lcdif_dbi_config *config = dev->config;

	/* Check if a reset port is provided to reset the LCD controller */
	if (config->reset.port == NULL) {
		return 0;
	}

	/* Reset the LCD controller. */
	ret = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_HIGH);
	if (ret) {
		return ret;
	}

	ret = gpio_pin_set_dt(&config->reset, 0);
	if (ret < 0) {
		return ret;
	}

	k_sleep(delay);

	ret = gpio_pin_set_dt(&config->reset, 1);
	if (ret < 0) {
		return ret;
	}

	LOG_DBG("%s device reset complete", dev->name);

	return 0;
}

static struct mipi_dbi_driver_api mcux_dcnano_lcdif_dbi_api = {
	.reset = mipi_dbi_dcnano_lcdif_reset,
	.command_write = mipi_dbi_dcnano_lcdif_command_write,
	.write_display = mipi_dbi_dcnano_lcdif_write_display,
};

#define MCUX_DCNANO_LCDIF_DEVICE_INIT(n)						\
	static void mcux_dcnano_lcdif_dbi_config_func_##n(const struct device *dev)	\
	{										\
		IRQ_CONNECT(DT_INST_IRQN(n),						\
				DT_INST_IRQ(n, priority),				\
				mcux_dcnano_lcdif_dbi_isr,				\
				DEVICE_DT_INST_GET(n),					\
				0);							\
		irq_enable(DT_INST_IRQN(n));						\
	}										\
	PINCTRL_DT_INST_DEFINE(n);							\
	struct mcux_dcnano_lcdif_dbi_data mcux_dcnano_lcdif_dbi_data_##n;		\
	struct mcux_dcnano_lcdif_dbi_config mcux_dcnano_lcdif_dbi_config_##n = {	\
		.base = (LCDIF_Type *) DT_INST_REG_ADDR(n),				\
		.irq_config_func = mcux_dcnano_lcdif_dbi_config_func_##n,		\
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),				\
		.reset = GPIO_DT_SPEC_INST_GET_OR(n, reset_gpios, {0}),			\
		.dbi_config = {								\
			.type =								\
			((DT_INST_PROP(n, mipi_mode) >= MIPI_DBI_MODE_8080_BUS_16_BIT) ?\
			 kLCDIF_DbiTypeB :						\
			((DT_INST_PROP(n, mipi_mode) >= MIPI_DBI_MODE_6800_BUS_16_BIT) ?\
			  kLCDIF_DbiTypeA_FixedE : 0xFF)),				\
			.swizzle = DT_INST_ENUM_IDX_OR(n, swizzle, 0),			\
			.format = LCDIF_DBICONFIG0_DBI_DATA_FORMAT(			\
					DT_INST_ENUM_IDX(n, color_coding)),		\
			.acTimeUnit = DT_INST_PROP_OR(n, divider, 1) - 1,		\
			.writeWRPeriod = DT_INST_PROP(n, wr_period),			\
			.writeWRAssert = DT_INST_PROP(n, wr_assert),			\
			.writeWRDeassert = DT_INST_PROP(n, wr_deassert),		\
			.writeCSAssert = DT_INST_PROP(n, cs_assert),			\
			.writeCSDeassert = DT_INST_PROP(n, cs_deassert),		\
		},									\
		.panel_config = {							\
			.enable = true,							\
			.enableGamma = false,						\
			.order = kLCDIF_VideoOverlay0Overlay1,				\
			.endian = DT_INST_ENUM_IDX_OR(n, endian, 0),			\
		},									\
	};										\
	DEVICE_DT_INST_DEFINE(n,							\
		&mcux_dcnano_lcdif_dbi_init,						\
		NULL,									\
		&mcux_dcnano_lcdif_dbi_data_##n,					\
		&mcux_dcnano_lcdif_dbi_config_##n,					\
		POST_KERNEL,								\
		CONFIG_MIPI_DBI_INIT_PRIORITY,						\
		&mcux_dcnano_lcdif_dbi_api);

DT_INST_FOREACH_STATUS_OKAY(MCUX_DCNANO_LCDIF_DEVICE_INIT)
