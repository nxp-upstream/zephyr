/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt/clock_driver.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <soc.h>
#include "../clock_mgmt_common.h"

/* PLL0 driver */
#define DT_DRV_COMPAT nxp_lpc55sxx_pll0

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

struct lpc55sxx_pll0_config {
	const struct clk *const *children;
	uint8_t child_count;
	const struct clk *parent;
	struct lpc55sxx_pll0_regs *regs;
};

struct lpc55sxx_pll0_data {
	uint32_t output_freq;
};

int syscon_lpc55sxx_pll0_get_rate(const struct clk *clk)
{
	struct lpc55sxx_pll0_data *data = clk->data;

	/* Return stored frequency */
	return data->output_freq;
}

int syscon_lpc55sxx_pll0_configure(const struct clk *clk, void *data)
{
	const struct lpc55sxx_pll0_config *config = clk->config;
	struct lpc55sxx_pll0_data *clk_data = clk->data;
	struct lpc55sxx_pll0_config_input *input = data;
	int input_clk;

	/* Copy configured frequency and PLL settings */
	clk_data->output_freq = input->output_freq;

	/* Power off PLL during setup changes */
	PMC->PDRUNCFGSET0 = PMC_PDRUNCFG0_PDEN_PLL0_SSCG_MASK;
	PMC->PDRUNCFGSET0 = PMC_PDRUNCFG0_PDEN_PLL0_MASK;

	if (clk_data->output_freq == 0) {
		/* Keep PLL powered off, return here */
		return 0;
	}

	config->regs->CTRL = input->reg_settings->CTRL;
	config->regs->STAT = input->reg_settings->STAT;
	config->regs->NDEC = input->reg_settings->NDEC;
	/* Request NDEC change */
	config->regs->NDEC = input->reg_settings->NDEC | SYSCON_PLL0NDEC_NREQ_MASK;
	config->regs->SSCG0 = input->reg_settings->SSCG0;
	config->regs->SSCG1 = input->reg_settings->SSCG1;
	/* Request MD change */
	config->regs->SSCG1 = input->reg_settings->SSCG1 |
		(SYSCON_PLL0SSCG1_MD_REQ_MASK | SYSCON_PLL0SSCG1_MREQ_MASK);

	/* Power PLL on */
	PMC->PDRUNCFGCLR0 = PMC_PDRUNCFG0_PDEN_PLL0_SSCG_MASK;
	PMC->PDRUNCFGCLR0 = PMC_PDRUNCFG0_PDEN_PLL0_MASK;

	/*
	 * Check input reference frequency to VCO. Lock bit is unreliable if
	 * - FREF is below 100KHz or above 20MHz.
	 * - spread spectrum mode is used
	 */
	input_clk = clock_get_rate(config->parent);
	if (input->reg_settings->CTRL & SYSCON_PLL0CTRL_BYPASSPREDIV_MASK) {
		/* Input passes through prediv */
		input_clk /= MAX(input->reg_settings->NDEC & SYSCON_PLL0NDEC_NDIV_MASK, 1);
	}

	if ((config->regs->SSCG0 & SYSCON_PLL0SSCG1_SEL_EXT_MASK) ||
	    (input_clk > MHZ(20)) || (input_clk < KHZ(100))) {
		/* Normal mode, use lock bit*/
		while ((config->regs->STAT & SYSCON_PLL0STAT_LOCK_MASK) == 0) {
			/* Spin */
		}
	} else {
		/* Spread spectrum mode/out of range input frequency.
		 * RM suggests waiting at least 6ms in this case.
		 */
		k_busy_wait(7000);
	}

	return 0;
}

const struct clock_driver_api nxp_syscon_pll0_api = {
	.get_rate = syscon_lpc55sxx_pll0_get_rate,
	.configure = syscon_lpc55sxx_pll0_configure,
	.notify = clock_mgmt_forward_cb,
};

