/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_mipi_dsi_dw

#include <zephyr/drivers/mipi_dsi.h>
#include <fsl_mipi_dsi.h>
#include <fsl_clock.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/logging/log.h>
#include <soc.h>

LOG_MODULE_REGISTER(dsi_mcux, CONFIG_MIPI_DSI_LOG_LEVEL);

#define MIPI_DPHY_REF_CLK DT_INST_PROP(0, dphy_ref_frequency)

/* Required by DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(_dev)  ((const struct display_mcux_mipi_dsi_config *)(_dev)->config)
#define DEV_DATA(_dev) ((struct display_mcux_mipi_dsi_data *)(_dev)->data)

struct display_mcux_mipi_dsi_config {
	DEVICE_MMIO_NAMED_ROM(reg_base);

	const struct device *phy_cfg_clk_dev;
	clock_control_subsys_t phy_cfg_clk_subsys;
	struct _clock_root_config_t phy_cfg_clk_config;

	dsi_dpi_config_t dpi_config;
	dsi_config_t dsi_config;
	dsi_command_config_t command_config;
	uint32_t dphy_ref_frequency;
	uint32_t phy_clock;
};

struct display_mcux_mipi_dsi_data {
	DEVICE_MMIO_NAMED_RAM(reg_base);
	const struct device *dev;
};

static void dump_reg(MIPI_DSI_Type *base)
{
	LOG_DBG("VERSION:0x%x", base->VERSION);
	LOG_DBG("PWR_UP:0x%x", base->PWR_UP);
	LOG_DBG("DPI_VCID:0x%x", base->DPI_VCID);
	LOG_DBG("DPI_COLOR_CODING:0x%x", base->DPI_COLOR_CODING);
	LOG_DBG("DPI_LP_CMD_TIM:0x%x", base->DPI_LP_CMD_TIM);
	LOG_DBG("PCKHDL_CFG:0x%x", base->PCKHDL_CFG);
	LOG_DBG("MODE_CFG:0x%x", base->MODE_CFG);
	LOG_DBG("VID_MODE_CFG:0x%x", base->VID_MODE_CFG);
	LOG_DBG("VID_PKT_SIZE:0x%x", base->VID_PKT_SIZE);
	LOG_DBG("VID_NUM_CHUNKS:0x%x", base->VID_NUM_CHUNKS);
	LOG_DBG("VID_HSA_TIME:0x%x", base->VID_HSA_TIME);
	LOG_DBG("VID_HBP_TIME:0x%x", base->VID_HBP_TIME);
	LOG_DBG("VID_HLINE_TIME:0x%x", base->VID_HLINE_TIME);
	LOG_DBG("VID_VSA_LINES:0x%x", base->VID_VSA_LINES);
	LOG_DBG("VID_VBP_LINES:0x%x", base->VID_VBP_LINES);
	LOG_DBG("VID_VFP_LINES:0x%x", base->VID_VFP_LINES);
	LOG_DBG("VID_VACTIVE_LINES:0x%x", base->VID_VACTIVE_LINES);
	LOG_DBG("CMD_MODE_CFG:0x%x", base->CMD_MODE_CFG);
	LOG_DBG("HS_RD_TO_CNT:0x%x", base->HS_RD_TO_CNT);
	LOG_DBG("LP_RD_TO_CNT:0x%x", base->LP_RD_TO_CNT);
	LOG_DBG("HS_WR_TO_CNT:0x%x", base->HS_WR_TO_CNT);
	LOG_DBG("LP_WR_TO_CNT:0x%x", base->LP_WR_TO_CNT);
	LOG_DBG("PHY_TMR_LPCLK_CFG:0x%x", base->PHY_TMR_LPCLK_CFG);
	LOG_DBG("PHY_TMR_CFG:0x%x", base->PHY_TMR_CFG);
	LOG_DBG("PHY_RSTZ:0x%x", base->PHY_RSTZ);
	LOG_DBG("PHY_IF_CFG:0x%x", base->PHY_IF_CFG);
	LOG_DBG("PHY_STATUS:0x%x", base->PHY_STATUS);

	LOG_DBG("BLK_CTRL_MEDIAMIX->MIPI.DSI_W0:0x%x", BLK_CTRL_MEDIAMIX->MIPI.DSI_W0);
	LOG_DBG("BLK_CTRL_MEDIAMIX->MIPI.DSI_W1:0x%x", BLK_CTRL_MEDIAMIX->MIPI.DSI_W1);
	LOG_DBG("BLK_CTRL_MEDIAMIX->MIPI.DSI:0x%x", BLK_CTRL_MEDIAMIX->MIPI.DSI);
}

static int dsi_mcux_attach(const struct device *dev, uint8_t channel,
			   const struct mipi_dsi_device *mdev)
{
	MIPI_DSI_Type *base = (MIPI_DSI_Type *)DEVICE_MMIO_NAMED_GET(dev, reg_base);
	const struct display_mcux_mipi_dsi_config *config = dev->config;
	dsi_dpi_config_t dpi_config = config->dpi_config;
	dsi_config_t dsi_config = config->dsi_config;
	dsi_command_config_t command_config = config->command_config;
	uint32_t m;
	uint32_t n;
	uint32_t vco_freq;

	if (mdev->mode_flags & MIPI_DSI_MODE_VIDEO) {
		dsi_config.mode = kDSI_VideoMode;
	} else {
		dsi_config.mode = kDSI_CommandMode;
	}
	/* Init the DSI module. */
	DSI_Init(base, &dsi_config);

	DSI_SetDpiConfig(base, &dpi_config, mdev->data_lanes);

	uint32_t phyByteClkFreq_Hz = config->phy_clock * mdev->data_lanes / 8;

	DSI_SetCommandModeConfig(base, &command_config, phyByteClkFreq_Hz);

	if (config->phy_clock / 2 >= MHZ(320)) {
		vco_freq = config->phy_clock / 2 * 1;
	} else if (config->phy_clock / 2 >= MHZ(160)) {
		vco_freq = config->phy_clock / 2 * 2;
	} else if (config->phy_clock / 2 >= MHZ(80)) {
		vco_freq = config->phy_clock / 2 * 4;
	} else {
		vco_freq = config->phy_clock / 2 * 8;
	}
	/* Get the divider value to set to the mediamix block. */
	DSI_DphyGetPllDivider(&m, &n, MIPI_DPHY_REF_CLK, vco_freq);

	LOG_INF("DPHY clock set to %u, m=%d, n=%d, target=%d",
		MIPI_DPHY_REF_CLK * (m + 2) / (n + 1), m, n, vco_freq);

