/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/platform/hooks.h>
#include <soc.h>

/* Get PHY addresses from device tree */
#define PHY0_ADDR DT_REG_ADDR(DT_NODELABEL(phy0))
#define PHY2_ADDR DT_REG_ADDR(DT_NODELABEL(phy2))

void board_early_init_hook(void)
{
#if defined(CONFIG_ETH_NXP_IMX_NETC) && (DT_CHILD_NUM_STATUS_OKAY(DT_NODELABEL(netc)) != 0)
	/* RGMII mode */
	BLK_CTRL_WAKEUPMIX->NETC_LINK_CFG[0] = BLK_CTRL_WAKEUPMIX_NETC_LINK_CFG_MII_PROT(2);
	BLK_CTRL_WAKEUPMIX->NETC_LINK_CFG[2] = BLK_CTRL_WAKEUPMIX_NETC_LINK_CFG_MII_PROT(2);

	/* Unlock the IERB. It will warm reset whole NETC. */
	NETC_PRIV->NETCRR &= ~NETC_PRIV_NETCRR_LOCK_MASK;

	while ((NETC_PRIV->NETCRR & NETC_PRIV_NETCRR_LOCK_MASK) != 0U) {
	}

	/* Set the access attribute, otherwise MSIX access will be blocked. */
	NETC_IERB->ARRAY_NUM_RC[0].RCMSIAMQR &= ~(7U << 27);
	NETC_IERB->ARRAY_NUM_RC[0].RCMSIAMQR |= (1U << 27);

	/* Set PHY address in IERB to use MAC port MDIO, otherwise the access will be blocked. */
	NETC_IERB->L0BCR = NETC_IERB_L0BCR_MDIO_PHYAD_PRTAD(PHY0_ADDR);
	NETC_IERB->L2BCR = NETC_IERB_L2BCR_MDIO_PHYAD_PRTAD(PHY2_ADDR);

	/* Lock the IERB. */
	NETC_PRIV->NETCRR |= NETC_PRIV_NETCRR_LOCK_MASK;
	while ((NETC_PRIV->NETCSR & NETC_PRIV_NETCSR_STATE_MASK) != 0U) {
	}
#endif
}
