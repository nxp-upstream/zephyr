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

/* No data structure needed for frgmult */
#define Z_CLOCK_MGMT_NXP_SYSCON_FLEXFRG_DATA_DEFINE(node_id, prop, idx)
/* Get numerator configuration value */
#define Z_CLOCK_MGMT_NXP_SYSCON_FLEXFRG_DATA_GET(node_id, prop, idx)           \
	DT_PHA_BY_IDX(node_id, prop, idx, numerator)

/* No data structure needed for div */
#define Z_CLOCK_MGMT_NXP_SYSCON_CLOCK_DIV_DATA_DEFINE(node_id, prop, idx)
/* Get div configuration value */
#define Z_CLOCK_MGMT_NXP_SYSCON_CLOCK_DIV_DATA_GET(node_id, prop, idx)         \
	DT_PHA_BY_IDX(node_id, prop, idx, divider)

#define Z_CLOCK_MGMT_NXP_LPC55SXX_PLL_PDEC_DATA_DEFINE(node_id, prop, idx)
#define Z_CLOCK_MGMT_NXP_LPC55SXX_PLL_PDEC_DATA_GET(node_id, prop, idx)        \
	DT_PHA_BY_IDX(node_id, prop, idx, pdec)

#define Z_CLOCK_MGMT_NXP_SYSCON_CLOCK_GATE_DATA_DEFINE(node_id, prop, idx)
#define Z_CLOCK_MGMT_NXP_SYSCON_CLOCK_GATE_DATA_GET(node_id, prop, idx)        \
	DT_PHA_BY_IDX(node_id, prop, idx, gate)

#define Z_CLOCK_MGMT_NXP_SYSCON_CLOCK_SOURCE_DATA_DEFINE(node_id, prop, idx)
#define Z_CLOCK_MGMT_NXP_SYSCON_CLOCK_SOURCE_DATA_GET(node_id, prop, idx)      \
	DT_PHA_BY_IDX(node_id, prop, idx, gate)

struct lpc55sxx_pll1_regs {
	volatile uint32_t CTRL;
	volatile uint32_t STAT;
	volatile uint32_t NDEC;
	volatile uint32_t MDEC;
	volatile uint32_t PDEC;
};

struct lpc55sxx_pll1_config_input {
	uint32_t output_freq;
	const struct lpc55sxx_pll1_regs *reg_settings;
};

#define Z_CLOCK_MGMT_NXP_LPC55SXX_PLL1_DATA_DEFINE(node_id, prop, idx)			\
	const struct lpc55sxx_pll1_regs _CONCAT(_CONCAT(node_id, idx), pll1_regs) = {	\
		.CTRL = SYSCON_PLL1CTRL_CLKEN_MASK |					\
			SYSCON_PLL1CTRL_SELI(DT_PHA_BY_IDX(node_id, prop, idx, seli)) | \
			SYSCON_PLL1CTRL_SELP(DT_PHA_BY_IDX(node_id, prop, idx, selp)),	\
		.NDEC = SYSCON_PLL1NDEC_NDIV(DT_PHA_BY_IDX(node_id, prop, idx, ndec)),	\
		.MDEC = SYSCON_PLL1MDEC_MDIV(DT_PHA_BY_IDX(node_id, prop, idx, mdec)),	\
	};										\
	const struct lpc55sxx_pll1_config_input _CONCAT(_CONCAT(node_id, idx), pll1_cfg) = { \
		.output_freq = DT_PHA_BY_IDX(node_id, prop, idx, frequency),		\
		.reg_settings = &_CONCAT(_CONCAT(node_id, idx), pll1_regs),		\
	};
#define Z_CLOCK_MGMT_NXP_LPC55SXX_PLL1_DATA_GET(node_id, prop, idx)        \
	&_CONCAT(_CONCAT(node_id, idx), pll1_cfg)


struct lpc55sxx_pll0_regs {
	volatile uint32_t CTRL;
	volatile uint32_t STAT;
	volatile uint32_t NDEC;
	volatile uint32_t PDEC;
	volatile uint32_t SSCG0;
	volatile uint32_t SSCG1;
};

struct lpc55sxx_pll0_config_input {
	uint32_t output_freq;
	struct lpc55sxx_pll0_regs *reg_settings;
};



/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SOC_ARM_NXP_LPC_55xxx_CLOCK_MGMT_SOC_H_ */
