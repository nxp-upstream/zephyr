/*
 * Copyright (c) 2019, Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx93_ldb
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/sys/byteorder.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(imx93_ldb, CONFIG_DISPLAY_LOG_LEVEL);

/* LDB Control Register */
#define LDB_CH0_MODE_EN_TO_DI0          (1 << 0)
#define LDB_CH0_MODE_EN_TO_DI1          (3 << 0)
#define LDB_CH0_MODE_EN_MASK            (3 << 0)
#define LDB_CH1_MODE_EN_TO_DI0          (1 << 2)
#define LDB_CH1_MODE_EN_TO_DI1          (3 << 2)
#define LDB_CH1_MODE_EN_MASK            (3 << 2)
#define LDB_SPLIT_MODE_EN               (1 << 4)
#define LDB_DATA_WIDTH_CH0_24           (1 << 5)
#define LDB_BIT_MAP_CH0_JEIDA           (1 << 6)
#define LDB_DATA_WIDTH_CH1_24           (1 << 7)
#define LDB_BIT_MAP_CH1_JEIDA           (1 << 8)
#define LDB_DI0_VS_POL_ACT_LOW          (1 << 9)
#define LDB_DI1_VS_POL_ACT_LOW          (1 << 10)
#define LDB_REG_CH0_FIFO_RESET          (1 << 11)
#define LDB_REG_ASYNC_FIFO_EN           (1 << 24)
#define LDB_FIFO_THRESHOLD              (4 << 25)



/* LVDS Control Register */
#define SPARE_IN(n)             (((n) & 0x7) << 25)
#define SPARE_IN_MASK           0xe000000
#define TEST_RANDOM_NUM_EN      BIT(24)
#define TEST_MUX_SRC(n)         (((n) & 0x3) << 22)
#define TEST_MUX_SRC_MASK       0xc00000
#define TEST_EN                 BIT(21)
#define TEST_DIV4_EN            BIT(20)
#define VBG_ADJ(n)              (((n) & 0x7) << 17)
#define VBG_ADJ_MASK            0xe0000
#define SLEW_ADJ(n)             (((n) & 0x7) << 14)
#define SLEW_ADJ_MASK           0x1c000
#define CC_ADJ(n)               (((n) & 0x7) << 11)
#define CC_ADJ_MASK             0x3800
#define CM_ADJ(n)               (((n) & 0x7) << 8)
#define CM_ADJ_MASK             0x700
#define PRE_EMPH_ADJ(n)         (((n) & 0x7) << 5)
#define PRE_EMPH_ADJ_MASK       0xe0
#define PRE_EMPH_EN             BIT(4)
#define HS_EN                   BIT(3)
#define BG_EN                   BIT(2)
#define DISABLE_LVDS            BIT(1)
#define CH_EN(id)               BIT(id)

enum imx93_ldb_bus_fmt {
	MEDIA_BUS_FMT_RGB666_1X7X3_SPWG 	= BIT(0),
	MEDIA_BUS_FMT_RGB888_1X7X4_SPWG		= BIT(1),
	MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA 	= BIT(2),
};

struct imx93_ldb_config {
	MEDIAMIX_BLK_CTRL_Type *base;
	const struct device *ldb_clk_dev;
	clock_control_subsys_t ldb_clk_subsys;
	struct _clock_root_config_t clk_config;
	enum imx93_ldb_bus_fmt bus_format;
};

static int imx93_ldb_configure_clock(const struct device *dev)
{
	const struct imx93_ldb_config *cfg = dev->config;

	return clock_control_configure(cfg->ldb_clk_dev, cfg->ldb_clk_subsys, (void *)&cfg->clk_config);
}

static int imx93_ldb_enable(const struct device *dev)
{
	const struct imx93_ldb_config *cfg = dev->config;

	cfg->base->BUS_CONTROL.LDB_CTRL &= ~LDB_CH0_MODE_EN_MASK;
	cfg->base->BUS_CONTROL.LDB_CTRL |= LDB_CH0_MODE_EN_TO_DI0;

	return 0;
}

static int imx93_lvds_phy_init(const struct device *dev)
{
	const struct imx93_ldb_config *cfg = dev->config;

	cfg->base->BUS_CONTROL.LVDS = CC_ADJ(0x2) | PRE_EMPH_EN | PRE_EMPH_ADJ(0x3);

	return 0;
}

static int imx93_lvds_phy_power_on(const struct device *dev)
{
	const struct imx93_ldb_config *cfg = dev->config;
	uint32_t val;
	bool bg_en;

	val = cfg->base->BUS_CONTROL.LVDS;
	bg_en = !!(val & BG_EN);
	val &= ~DISABLE_LVDS;
	cfg->base->BUS_CONTROL.LVDS = val;

	/* Wait 15us to make sure the bandgap to be stable. */
	if (!bg_en)
		k_usleep(15);

	val = cfg->base->BUS_CONTROL.LVDS;
	val |= CH_EN(0);
	cfg->base->BUS_CONTROL.LVDS = val;

	/* Wait 5us to ensure the phy be settling. */
        k_usleep(5);

	return 0;
}

