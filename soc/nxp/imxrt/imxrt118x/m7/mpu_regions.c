/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/arch/arm/cortex_m/arm_mpu_mem_cfg.h>

#define REGION_ITCM_BASE_ADDRESS       0x00000000
#define REGION_ITCM_SIZE               REGION_256K
#define REGION_FLEXSPI2_BASE_ADDRESS   0x04000000
#define REGION_FLEXSPI2_SIZE           REGION_64M
#define REGION_DTCM_BASE_ADDRESS       0x20000000
#define REGION_DTCM_SIZE               REGION_256K
#define REGION_FLEXSPI_BASE_ADDRESS    0x28000000
#define REGION_FLEXSPI_SIZE            REGION_128M
#define REGION_PERIPHERAL_BASE_ADDRESS 0x40000000
#define REGION_PERIPHERAL_SIZE         REGION_1G

static const struct arm_mpu_region mpu_regions[] = {
	MPU_REGION_ENTRY("ITCM", REGION_ITCM_BASE_ADDRESS, REGION_FLASH_ATTR(REGION_ITCM_SIZE)),
	MPU_REGION_ENTRY("FLEXSPI2", REGION_FLEXSPI2_BASE_ADDRESS,
			 REGION_RAM_ATTR(REGION_FLEXSPI2_SIZE)),
	MPU_REGION_ENTRY("FLEXSPI", REGION_FLEXSPI_BASE_ADDRESS,
			 REGION_FLASH_ATTR(REGION_FLEXSPI_SIZE)),
	MPU_REGION_ENTRY("DTCM", REGION_DTCM_BASE_ADDRESS, REGION_RAM_ATTR(REGION_DTCM_SIZE)),
	MPU_REGION_ENTRY("PERIPHERAL", REGION_PERIPHERAL_BASE_ADDRESS,
			 REGION_IO_ATTR(REGION_PERIPHERAL_SIZE)),
};

const struct arm_mpu_config mpu_config = {
	.num_regions = ARRAY_SIZE(mpu_regions),
	.mpu_regions = mpu_regions,
};
