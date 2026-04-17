/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include "nxp_imx_mipi_csi_priv.h"
#include "phy_soc.h"

LOG_MODULE_DECLARE(nxp_imx_mipi_csi, CONFIG_VIDEO_LOG_LEVEL);

int nxp_imx_dphy_write(struct nxp_imx_mipi_csi_data *d, const struct nxp_imx_dphy_drv_data *drv,
			unsigned int index, uint32_t val)
{
	const struct dw_dphy_reg *reg;
	uint32_t mask;
	uint32_t tmp;

	if (!drv || !drv->regs || index >= drv->regs_size) {
		return -EINVAL;
	}

	reg = &drv->regs[index];
	mask = reg->mask << reg->shift;
	val <<= reg->shift;

	tmp = sys_read32(d->dphy_regs + reg->offset);
	tmp = (tmp & ~mask) | val;
	sys_write32(tmp, d->dphy_regs + reg->offset);

	return 0;
}

static int nxp_imx_dphy_set_freqrange_by_mbps(struct nxp_imx_mipi_csi_data *d,
					     const struct nxp_imx_dphy_drv_data *drv, uint64_t mbps)
{
	const struct dphy_mbps_hsfreqrange_map *value;
	const struct dphy_mbps_hsfreqrange_map *prev = NULL;

	if (!drv || !drv->hsfreq_tbl) {
		return -EINVAL;
	}

	for (value = drv->hsfreq_tbl; value->mbps; value++) {
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

static int nxp_imx_dphy_configure(struct nxp_imx_mipi_csi_data *d,
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

	ret = nxp_imx_dphy_set_freqrange_by_mbps(d, cfg->dphy_drv_data, data_rate_mbps);
	if (ret) {
		return ret;
	}

	nxp_imx_dphy_write(d, cfg->dphy_drv_data, DPHY_RX_CFGCLKFREQRANGE, d->cfgclkfreqrange);
	nxp_imx_dphy_write(d, cfg->dphy_drv_data, DPHY_RX_HSFREQRANGE, d->hsfreqrange);


	/* SoC-specific DPHY lane configuration */
	if (cfg->dphy_drv_data && cfg->dphy_drv_data->ops && cfg->dphy_drv_data->ops->config) {
		ret = cfg->dphy_drv_data->ops->config(d, cfg);
		if (ret) {
			return ret;
		}
	}


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

	ret = nxp_imx_dphy_configure(d, cfg);
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
