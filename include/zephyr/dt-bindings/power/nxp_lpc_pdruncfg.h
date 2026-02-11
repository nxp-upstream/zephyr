/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_DT_BINDINGS_POWER_NXP_LPC_PDRUNCFG_H_
#define ZEPHYR_INCLUDE_ZEPHYR_DT_BINDINGS_POWER_NXP_LPC_PDRUNCFG_H_

/* PDRUNCFG power-down configuration bitmasks (LPC55xxx family). */

/** Power down the GPADC LDO (DisablePD to power the ADC LDO up). */
#define NXP_LPC_PDRUNCFG_PD_LDOGPADC (1U << 19)

#endif /* ZEPHYR_INCLUDE_ZEPHYR_DT_BINDINGS_POWER_NXP_LPC_PDRUNCFG_H_ */