#define NXP_LPC55SXX_PLL0_DEFINE(inst)                                         \
	const struct clk *const lpc55sxx_pll0_children_##inst[] =              \
		CLOCK_INST_GET_DEPS(inst);                                     \
                                                                               \
	const struct lpc55sxx_pll0_config nxp_lpc55sxx_pll0_config_##inst = {  \
		.children = lpc55sxx_pll0_children_##inst,                     \
		.child_count = ARRAY_SIZE(lpc55sxx_pll0_children_##inst),      \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.regs = (struct lpc55sxx_pll0_regs*)DT_INST_REG_ADDR(inst),    \
	};                                                                     \
	struct lpc55sxx_pll0_data nxp_lpc55sxx_pll0_data_##inst = {0};         \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, &nxp_lpc55sxx_pll0_data_##inst,             \
			     &nxp_lpc55sxx_pll0_config_##inst,                 \
			     &nxp_syscon_pll0_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_LPC55SXX_PLL0_DEFINE)


/* PLL1 driver */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT nxp_lpc55sxx_pll1

struct lpc55sxx_pll1_regs {
	volatile uint32_t CTRL;
	volatile uint32_t STAT;
	volatile uint32_t NDEC;
	volatile uint32_t MDEC;
	volatile uint32_t PDEC;
};

struct lpc55sxx_pll1_config_input {
	uint32_t output_freq;
	struct lpc55sxx_pll1_regs *reg_settings;
};

struct lpc55sxx_pll1_config {
	const struct clk *const *children;
	uint8_t child_count;
	const struct clk *parent;
	struct lpc55sxx_pll1_regs *regs;
};

struct lpc55sxx_pll1_data {
	uint32_t output_freq;
};

int syscon_lpc55sxx_pll1_get_rate(const struct clk *clk)
{
	struct lpc55sxx_pll1_data *data = clk->data;

	/* Return stored frequency */
	return data->output_freq;
}

int syscon_lpc55sxx_pll1_configure(const struct clk *clk, void *data)

{
	const struct lpc55sxx_pll1_config *config = clk->config;
	struct lpc55sxx_pll1_data *clk_data = clk->data;
	struct lpc55sxx_pll1_config_input *input = data;
	int input_clk;

	/* Copy configured frequency and PLL settings */
	clk_data->output_freq = input->output_freq;

	/* Power off PLL during setup changes */
	PMC->PDRUNCFGSET0 = PMC_PDRUNCFG0_PDEN_PLL1_MASK;

	if (clk_data->output_freq == 0) {
		/* Keep PLL powered off, return here */
		return 0;
	}

	config->regs->CTRL = input->reg_settings->CTRL;
	config->regs->STAT = input->reg_settings->STAT;
	config->regs->NDEC = input->reg_settings->NDEC;
	/* Request NDEC change */
	config->regs->NDEC = input->reg_settings->NDEC | SYSCON_PLL1NDEC_NREQ_MASK;
	config->regs->MDEC = input->reg_settings->MDEC;
	/* Request MDEC change */
	config->regs->MDEC = input->reg_settings->MDEC | SYSCON_PLL1MDEC_MREQ_MASK;

	/* Power PLL on */
	PMC->PDRUNCFGCLR0 = PMC_PDRUNCFG0_PDEN_PLL1_MASK;

	/*
	 * Check input reference frequency to VCO. Lock bit is unreliable if
	 * - FREF is below 100KHz or above 20MHz.
	 * - spread spectrum mode is used
	 */
	input_clk = clock_get_rate(config->parent);
	if (input->reg_settings->CTRL & SYSCON_PLL1CTRL_BYPASSPREDIV_MASK) {
		/* Input passes through prediv */
		input_clk /= MAX(input->reg_settings->NDEC & SYSCON_PLL1NDEC_NDIV_MASK, 1);
	}

	if ((input_clk > MHZ(20)) || (input_clk < KHZ(100))) {
		/* Normal mode, use lock bit*/
		while ((config->regs->STAT & SYSCON_PLL1STAT_LOCK_MASK) == 0) {
			/* Spin */
		}
	} else {
		/* out of range input frequency. RM suggests waiting at least
		 * 6ms in this case.
		 */
		k_busy_wait(7000);
	}

	return 0;
}


