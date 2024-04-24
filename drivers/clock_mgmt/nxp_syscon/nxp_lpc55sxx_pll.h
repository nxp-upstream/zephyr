/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_CLOCK_MGMT_NXP_LPC55SXX_PLL_H_
#define ZEPHYR_DRIVERS_CLOCK_MGMT_NXP_LPC55SXX_PLL_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL_HIDDEN */

struct lpc55sxx_pll0_cfg {
	volatile uint32_t CTRL;
	volatile uint32_t NDEC;
	volatile uint32_t SSCG0;
	volatile uint32_t SSCG1;
};

struct lpc55sxx_pll1_cfg {
	volatile uint32_t CTRL;
	volatile uint32_t NDEC;
	volatile uint32_t MDEC;
};

/* Configuration common to both PLLs */
struct lpc55sxx_pllx_cfg {
	volatile uint32_t CTRL;
	volatile uint32_t NDEC;
};

union lpc55sxx_pll_cfg {
	const struct lpc55sxx_pllx_cfg *common;
	const struct lpc55sxx_pll0_cfg *pll0;
	const struct lpc55sxx_pll1_cfg *pll1;
};

struct lpc55sxx_pll_config_input {
	uint32_t output_freq;
	const union lpc55sxx_pll_cfg cfg;
};

#define Z_CLOCK_MGMT_NXP_LPC55SXX_PLL1_DATA_DEFINE(node_id, prop, idx)			\
	const struct lpc55sxx_pll1_cfg _CONCAT(_CONCAT(node_id, idx), pll1_regs) = {	\
		.CTRL = SYSCON_PLL1CTRL_CLKEN_MASK |					\
			SYSCON_PLL1CTRL_SELI(DT_PHA_BY_IDX(node_id, prop, idx, seli)) | \
			SYSCON_PLL1CTRL_SELP(DT_PHA_BY_IDX(node_id, prop, idx, selp)),	\
		.NDEC = SYSCON_PLL1NDEC_NDIV(DT_PHA_BY_IDX(node_id, prop, idx, ndec)),	\
		.MDEC = SYSCON_PLL1MDEC_MDIV(DT_PHA_BY_IDX(node_id, prop, idx, mdec)),	\
	};										\
	const struct lpc55sxx_pll_config_input _CONCAT(_CONCAT(node_id, idx), pll1_cfg) = { \
		.output_freq = DT_PHA_BY_IDX(node_id, prop, idx, frequency),		\
		.cfg.pll1 = &_CONCAT(_CONCAT(node_id, idx), pll1_regs),			\
	};
#define Z_CLOCK_MGMT_NXP_LPC55SXX_PLL1_DATA_GET(node_id, prop, idx)        \
	&_CONCAT(_CONCAT(node_id, idx), pll1_cfg)



/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_CLOCK_MGMT_NXP_LPC55SXX_PLL_H_ */