#if CONFIG_SOC_MIMX9352_A55
	/* MEDIAMIX */
	/* Clear the bit to reset the clock logic */
	BLK_CTRL_MEDIAMIX->CLK_RESETN.RESET &= ~(MEDIAMIX_BLK_CTRL_RESET_dsi_apb_en_MASK |
						 MEDIAMIX_BLK_CTRL_RESET_ref_clk_en_MASK);
	BLK_CTRL_MEDIAMIX->CLK_RESETN.RESET |=
		(MEDIAMIX_BLK_CTRL_RESET_dsi_apb_en_MASK | MEDIAMIX_BLK_CTRL_RESET_ref_clk_en_MASK);
	BLK_CTRL_MEDIAMIX->CLK_RESETN.CLK =
		MEDIAMIX_BLK_CTRL_RESET_cam_clk_en(1) | MEDIAMIX_BLK_CTRL_RESET_csi_apb_en(1) |
		MEDIAMIX_BLK_CTRL_RESET_pxp_axi_en(1) | MEDIAMIX_BLK_CTRL_RESET_pxp_apb_en(1) |
		MEDIAMIX_BLK_CTRL_RESET_isi_proc_en(1) | MEDIAMIX_BLK_CTRL_RESET_isi_apb_en(1);

	BLK_CTRL_MEDIAMIX->MIPI.DSI_W0 =
		MEDIAMIX_BLK_CTRL_DSI_W0_PROP_CNTRL(
			Pll_Set_Pll_Prop_Param(config->phy_clock / MHZ(2))) |
		MEDIAMIX_BLK_CTRL_DSI_W0_VCO_CNTRL(
			Pll_Set_Pll_Vco_Param(config->phy_clock / MHZ(2))) |
		MEDIAMIX_BLK_CTRL_DSI_W0_N(n) | MEDIAMIX_BLK_CTRL_DSI_W0_M(m);

	BLK_CTRL_MEDIAMIX->MIPI.DSI_W1 =
		MEDIAMIX_BLK_CTRL_DSI_W1_CPBIAS_CNTRL(0x10) | MEDIAMIX_BLK_CTRL_DSI_W1_GMP_CNTRL(1);

