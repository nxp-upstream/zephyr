/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include "nxp_imx_mipi_csi_priv.h"

LOG_MODULE_DECLARE(nxp_imx_mipi_csi, CONFIG_VIDEO_LOG_LEVEL);

const struct dw_dphy_reg imx95_dphy_regs[] = {
	[DPHY_RX_CFGCLKFREQRANGE] = PHY_REG(CSR_PHY_FREQ_CTRL, 6, 0),
	[DPHY_RX_HSFREQRANGE] = PHY_REG(CSR_PHY_FREQ_CTRL, 7, 16),
	[DPHY_RX_DATA_LANE_EN] = PHY_REG(CSR_PHY_MODE_CTRL, 4, 4),
	[DPHY_RX_DATA_LANE_BASEDIR] = PHY_REG(CSR_PHY_TEST_MODE_CTRL, 1, 0),
	[DPHY_RX_DATA_LANE_FORCERXMODE] = PHY_REG(CSR_PHY_TEST_MODE_CTRL, 4, 8),
	[DPHY_RX_ENABLE_CLK_EXT] = PHY_REG(CSR_PHY_TEST_MODE_CTRL, 1, 12),
	[DPHY_RX_PHY_ENABLE_BYP] = PHY_REG(CSR_PHY_TEST_MODE_CTRL, 1, 14),
};

const struct dphy_mbps_hsfreqrange_map hsfreqrange_table[] = {
	{ .mbps = 80,   .hsfreqrange = 0x00 },
	{ .mbps = 90,   .hsfreqrange = 0x10 },
	{ .mbps = 100,  .hsfreqrange = 0x20 },
	{ .mbps = 110,  .hsfreqrange = 0x30 },
	{ .mbps = 120,  .hsfreqrange = 0x01 },
	{ .mbps = 130,  .hsfreqrange = 0x11 },
	{ .mbps = 140,  .hsfreqrange = 0x21 },
	{ .mbps = 150,  .hsfreqrange = 0x31 },
	{ .mbps = 160,  .hsfreqrange = 0x02 },
	{ .mbps = 170,  .hsfreqrange = 0x12 },
	{ .mbps = 180,  .hsfreqrange = 0x22 },
	{ .mbps = 190,  .hsfreqrange = 0x32 },
	{ .mbps = 205,  .hsfreqrange = 0x03 },
	{ .mbps = 220,  .hsfreqrange = 0x13 },
	{ .mbps = 235,  .hsfreqrange = 0x23 },
	{ .mbps = 250,  .hsfreqrange = 0x33 },
	{ .mbps = 275,  .hsfreqrange = 0x04 },
	{ .mbps = 300,  .hsfreqrange = 0x14 },
	{ .mbps = 325,  .hsfreqrange = 0x25 },
	{ .mbps = 350,  .hsfreqrange = 0x35 },
	{ .mbps = 400,  .hsfreqrange = 0x05 },
	{ .mbps = 450,  .hsfreqrange = 0x16 },
	{ .mbps = 500,  .hsfreqrange = 0x26 },
	{ .mbps = 550,  .hsfreqrange = 0x37 },
	{ .mbps = 600,  .hsfreqrange = 0x07 },
	{ .mbps = 650,  .hsfreqrange = 0x18 },
	{ .mbps = 700,  .hsfreqrange = 0x28 },
	{ .mbps = 750,  .hsfreqrange = 0x39 },
	{ .mbps = 800,  .hsfreqrange = 0x09 },
	{ .mbps = 850,  .hsfreqrange = 0x19 },
	{ .mbps = 900,  .hsfreqrange = 0x29 },
	{ .mbps = 950,  .hsfreqrange = 0x3a },
	{ .mbps = 1000, .hsfreqrange = 0x0a },
	{ .mbps = 1050, .hsfreqrange = 0x1a },
	{ .mbps = 1100, .hsfreqrange = 0x2a },
	{ .mbps = 1150, .hsfreqrange = 0x3b },
	{ .mbps = 1200, .hsfreqrange = 0x0b },
	{ .mbps = 1250, .hsfreqrange = 0x1b },
	{ .mbps = 1300, .hsfreqrange = 0x2b },
	{ .mbps = 1350, .hsfreqrange = 0x3c },
	{ .mbps = 1400, .hsfreqrange = 0x0c },
	{ .mbps = 1450, .hsfreqrange = 0x1c },
	{ .mbps = 1500, .hsfreqrange = 0x2c },
	{ .mbps = 1550, .hsfreqrange = 0x3d },
	{ .mbps = 1600, .hsfreqrange = 0x0d },
	{ .mbps = 1650, .hsfreqrange = 0x1d },
	{ .mbps = 1700, .hsfreqrange = 0x2e },
	{ .mbps = 1750, .hsfreqrange = 0x3e },
	{ .mbps = 1800, .hsfreqrange = 0x0e },
	{ .mbps = 1850, .hsfreqrange = 0x1e },
	{ .mbps = 1900, .hsfreqrange = 0x1f },
	{ .mbps = 1950, .hsfreqrange = 0x3f },
	{ .mbps = 2000, .hsfreqrange = 0x0f },
	{ .mbps = 2050, .hsfreqrange = 0x40 },
	{ .mbps = 2100, .hsfreqrange = 0x41 },
	{ .mbps = 2150, .hsfreqrange = 0x42 },
	{ .mbps = 2200, .hsfreqrange = 0x43 },
	{ .mbps = 2250, .hsfreqrange = 0x44 },
	{ .mbps = 2300, .hsfreqrange = 0x45 },
	{ .mbps = 2350, .hsfreqrange = 0x46 },
	{ .mbps = 2400, .hsfreqrange = 0x47 },
	{ .mbps = 2450, .hsfreqrange = 0x48 },
	{ .mbps = 2500, .hsfreqrange = 0x49 },
	{ .mbps = 0,    .hsfreqrange = 0x00 },
};

