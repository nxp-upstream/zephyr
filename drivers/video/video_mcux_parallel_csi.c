/*
 * Copyright (c) 2023, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx93_parallel_csi

#include <zephyr/kernel.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/clock_control.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(parallel_csi, CONFIG_VIDEO_LOG_LEVEL);

#include <fsl_common.h>

#define DATA_TYPE_OUT_NULL				0x00
#define DATA_TYPE_OUT_RGB				0x04
#define DATA_TYPE_OUT_YUV444				0x08
#define DATA_TYPE_OUT_YYU420_ODD			0x10
#define DATA_TYPE_OUT_YYU420_EVEN			0x12
#define DATA_TYPE_OUT_YYY_ODD				0x18
#define DATA_TYPE_OUT_UYVY_EVEN				0x1A
#define DATA_TYPE_OUT_RAW				0x1C

#define DATA_TYPE_IN_UYVY_BT656_8BITS			0x0
#define DATA_TYPE_IN_UYVY_BT656_10BITS			0x1
#define DATA_TYPE_IN_RGB_8BITS				0x2
#define DATA_TYPE_IN_BGR_8BITS				0x3
#define DATA_TYPE_IN_YUV422_YVYU_8BITS			0x5
#define DATA_TYPE_IN_YUV444_YUV_8BITS			0x6
#define DATA_TYPE_IN_BAYER_8BITS			0x9
#define DATA_TYPE_IN_BAYER_10BITS			0xA

struct video_mcux_pcsi_config {
	MEDIAMIX_BLK_CTRL_Type *base;
	struct device *sensor_dev;
	struct device *cam_pix_clk_dev;
	clock_control_subsys_t cam_pix_clk_subsys;
	struct _clock_root_config_t cam_pix_clk_cfg;
};

struct video_mcux_pcsi_data {
	const struct device *dev;
	struct video_format fmt;
	uint8_t in_data_type;
	uint8_t hsync_pol;
	uint8_t vsync_pol;
	uint8_t pclk_pol;
	bool uv_swap;
};

#ifdef DEBUG
static void dump_pcsi_regs(MEDIAMIX_BLK_CTRL_Type *base)
{
	LOG_DBG("RESET[0x0]: 0x%08x", base->CLK_RESETN.RESET);
	LOG_DBG("CLK[0x4]: 0x%08x", base->CLK_RESETN.CLK);
	LOG_DBG("ISI0[0x14]: 0x%08x", base->BUS_CONTROL.ISI0);
	LOG_DBG("ISI1[0x1C]: 0x%08x", base->BUS_CONTROL.ISI1);
	LOG_DBG("CAMERA_MUX[0x30]: 0x%08x", base->GASKET.CAMERA_MUX);
	LOG_DBG("PIXEL_CTRL[0x3C]: 0x%08x", base->GASKET.PIXEL_CTRL);
	LOG_DBG("IF_CTRL_REG[0x70]: 0x%08x", base->GASKET.IF_CTRL_REG);
	LOG_DBG("INTERFACE_STATUS[0x74]: 0x%08x", base->GASKET.INTERFACE_STATUS);
	LOG_DBG("INTERFACE_CTRL_REG[0x78]: 0x%08x", base->GASKET.INTERFACE_CTRL_REG);
	LOG_DBG("INTERFACE_CTRL_REG1[0x7C]: 0x%08x", base->GASKET.INTERFACE_CTRL_REG1);
}
#else
static void dump_pcsi_regs(MEDIAMIX_BLK_CTRL_Type *base) {}
#endif

static void mcux_pcsi_sw_reset(MEDIAMIX_BLK_CTRL_Type *base)
{
	uint32_t reg;

	reg = base->GASKET.INTERFACE_CTRL_REG;
	reg |= MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_SOFTRST_MASK;
	base->GASKET.INTERFACE_CTRL_REG = reg;

	k_msleep(1);

	reg = base->GASKET.INTERFACE_CTRL_REG;
	reg &= ~MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_SOFTRST_MASK;
	base->GASKET.INTERFACE_CTRL_REG = reg;
}

static void mcux_pcsi_init_config(const struct device *dev)
{
	const struct video_mcux_pcsi_config *config = dev->config;
	struct video_mcux_pcsi_data *data = dev->data;
	MEDIAMIX_BLK_CTRL_Type *base = config->base;
	uint32_t reg;

	/* Software Reset */
	mcux_pcsi_sw_reset(base);

	/* Config PL Data Type */
	reg = base->GASKET.IF_CTRL_REG;
	reg |= MEDIAMIX_BLK_CTRL_IF_CTRL_REG_DATA_TYPE(DATA_TYPE_OUT_YUV444);
	base->GASKET.IF_CTRL_REG = reg;

	/* Config INTERFACE_CTRL_REG*/
	reg = base->GASKET.INTERFACE_CTRL_REG;
	reg |= (MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_HSYNC_FORCE_EN_MASK |
		MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_VSYNC_FORCE_EN_MASK);

	reg |= (MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_DATA_TYPE_IN(data->in_data_type) |
			MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_VSYNC_POL(data->vsync_pol) |
			MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_HSYNC_POL(data->hsync_pol) |
			MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_PIXEL_CLK_POL(data->pclk_pol) |
			MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_MASK_VSYNC_CNTR(3) |
			MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_HSYNC_PULSE(2));

	if (data->uv_swap)
		reg |= MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_UV_SWAP_EN_MASK;

	reg |= MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_GCLK_MODE_EN_MASK;
	base->GASKET.INTERFACE_CTRL_REG = reg;

	/* Config INTERFACE_CTRL_REG1*/
	reg = base->GASKET.INTERFACE_CTRL_REG1;
	reg |= (MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG1_PIXEL_WIDTH(data->fmt.width - 1) |
			MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG1_VSYNC_PULSE(10));
	base->GASKET.INTERFACE_CTRL_REG1 = reg;
}

