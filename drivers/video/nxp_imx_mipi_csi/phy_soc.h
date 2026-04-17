/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_VIDEO_NXP_IMX_MIPI_CSI_PHY_SOC_H_
#define ZEPHYR_DRIVERS_VIDEO_NXP_IMX_MIPI_CSI_PHY_SOC_H_

#include <stdint.h>

#include "nxp_imx_mipi_csi_priv.h"

struct nxp_imx_dphy_config_ops {
	/**
	 * SoC/PHY-specific DPHY CSR configuration after common freqrange setup.
	 */
	int (*config)(struct nxp_imx_mipi_csi_data *d, const struct nxp_imx_mipi_csi_config *cfg);
};

struct nxp_imx_dphy_drv_data {
	const struct dw_dphy_reg *regs;
	uint32_t regs_size;
	const struct dphy_mbps_hsfreqrange_map *hsfreq_tbl;
	uint8_t max_lanes;
	uint32_t max_data_rate_mbps;
	const struct nxp_imx_dphy_config_ops *ops;
};

int nxp_imx_dphy_write(struct nxp_imx_mipi_csi_data *d, const struct nxp_imx_dphy_drv_data *drv,
			unsigned int index, uint32_t val);

/* i.MX95 DPHY backend */
extern const struct nxp_imx_dphy_drv_data nxp_imx95_dphy_drv_data;

#endif /* ZEPHYR_DRIVERS_VIDEO_NXP_IMX_MIPI_CSI_PHY_SOC_H_ */