static int imx93_display_mode_set(const struct device *dev)
{
	const struct imx93_ldb_config *cfg = dev->config;
	int err = 0;

	cfg->base->BUS_CONTROL.LCDIFr = 0x3712;
	cfg->base->GASKET.DISPLAY_MUX &= ~MEDIAMIX_BLK_CTRL_DISPLAY_MUX_PARALLEL_DISP_FORMAT_MASK;
	switch (cfg->bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		cfg->base->GASKET.DISPLAY_MUX 	|= MEDIAMIX_BLK_CTRL_DISPLAY_MUX_PARALLEL_DISP_FORMAT(1);
		cfg->base->BUS_CONTROL.LDB_CTRL 	&= ~LDB_DATA_WIDTH_CH0_24;
		cfg->base->BUS_CONTROL.LDB_CTRL 	&= ~LDB_BIT_MAP_CH0_JEIDA;
		LOG_INF("set RGB666_1X7X3_SPWG in %s", __func__);
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		cfg->base->GASKET.DISPLAY_MUX 	|= MEDIAMIX_BLK_CTRL_DISPLAY_MUX_PARALLEL_DISP_FORMAT(0);
		cfg->base->BUS_CONTROL.LDB_CTRL 	|= LDB_DATA_WIDTH_CH0_24;
		cfg->base->BUS_CONTROL.LDB_CTRL 	&= ~LDB_BIT_MAP_CH0_JEIDA;
		LOG_INF("set RGB888_1X7X4_SPWG in %s", __func__);
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		cfg->base->GASKET.DISPLAY_MUX 	|= MEDIAMIX_BLK_CTRL_DISPLAY_MUX_PARALLEL_DISP_FORMAT(0);
		cfg->base->BUS_CONTROL.LDB_CTRL 	|= LDB_DATA_WIDTH_CH0_24;
		cfg->base->BUS_CONTROL.LDB_CTRL 	|= LDB_BIT_MAP_CH0_JEIDA;
		LOG_INF("set RGB888_1X7X4_JEIDA in %s", __func__);
		break;
		default:
		err = -EINVAL;
	}

	return err;
}

static int imx93_ldb_init(const struct device *dev)
{
	const struct imx93_ldb_config *cfg = dev->config;
	int err = 0;

	/* configure ldb_clk */
	if (!device_is_ready(cfg->ldb_clk_dev)) {
		LOG_ERR("ldb clock control device not ready\n");
		return -ENODEV;
	}
	imx93_ldb_configure_clock(dev);

	uint32_t clk_freq;
	if (clock_control_get_rate(cfg->ldb_clk_dev, cfg->ldb_clk_subsys, &clk_freq)) {
		return -EINVAL;
	}
	LOG_INF("ldb clock frequency %d", clk_freq);

	imx93_ldb_enable(dev);

	imx93_lvds_phy_init(dev);
	imx93_lvds_phy_power_on(dev);

	err = imx93_display_mode_set(dev);
	if(!err)
		LOG_INF("%s init succeeded\n", dev->name);
	else
		LOG_INF("%s init failed\n", dev->name);

	return err;
}

#define GET_MEDIA_BUS_FMT(id)									\
(												\
	(DT_INST_ENUM_IDX(id, bus_format) == 0) ? MEDIA_BUS_FMT_RGB666_1X7X3_SPWG :		\
	((DT_INST_ENUM_IDX(id, bus_format) == 1) ? MEDIA_BUS_FMT_RGB888_1X7X4_SPWG :		\
	 MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA)							\
)												\

#define DISPLAY_MCUX_IMX93_LDB_INIT(id)							\
	static const struct imx93_ldb_config imx93_ldb_config_##id = {			\
		.base = (MEDIAMIX_BLK_CTRL_Type *)DT_REG_ADDR(DT_INST_PARENT(id)),	\
		.ldb_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(id)),			\
		.ldb_clk_subsys =							\
		(clock_control_subsys_t)DT_INST_CLOCKS_CELL(id, name),			\
		.clk_config = {							\
			.clockOff = false,					\
			.mux = DT_INST_CLOCKS_CELL(id, mux),			\
			.div = DT_INST_CLOCKS_CELL(id, div),			\
		},								\
		.bus_format = GET_MEDIA_BUS_FMT(id),					\
	};										\
											\
	DEVICE_DT_INST_DEFINE(id, imx93_ldb_init, NULL,					\
			    NULL, &imx93_ldb_config_##id,				\
			    POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,			\
			    NULL);							\
											\

DT_INST_FOREACH_STATUS_OKAY(DISPLAY_MCUX_IMX93_LDB_INIT)