const struct clock_driver_api nxp_syscon_pll1_api = {
	.get_rate = syscon_lpc55sxx_pll1_get_rate,
	.configure = syscon_lpc55sxx_pll1_configure,
	.notify = clock_mgmt_forward_cb,
};

#define NXP_LPC55SXX_PLL1_DEFINE(inst)                                         \
	const struct clk *const lpc55sxx_pll1_children_##inst[] =              \
		CLOCK_INST_GET_DEPS(inst);                                     \
                                                                               \
	const struct lpc55sxx_pll1_config nxp_lpc55sxx_pll1_config_##inst = {  \
		.children = lpc55sxx_pll1_children_##inst,                     \
		.child_count = ARRAY_SIZE(lpc55sxx_pll1_children_##inst),      \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.regs = (struct lpc55sxx_pll1_regs*)DT_INST_REG_ADDR(inst),    \
	};                                                                     \
	struct lpc55sxx_pll1_data nxp_lpc55sxx_pll1_data_##inst = {0};         \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, &nxp_lpc55sxx_pll1_data_##inst,             \
			     &nxp_lpc55sxx_pll1_config_##inst,                 \
			     &nxp_syscon_pll1_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_LPC55SXX_PLL1_DEFINE)

/* PLL PDEC divider driver */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT nxp_lpc55sxx_pll_pdec

struct lpc55sxx_pll_pdec_config {
	const struct clk *const *children;
	uint8_t child_count;
	const struct clk *parent;
	volatile uint32_t *reg;
};

int syscon_lpc55sxx_pll_pdec_get_rate(const struct clk *clk)
{
	const struct lpc55sxx_pll_pdec_config *config = clk->config;
	int parent_rate = clock_get_rate(config->parent);
	int div = (((*config->reg) & SYSCON_PLL0PDEC_PDIV_MASK)) * 2;

	if (parent_rate <= 0) {
		return parent_rate;
	}

	if (div == 0) {
		return -EIO;
	}

	return parent_rate / div;
}

int syscon_lpc55sxx_pll_pdec_configure(const struct clk *clk, void *data)

{
	const struct lpc55sxx_pll_pdec_config *config = clk->config;
	uint32_t div_val = FIELD_PREP(SYSCON_PLL0PDEC_PDIV_MASK, ((uint32_t)data));

	*config->reg = ((*config->reg) & ~SYSCON_PLL0PDEC_PDIV_MASK) | div_val;

	return 0;
}


const struct clock_driver_api nxp_syscon_pdec_api = {
	.get_rate = syscon_lpc55sxx_pll_pdec_get_rate,
	.configure = syscon_lpc55sxx_pll_pdec_configure,
	.notify = clock_mgmt_forward_cb,
};

#define NXP_LPC55SXX_PDEC_DEFINE(inst)                                         \
	const struct clk *const lpc55sxx_pdec_children_##inst[] =              \
		CLOCK_INST_GET_DEPS(inst);                                     \
                                                                               \
	const struct lpc55sxx_pll_pdec_config lpc55sxx_pdec_cfg_##inst = {     \
		.children = lpc55sxx_pdec_children_##inst,                     \
		.child_count = ARRAY_SIZE(lpc55sxx_pdec_children_##inst),      \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.reg = (volatile uint32_t*)DT_INST_REG_ADDR(inst),             \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, NULL, &lpc55sxx_pdec_cfg_##inst,            \
			     &nxp_syscon_pdec_api);

DT_INST_FOREACH_CLK_REFERENCED(NXP_LPC55SXX_PDEC_DEFINE)
