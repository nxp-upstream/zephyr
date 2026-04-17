/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_VIDEO_NXP_IMX_MIPI_CSI_PHY_SELECT_H_
#define ZEPHYR_DRIVERS_VIDEO_NXP_IMX_MIPI_CSI_PHY_SELECT_H_

#include <zephyr/devicetree.h>

#include "phy_soc.h"

/*
 * Select the PHY backend based on the phys node compatible.
 * Extend this when adding i.MX952/i.MX93 PHY support.
 */
#define NXP_IMX_DPHY_DRV_DATA(node_id)							\
	COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, nxp_imx95_dphy_rx), 			\
			(&nxp_imx95_dphy_drv_data), (NULL))				\

#endif /* ZEPHYR_DRIVERS_VIDEO_NXP_IMX_MIPI_CSI_PHY_SELECT_H_ */
