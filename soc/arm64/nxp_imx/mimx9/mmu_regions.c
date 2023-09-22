/*
 * Copyright 2022 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm64/arm_mmu.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

/**
 * @brief Auto generate mmu region entry for node_id
 *
 * Example usage:
 *
 * @code{.c}
 *      DT_FOREACH_STATUS_OKAY_VARGS(nxp_imx_gpio,
 *				  MIMX9_DT_MMU_REGION_FLAT_ENTRY,
 *				 (MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS))
 * @endcode
 *
 * @note  Since devicetree_generated.h does not include
 *        node_id##_P_reg_FOREACH_PROP_ELEM* definitions,
 *        we can't automate dts node with multiple reg
 *        entries.
 *
 */
#define MIMX9_DT_MMU_REGION_FLAT_ENTRY(node_id, attrs)  \
	MMU_REGION_FLAT_ENTRY(DT_NODE_FULL_NAME(node_id), \
			      DT_REG_ADDR(node_id), \
			      DT_REG_SIZE(node_id), \
			      attrs),


static const struct arm_mmu_region mmu_regions[] = {

	MMU_REGION_FLAT_ENTRY("GIC",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 0),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 0),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("GIC",
			      DT_REG_ADDR_BY_IDX(DT_NODELABEL(gic), 1),
			      DT_REG_SIZE_BY_IDX(DT_NODELABEL(gic), 1),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("CCM",
			      DT_REG_ADDR(DT_NODELABEL(ccm)),
			      DT_REG_SIZE(DT_NODELABEL(ccm)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("ANA_PLL",
			      DT_REG_ADDR(DT_NODELABEL(ana_pll)),
			      DT_REG_SIZE(DT_NODELABEL(ana_pll)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("IOMUXC",
			      DT_REG_ADDR(DT_NODELABEL(iomuxc)),
			      DT_REG_SIZE(DT_NODELABEL(iomuxc)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("MEDIAMIX",
			      DT_REG_ADDR(DT_NODELABEL(media_blk_ctrl)),
			      DT_REG_SIZE(DT_NODELABEL(media_blk_ctrl)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("ISI",
			      DT_REG_ADDR(DT_NODELABEL(isi)),
			      DT_REG_SIZE(DT_NODELABEL(isi)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	DT_FOREACH_STATUS_OKAY_VARGS(nxp_imx_gpio,
				  MIMX9_DT_MMU_REGION_FLAT_ENTRY,
				 (MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS))

	DT_FOREACH_STATUS_OKAY_VARGS(nxp_kinetis_lpuart,
				  MIMX9_DT_MMU_REGION_FLAT_ENTRY,
				 (MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS))

	DT_FOREACH_STATUS_OKAY_VARGS(nxp_imx_lpi2c,
				  MIMX9_DT_MMU_REGION_FLAT_ENTRY,
				 (MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS))

	DT_FOREACH_STATUS_OKAY_VARGS(nxp_imx_lpspi,
				  MIMX9_DT_MMU_REGION_FLAT_ENTRY,
				 (MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS))

	DT_FOREACH_STATUS_OKAY_VARGS(nxp_flexcan_fd,
				  MIMX9_DT_MMU_REGION_FLAT_ENTRY,
				 (MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS))

#if CONFIG_SOF
	MMU_REGION_FLAT_ENTRY("MU2_A",
			      DT_REG_ADDR(DT_NODELABEL(mu2_a)),
			      DT_REG_SIZE(DT_NODELABEL(mu2_a)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SAI3",
			      DT_REG_ADDR(DT_NODELABEL(sai3)),
			      DT_REG_SIZE(DT_NODELABEL(sai3)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("EDMA2_CH0",
			      DT_REG_ADDR(DT_NODELABEL(edma2_ch0)),
			      DT_REG_SIZE(DT_NODELABEL(edma2_ch0)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("EDMA2_CH1",
			      DT_REG_ADDR(DT_NODELABEL(edma2_ch1)),
			      DT_REG_SIZE(DT_NODELABEL(edma2_ch1)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("OUTBOX",
			      DT_REG_ADDR(DT_NODELABEL(outbox)),
			      DT_REG_SIZE(DT_NODELABEL(outbox)),
			      MT_NORMAL | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("INBOX",
			      DT_REG_ADDR(DT_NODELABEL(inbox)),
			      DT_REG_SIZE(DT_NODELABEL(inbox)),
			      MT_NORMAL | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("STREAM",
			      DT_REG_ADDR(DT_NODELABEL(stream)),
			      DT_REG_SIZE(DT_NODELABEL(stream)),
			      MT_NORMAL | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("HOST_RAM",
			      DT_REG_ADDR(DT_NODELABEL(host_ram)),
			      DT_REG_SIZE(DT_NODELABEL(host_ram)),
			      MT_NORMAL | MT_P_RW_U_NA | MT_NS),
#endif /* CONFIG_SOF */
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
