/*
 * Copyright 2022 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm64/arm_mmu.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

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

	MMU_REGION_FLAT_ENTRY("UART1",
			      DT_REG_ADDR(DT_NODELABEL(lpuart1)),
			      DT_REG_SIZE(DT_NODELABEL(lpuart1)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("UART2",
			      DT_REG_ADDR(DT_NODELABEL(lpuart2)),
			      DT_REG_SIZE(DT_NODELABEL(lpuart2)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("IOMUXC",
			      DT_REG_ADDR(DT_NODELABEL(iomuxc)),
			      DT_REG_SIZE(DT_NODELABEL(iomuxc)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("GPIO1",
			      DT_REG_ADDR(DT_NODELABEL(gpio1)),
			      DT_REG_SIZE(DT_NODELABEL(gpio1)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("GPIO2",
			      DT_REG_ADDR(DT_NODELABEL(gpio2)),
			      DT_REG_SIZE(DT_NODELABEL(gpio2)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("GPIO3",
			      DT_REG_ADDR(DT_NODELABEL(gpio3)),
			      DT_REG_SIZE(DT_NODELABEL(gpio3)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("GPIO4",
			      DT_REG_ADDR(DT_NODELABEL(gpio4)),
			      DT_REG_SIZE(DT_NODELABEL(gpio4)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

    MMU_REGION_FLAT_ENTRY("I2C1",
			      DT_REG_ADDR(DT_NODELABEL(lpi2c1)),
			      DT_REG_SIZE(DT_NODELABEL(lpi2c1)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("I2C2",
			      DT_REG_ADDR(DT_NODELABEL(lpi2c2)),
			      DT_REG_SIZE(DT_NODELABEL(lpi2c2)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("I2C3",
			      DT_REG_ADDR(DT_NODELABEL(lpi2c3)),
			      DT_REG_SIZE(DT_NODELABEL(lpi2c3)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("I2C4",
			      DT_REG_ADDR(DT_NODELABEL(lpi2c4)),
			      DT_REG_SIZE(DT_NODELABEL(lpi2c4)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SPI1",
			      DT_REG_ADDR(DT_NODELABEL(lpspi1)),
			      DT_REG_SIZE(DT_NODELABEL(lpspi1)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SPI2",
			      DT_REG_ADDR(DT_NODELABEL(lpspi2)),
			      DT_REG_SIZE(DT_NODELABEL(lpspi2)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SPI3",
			      DT_REG_ADDR(DT_NODELABEL(lpspi3)),
			      DT_REG_SIZE(DT_NODELABEL(lpspi3)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("SPI4",
			      DT_REG_ADDR(DT_NODELABEL(lpspi4)),
			      DT_REG_SIZE(DT_NODELABEL(lpspi4)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("CAN1",
			      DT_REG_ADDR(DT_NODELABEL(flexcan1)),
			      DT_REG_SIZE(DT_NODELABEL(flexcan1)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

	MMU_REGION_FLAT_ENTRY("CAN2",
			      DT_REG_ADDR(DT_NODELABEL(flexcan2)),
			      DT_REG_SIZE(DT_NODELABEL(flexcan2)),
			      MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS),

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