#endif
	dsi_dphy_config_t phy_config;

	DSI_GetDefaultDphyConfig(&phy_config, phyByteClkFreq_Hz, mdev->data_lanes);
	DSI_InitDphy(base, &phy_config);

#if CONFIG_SOC_MIMX9352_A55
	BLK_CTRL_MEDIAMIX->MIPI.DSI =
		MEDIAMIX_BLK_CTRL_DSI_updatepll(1) |
		MEDIAMIX_BLK_CTRL_DSI_HSFREQRANGE(Pll_Set_Hs_Freqrange(config->phy_clock)) |
		MEDIAMIX_BLK_CTRL_DSI_CLKSEL(1) | MEDIAMIX_BLK_CTRL_DSI_CFGCLKFREQRANGE(0x1c);

#endif
	int result = DSI_PowerUp(base);

	if (result < 0) {
		LOG_ERR("DSI PHY init failed.\r\n");
	} else {
		LOG_INF("%s succeeded\n", __func__);
	}
#if DUMP_REG
	dump_reg(base);
#endif
	return 0;
}

static ssize_t dsi_mcux_transfer(const struct device *dev, uint8_t channel,
				 struct mipi_dsi_msg *msg)
{
	MIPI_DSI_Type *base = (MIPI_DSI_Type *)DEVICE_MMIO_NAMED_GET(dev, reg_base);
	dsi_transfer_t dsi_xfer = {0};
	status_t status;

	dsi_xfer.virtualChannel = channel;
	dsi_xfer.txDataSize = msg->tx_len;
	dsi_xfer.txData = msg->tx_buf;
	dsi_xfer.rxDataSize = msg->rx_len;
	dsi_xfer.rxData = msg->rx_buf;

	switch (msg->type) {

	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		dsi_xfer.txDataType = kDSI_TxDataGenShortWrNoParam;
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		dsi_xfer.txDataType = kDSI_TxDataGenShortWrOneParam;
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		dsi_xfer.txDataType = kDSI_TxDataGenShortWrTwoParam;
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		dsi_xfer.txDataType = kDSI_TxDataGenLongWr;
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
		__fallthrough;
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
		__fallthrough;
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		LOG_ERR("Generic Read not yet implemented or used");
		return -ENOTSUP;
	default:
		LOG_ERR("Unsupported message type (%d)", msg->type);
		return -ENOTSUP;
	}

	status = DSI_TransferBlocking(base, &dsi_xfer);

	if (status != kStatus_Success) {
		LOG_ERR("Transmission failed");
		return -EIO;
	}

	if (msg->rx_len != 0) {
		/* Return rx_len on a read */
		return dsi_xfer.rxDataSize;
	}

	/* Return tx_len on a write */
	return dsi_xfer.txDataSize;
}

static struct mipi_dsi_driver_api dsi_mcux_api = {
	.attach = dsi_mcux_attach,
	.transfer = dsi_mcux_transfer,
};

static int display_mcux_mipi_dsi_init(const struct device *dev)
{
	const struct display_mcux_mipi_dsi_config *config = dev->config;

	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE | K_MEM_DIRECT_MAP);

	clock_control_set_rate(config->phy_cfg_clk_dev, config->phy_cfg_clk_subsys,
			       (clock_control_subsys_rate_t)MIPI_DPHY_REF_CLK);

	return 0;
}