static void mcux_pcsi_enable_csi(MEDIAMIX_BLK_CTRL_Type *base)
{
	uint32_t reg;

	/* Enable CSI */
	reg = base->GASKET.INTERFACE_CTRL_REG;
	reg |= MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_CSI_EN_MASK;
	base->GASKET.INTERFACE_CTRL_REG = reg;

	/* Disable H/VSYNC Force */
	reg = base->GASKET.INTERFACE_CTRL_REG;
	reg &= ~(MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_HSYNC_FORCE_EN_MASK |
			MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_VSYNC_FORCE_EN_MASK);
	base->GASKET.INTERFACE_CTRL_REG = reg;

	/* Gasket Source from Parallel CSI */
	reg = base->GASKET.CAMERA_MUX;
	reg |= MEDIAMIX_BLK_CTRL_CAMERA_MUX_SOURCE_TYPE_MASK;
	base->GASKET.CAMERA_MUX = reg;

}

static void mcux_pcsi_disable_csi(MEDIAMIX_BLK_CTRL_Type *base)
{
	uint32_t reg;

	/* Enable H/VSYNC Force */
	reg = base->GASKET.INTERFACE_CTRL_REG;
	reg |= (MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_HSYNC_FORCE_EN_MASK |
			MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_VSYNC_FORCE_EN_MASK);
	base->GASKET.INTERFACE_CTRL_REG = reg;

	/* Disable CSI */
	reg = base->GASKET.INTERFACE_CTRL_REG;
	reg &= ~MEDIAMIX_BLK_CTRL_INTERFACE_CTRL_REG_CSI_EN_MASK;
	base->GASKET.INTERFACE_CTRL_REG = reg;
}

static int video_mcux_pcsi_set_fmt(const struct device *dev,
				enum video_endpoint_id ep,
				struct video_format *fmt)
{
	const struct video_mcux_pcsi_config *config = dev->config;
	int ret = -ENODEV;

	if (config->sensor_dev) {
		ret = video_set_format(config->sensor_dev, ep, fmt);
	}

	return ret;
}

static int video_mcux_pcsi_get_fmt(const struct device *dev,
				enum video_endpoint_id ep,
				struct video_format *fmt)
{
	const struct video_mcux_pcsi_config *config = dev->config;
	struct video_mcux_pcsi_data *data = dev->data;
	int ret = -ENODEV;

	if (config->sensor_dev) {
		ret = video_get_format(config->sensor_dev, ep, fmt);
		if (ret)
			return ret;
		data->fmt = *fmt;

		data->in_data_type = DATA_TYPE_IN_YUV422_YVYU_8BITS;
		data->hsync_pol = 0;
		data->vsync_pol = 0;
		data->pclk_pol = 0;
		data->uv_swap = true;
	}

	return ret;
}

static int video_mcux_pcsi_get_caps(const struct device *dev,
				enum video_endpoint_id ep,
				struct video_caps *caps)
{
	const struct video_mcux_pcsi_config *config = dev->config;
	int ret = -ENODEV;

	if (config->sensor_dev) {
		ret = video_get_caps(config->sensor_dev, ep, caps);
	}

	return ret;
}