static int dphy_write(struct nxp_imx_mipi_csi_data *d, unsigned int index, uint32_t val)
{
	const struct dw_dphy_reg *reg;
	uint32_t mask;
	uint32_t tmp;

	if (index >= ARRAY_SIZE(imx95_dphy_regs)) {
		return -EINVAL;
	}

	reg = &imx95_dphy_regs[index];
	mask = reg->mask << reg->shift;
	val <<= reg->shift;

	tmp = sys_read32(d->dphy_regs + reg->offset);
	tmp = (tmp & ~mask) | val;
	sys_write32(tmp, d->dphy_regs + reg->offset);

	return 0;
}

static int set_freqrange_by_mbps(struct nxp_imx_mipi_csi_data *d, uint64_t mbps)
{
	const struct dphy_mbps_hsfreqrange_map *value;
	const struct dphy_mbps_hsfreqrange_map *prev = NULL;

	for (value = hsfreqrange_table; value->mbps; value++) {
		if (value->mbps >= mbps) {
			break;
		}
		prev = value;
	}

	if (prev && value->mbps && ((mbps - prev->mbps) <= (value->mbps - mbps))) {
		value = prev;
	}

	if (!value->mbps) {
		return -ERANGE;
	}

	d->hsfreqrange = value->hsfreqrange;

	/* cfgclkfreqrange: assume 24MHz reference (typical). */
	d->cfgclkfreqrange = 0x2;
	return 0;
}

static int imx95_dphy_configure(struct nxp_imx_mipi_csi_data *d,
			       const struct nxp_imx_mipi_csi_config *cfg)
{
	uint64_t link_freq;
	uint64_t hs_clk_rate;
	uint64_t data_rate_mbps;
	int ret;

	link_freq = video_get_csi_link_freq(cfg->sensor_dev, d->csi_fmt.bpp, cfg->num_lanes);
	if ((int64_t)link_freq < 0) {
		return -EINVAL;
	}

	hs_clk_rate = link_freq * 2U;
	data_rate_mbps = hs_clk_rate / MHZ_TO_HZ;

	if (data_rate_mbps < DPHY_MIN_DATA_RATE_MBPS || data_rate_mbps > DPHY_MAX_DATA_RATE_MBPS) {
		return -ERANGE;
	}

	ret = set_freqrange_by_mbps(d, data_rate_mbps);
	if (ret) {
		return ret;
	}

	dphy_write(d, DPHY_RX_CFGCLKFREQRANGE, d->cfgclkfreqrange);
	dphy_write(d, DPHY_RX_HSFREQRANGE, d->hsfreqrange);

	uint8_t active_lanes = BIT(cfg->num_lanes) - 1;
	dphy_write(d, DPHY_RX_DATA_LANE_BASEDIR, 1);
	k_busy_wait(1);
	dphy_write(d, DPHY_RX_DATA_LANE_FORCERXMODE, active_lanes);
	k_busy_wait(1);
	dphy_write(d, DPHY_RX_DATA_LANE_EN, active_lanes);
	dphy_write(d, DPHY_RX_DATA_LANE_FORCERXMODE, 0);
	dphy_write(d, DPHY_RX_ENABLE_CLK_EXT, 1);
	dphy_write(d, DPHY_RX_PHY_ENABLE_BYP, 1);

	return 0;
}

int nxp_imx_dphy_enable(struct nxp_imx_mipi_csi_data *d, const struct nxp_imx_mipi_csi_config *cfg)
{
	uint32_t val;
	int ret;

	if (cfg->dphy_clock_dev) {
		ret = clock_control_on(cfg->dphy_clock_dev, cfg->dphy_clock_subsys);
		if (ret) {
			return ret;
		}
	}

	csis_write(d, CSIS_DPHY_RSTZ, 0x0);
	csis_write(d, CSIS_DPHY_SHUTDOWNZ, 0x0);

	val = csis_read(d, CSIS_DPHY_TEST_CTRL0);
	val &= ~PHY_TESTCLR;
	csis_write(d, CSIS_DPHY_TEST_CTRL0, val);
	k_busy_wait(1);

	val = csis_read(d, CSIS_DPHY_TEST_CTRL0);
	val |= PHY_TESTCLR;
	csis_write(d, CSIS_DPHY_TEST_CTRL0, val);

	csis_write(d, CSIS_N_LANES, N_LANES(cfg->num_lanes));

	ret = imx95_dphy_configure(d, cfg);
	if (ret) {
		return ret;
	}

	csis_write(d, CSIS_DPHY_SHUTDOWNZ, 0x1);
	k_busy_wait(5);
	csis_write(d, CSIS_DPHY_RSTZ, 0x1);
	k_busy_wait(5);

	return 0;
}

int nxp_imx_dphy_disable(struct nxp_imx_mipi_csi_data *d, const struct nxp_imx_mipi_csi_config *cfg)
{
	int ret = 0;

	csis_write(d, CSIS_N_LANES, 0);
	csis_write(d, CSIS_DPHY_RSTZ, 0x0);
	csis_write(d, CSIS_DPHY_SHUTDOWNZ, 0x0);

	if (cfg->dphy_clock_dev) {
		ret = clock_control_off(cfg->dphy_clock_dev, cfg->dphy_clock_subsys);
	}

	return ret;
}