#define MCUX_DSI_DPI_CONFIG(id)                                                                    \
	IF_ENABLED(DT_NODE_HAS_PROP(DT_DRV_INST(id), nxp_lcdif),				\
	(.dpi_config = {									\
		.virtualChannel   = 0U,								\
		.colorCoding = DT_INST_ENUM_IDX(id, dpi_color_coding),				\
		.enableAck        = false,							\
		 .enablelpSwitch   = true,							\
		.pattern          = kDSI_PatternDisable,					\
		.videoMode = DT_INST_ENUM_IDX(id, dpi_video_mode),				\
		.pixelPayloadSize = DT_INST_PROP_BY_PHANDLE(id, nxp_lcdif, width),		\
		.panelHeight = DT_INST_PROP_BY_PHANDLE(id, nxp_lcdif, height),			\
		.polarityFlags = kDSI_DpiVsyncActiveLow | kDSI_DpiHsyncActiveLow,		\
		.hfp = DT_PROP(DT_CHILD(DT_INST_PHANDLE(id, nxp_lcdif),                         \
					display_timings), hfront_porch),                        \
		.hbp = DT_PROP(DT_CHILD(DT_INST_PHANDLE(id, nxp_lcdif),                         \
					display_timings), hback_porch),                         \
		.hsw = DT_PROP(DT_CHILD(DT_INST_PHANDLE(id, nxp_lcdif),                         \
					display_timings), hsync_len),                           \
		.vfp = DT_PROP(DT_CHILD(DT_INST_PHANDLE(id, nxp_lcdif),                         \
					display_timings), vfront_porch),                        \
		.vbp = DT_PROP(DT_CHILD(DT_INST_PHANDLE(id, nxp_lcdif),                         \
					display_timings), vback_porch),                         \
		.vsw = DT_PROP(DT_CHILD(DT_INST_PHANDLE(id, nxp_lcdif),                         \
					display_timings), vsync_len),                           \
	},))

#define MCUX_MIPI_DSI_DEVICE(id)                                                                   \
	static const struct display_mcux_mipi_dsi_config display_mcux_mipi_dsi_config_##id = {     \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(id)),                             \
		.phy_cfg_clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(id, 0)),               \
		.phy_cfg_clk_subsys =                                                              \
			(clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(id, 0, name),           \
		.phy_cfg_clk_config =                                                              \
			{                                                                          \
				.clockOff = false,                                                 \
			},                                                                         \
		MCUX_DSI_DPI_CONFIG(id).dsi_config =                                               \
			{                                                                          \
				.mode = kDSI_VideoMode,                                            \
				.packageFlags =                                                    \
					kDSI_DpiEnableBta | kDSI_DpiEnableEcc | kDSI_DpiEnableCrc, \
				.enableNoncontinuousClk = false,                                   \
				.HsRxDeviceReady_ByteClk = 0U,                                     \
				.lpRxDeviceReady_ByteClk = 0U,                                     \
				.HsTxDeviceReady_ByteClk = 0U,                                     \
				.lpTxDeviceReady_ByteClk = 0U,                                     \
			},                                                                         \
		.command_config =                                                                  \
			{                                                                          \
				.escClkFreq_Hz = 20000000,                                         \
				.btaTo_Ns = 10000,                                                 \
				.hsTxTo_Ns = 60000,                                                \
				.lpRxTo_Ns = 60000,                                                \
			},                                                                         \
		.phy_clock = DT_INST_PROP(id, phy_clock),                                          \
	};                                                                                         \
	static struct display_mcux_mipi_dsi_data display_mcux_mipi_dsi_data_##id;                  \
	DEVICE_DT_INST_DEFINE(id, &display_mcux_mipi_dsi_init, NULL,                               \
			      &display_mcux_mipi_dsi_data_##id,                                    \
			      &display_mcux_mipi_dsi_config_##id, POST_KERNEL,                     \
			      CONFIG_MIPI_DSI_INIT_PRIORITY, &dsi_mcux_api);

DT_INST_FOREACH_STATUS_OKAY(MCUX_MIPI_DSI_DEVICE)