static int video_mcux_pcsi_stream_start(const struct device *dev)
{
	const struct video_mcux_pcsi_config *config = dev->config;
	struct video_format fmt;
	int ret = -ENODEV;

	LOG_DBG("enter %s", __func__);

	memset(&fmt, 0, sizeof(fmt));
	video_mcux_pcsi_get_fmt(dev, VIDEO_EP_OUT, &fmt);
	mcux_pcsi_sw_reset(config->base);
	mcux_pcsi_init_config(dev);
	mcux_pcsi_enable_csi(config->base);
	dump_pcsi_regs(config->base);

	if (config->sensor_dev) {
		ret = video_stream_start(config->sensor_dev);
		if (ret)
			LOG_ERR("sensor dev start stream failed");
	}

	return ret;
}

static int video_mcux_pcsi_stream_stop(const struct device *dev)
{
	const struct video_mcux_pcsi_config *config = dev->config;
	int ret = -ENODEV;

	LOG_DBG("enter %s", __func__);

	if (config->sensor_dev) {
		ret = video_stream_stop(config->sensor_dev);
		if (ret)
			LOG_ERR("sensor dev stop stream failed");
		mcux_pcsi_disable_csi(config->base);
	}

	return ret;
}

static const struct video_driver_api video_mcux_pcsi_driver_api = {
	.set_format = video_mcux_pcsi_set_fmt,
	.get_format = video_mcux_pcsi_get_fmt,
	.get_caps = video_mcux_pcsi_get_caps,
	.stream_start = video_mcux_pcsi_stream_start,
	.stream_stop = video_mcux_pcsi_stream_stop,
};

static const struct video_mcux_pcsi_config video_mcux_pcsi_config_0 = {
	.base = (MEDIAMIX_BLK_CTRL_Type *)DT_REG_ADDR(DT_INST_PARENT(0)),
	.sensor_dev = DEVICE_DT_GET(DT_INST_PHANDLE(0, sensor)),
	.cam_pix_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(0)),
	.cam_pix_clk_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(0, name),
	.cam_pix_clk_cfg = {
		.clockOff = false,
		.mux = DT_INST_CLOCKS_CELL(0, mux),
		.div = DT_INST_CLOCKS_CELL(0, div),
	},
};

static struct video_mcux_pcsi_data video_mcux_pcsi_data_0;

static int video_mcux_pcsi_configure_clock(const struct device *dev)
{
	const struct video_mcux_pcsi_config *config = dev->config;
	enum clock_control_status clk_status;
	uint32_t clk_freq;
	int ret;

	/* configure cam_pix_clk */
	if (!device_is_ready(config->cam_pix_clk_dev)) {
		LOG_ERR("cam_pix clock control device not ready");
		return -ENODEV;
	}

	clock_control_configure(config->cam_pix_clk_dev, config->cam_pix_clk_subsys,
			&config->cam_pix_clk_cfg);

	clk_status = clock_control_get_status(config->cam_pix_clk_dev, config->cam_pix_clk_subsys);
	if (clk_status != CLOCK_CONTROL_STATUS_ON) {
		if (clk_status == CLOCK_CONTROL_STATUS_OFF) {
			ret = clock_control_on(config->cam_pix_clk_dev, config->cam_pix_clk_subsys);
			if (ret) {
				LOG_ERR("cam_pix clock can't be enabled");
				return ret;
			}
		}
		else
			return -EINVAL;
	}

	if (clock_control_get_rate(config->cam_pix_clk_dev, config->cam_pix_clk_subsys, &clk_freq)) {
		return -EINVAL;
	}
	LOG_DBG("cam_pix clock frequency %d", clk_freq);

	return 0;
}

static int video_mcux_pcsi_init_0(const struct device *dev)
{
	struct video_mcux_pcsi_data *data = dev->data;
	const struct video_mcux_pcsi_config *config = dev->config;
	int ret;

	data->dev = dev;

	/* check if there is any sensor device */
	if (!device_is_ready(config->sensor_dev)) {
		LOG_ERR("sensor device %s not ready", config->sensor_dev->name);
		LOG_ERR("%s init failed", dev->name);
		return -ENODEV;
	}

	ret = video_mcux_pcsi_configure_clock(dev);
	if (ret) {
		LOG_ERR("%s configure clock failed", dev->name);
		return ret;
	}

	LOG_INF("%s init succeeded, source from %s", dev->name, config->sensor_dev->name);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, &video_mcux_pcsi_init_0, NULL,
		    &video_mcux_pcsi_data_0,
		    &video_mcux_pcsi_config_0,
		    POST_KERNEL, CONFIG_VIDEO_PARALLEL_CSI_INIT_PRIORITY,
		    &video_mcux_pcsi_driver_api);
