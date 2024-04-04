/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_NXP_LPC_55xxx_CLOCK_MGMT_SOC_H_
#define ZEPHYR_SOC_ARM_NXP_LPC_55xxx_CLOCK_MGMT_SOC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/devicetree.h>

/** @cond INTERNAL_HIDDEN */

/* No data structure needed for mux */
#define Z_CLOCK_MGMT_NXP_SYSCON_CLOCK_MUX_DATA_DEFINE(node_id, prop, idx)
/* Get mux configuration value */
#define Z_CLOCK_MGMT_NXP_SYSCON_CLOCK_MUX_DATA_GET(node_id, prop, idx)         \
	DT_PHA_BY_IDX(node_id, prop, idx, multiplexer)



/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SOC_ARM_NXP_LPC_55xxx_CLOCK_MGMT_SOC_H_ */
