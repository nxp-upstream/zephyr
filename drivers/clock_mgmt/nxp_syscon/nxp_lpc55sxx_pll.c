/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_mgmt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <soc.h>

/* Registers common to both PLLs */
struct lpc55sxx_pllx_regs {
	volatile uint32_t CTRL;
	volatile uint32_t STAT;
	volatile uint32_t NDEC;
};

struct lpc55sxx_pll0_regs {
	volatile uint32_t CTRL;
	volatile uint32_t STAT;
	volatile uint32_t NDEC;
	volatile uint32_t PDEC;
	volatile uint32_t SSCG0;
	volatile uint32_t SSCG1;
};

struct lpc55sxx_pll1_regs {
	volatile uint32_t CTRL;
	volatile uint32_t STAT;
	volatile uint32_t NDEC;
	volatile uint32_t MDEC;
	volatile uint32_t PDEC;
};

union lpc55sxx_pll_regs {
	struct lpc55sxx_pllx_regs *common;
	struct lpc55sxx_pll0_regs *pll0;
	struct lpc55sxx_pll1_regs *pll1;
};

struct lpc55sxx_pll_data {
	uint32_t output_freq;
	const struct clk *parent;
	const union lpc55sxx_pll_regs regs;
	uint8_t idx;
};


int syscon_lpc55sxx_pll_get_rate(const struct clk *clk)
{
	struct lpc55sxx_pll_data *data = clk->hw_data;

	/* Return stored frequency */
	return data->output_freq;
}

int syscon_lpc55sxx_pll_configure(const struct clk *clk, const void *data)
{
	struct lpc55sxx_pll_data *clk_data = clk->hw_data;
	const struct lpc55sxx_pll_config_input *input = data;
	int input_clk;
	uint32_t ctrl, ndec;

	/* Copy configured frequency and PLL settings */
	clk_data->output_freq = input->output_freq;
	clock_notify_children(clk, input->output_freq);

	/* Power off PLL during setup changes */
	if (clk_data->idx == 0) {
		PMC->PDRUNCFGSET0 = PMC_PDRUNCFG0_PDEN_PLL0_SSCG_MASK;
		PMC->PDRUNCFGSET0 = PMC_PDRUNCFG0_PDEN_PLL0_MASK;
	} else {
		PMC->PDRUNCFGSET0 = PMC_PDRUNCFG0_PDEN_PLL1_MASK;
	}

	if (clk_data->output_freq == 0) {
		/* Keep PLL powered off, return here */
		return 0;
	}

	ctrl = input->cfg.common->CTRL;
	ndec = input->cfg.common->NDEC;

	clk_data->regs.common->CTRL = ctrl;
	clk_data->regs.common->NDEC = ndec;
	/* Request NDEC change */
	clk_data->regs.common->NDEC = ndec | SYSCON_PLL0NDEC_NREQ_MASK;
	if (clk_data->idx == 0) {
		/* Setup SSCG parameters */
		clk_data->regs.pll0->SSCG0 = input->cfg.pll0->SSCG0;
		clk_data->regs.pll0->SSCG1 = input->cfg.pll0->SSCG1;
		/* Request MD change */
		clk_data->regs.pll0->SSCG1 = input->cfg.pll0->SSCG1 |
			(SYSCON_PLL0SSCG1_MD_REQ_MASK | SYSCON_PLL0SSCG1_MREQ_MASK);
	} else {
		clk_data->regs.pll1->MDEC = input->cfg.pll1->MDEC;
		/* Request MDEC change */
		clk_data->regs.pll1->MDEC = input->cfg.pll1->MDEC |
					SYSCON_PLL1MDEC_MREQ_MASK;
	}

	/* Power PLL on */
	if (clk_data->idx == 0) {
		PMC->PDRUNCFGCLR0 = PMC_PDRUNCFG0_PDEN_PLL0_SSCG_MASK;
		PMC->PDRUNCFGCLR0 = PMC_PDRUNCFG0_PDEN_PLL0_MASK;
	} else {
		PMC->PDRUNCFGCLR0 = PMC_PDRUNCFG0_PDEN_PLL1_MASK;
	}

	/*
	 * Check input reference frequency to VCO. Lock bit is unreliable if
	 * - FREF is below 100KHz or above 20MHz.
	 * - spread spectrum mode is used
	 */
	input_clk = clock_get_rate(clk_data->parent);
	if ((ctrl & SYSCON_PLL0CTRL_BYPASSPREDIV_MASK) == 0) {
		/* Input passes through prediv */
		input_clk /= MAX((ndec & SYSCON_PLL0NDEC_NDIV_MASK), 1);
	}

	if (((clk_data->idx == 0) &&
	    (clk_data->regs.pll0->SSCG0 & SYSCON_PLL0SSCG1_SEL_EXT_MASK)) ||
	    ((input_clk < MHZ(20)) && (input_clk > KHZ(100)))) {
		/* Normal mode, use lock bit*/
		while ((clk_data->regs.common->STAT & SYSCON_PLL0STAT_LOCK_MASK) == 0) {
			/* Spin */
		}
	} else {
		/* Spread spectrum mode/out of range input frequency.
		 * RM suggests waiting at least 6ms in this case.
		 */
		SDK_DelayAtLeastUs(6000, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
	}

	return 0;
}

int syscon_lpc55sxx_pll_notify(const struct clk *clk, const struct clk *parent,
				uint32_t parent_rate)
{
	struct lpc55sxx_pll_data *clk_data = clk->hw_data;
	/*
	 * Reuse current output rate. This
	 * may not be correct if the parent clock has been reconfigured,
	 * but we are able to avoid runtime rate calculations via this
	 * method
	 */
	return clock_notify_children(clk, clk_data->output_freq);
}

const struct clock_driver_api nxp_syscon_pll_api = {
	.get_rate = syscon_lpc55sxx_pll_get_rate,
	.configure = syscon_lpc55sxx_pll_configure,
	.notify = syscon_lpc55sxx_pll_notify,
};

/* PLL0 driver */
#define DT_DRV_COMPAT nxp_lpc55sxx_pll0

#define NXP_LPC55SXX_PLL0_DEFINE(inst)                                         \
	struct lpc55sxx_pll_data nxp_lpc55sxx_pll0_data_##inst = {             \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.regs.pll0 = ((struct lpc55sxx_pll0_regs *)                    \
			DT_INST_REG_ADDR(inst)),                               \
		.idx = 0,                                                      \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, &nxp_lpc55sxx_pll0_data_##inst,             \
			     &nxp_syscon_pll_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_LPC55SXX_PLL0_DEFINE)


#define NXP_LPC55SXX_PLL1_DEFINE(inst)                                         \
	struct lpc55sxx_pll_data nxp_lpc55sxx_pll1_data_##inst = {             \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.regs.pll1 = ((struct lpc55sxx_pll1_regs *)                    \
			DT_INST_REG_ADDR(inst)),                               \
		.idx = 1,                                                      \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, &nxp_lpc55sxx_pll1_data_##inst,             \
			     &nxp_syscon_pll_api);

/* PLL1 driver */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT nxp_lpc55sxx_pll1

DT_INST_FOREACH_STATUS_OKAY(NXP_LPC55SXX_PLL1_DEFINE)

/* PLL PDEC divider driver */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT nxp_lpc55sxx_pll_pdec

struct lpc55sxx_pll_pdec_config {
	const struct clk *parent;
	volatile uint32_t *reg;
};

int syscon_lpc55sxx_pll_pdec_get_rate(const struct clk *clk)
{
	const struct lpc55sxx_pll_pdec_config *config = clk->hw_data;
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

int syscon_lpc55sxx_pll_pdec_configure(const struct clk *clk, const void *data)

{
	const struct lpc55sxx_pll_pdec_config *config = clk->hw_data;
	int parent_rate = clock_get_rate(config->parent);
	uint32_t div_val = FIELD_PREP(SYSCON_PLL0PDEC_PDIV_MASK, (((uint32_t)data) / 2));

	clock_notify_children(clk, parent_rate / ((uint32_t)div));
	*config->reg = div_val | SYSCON_PLL0PDEC_PREQ_MASK;

	return 0;
}

int syscon_lpc55sxx_pll_pdec_notify(const struct clk *clk, const struct clk *parent,
				    uint32_t parent_rate)
{
	const struct lpc55sxx_pll_pdec_config *config = clk->hw_data;
	int div = (((*config->reg) & SYSCON_PLL0PDEC_PDIV_MASK)) * 2;

	if (div == 0) {
		return -EIO;
	}

	return clock_notify_children(clk, parent_rate / div);
}

const struct clock_driver_api nxp_syscon_pdec_api = {
	.get_rate = syscon_lpc55sxx_pll_pdec_get_rate,
	.configure = syscon_lpc55sxx_pll_pdec_configure,
	.notify = syscon_lpc55sxx_pll_pdec_notify,
};

#define NXP_LPC55SXX_PDEC_DEFINE(inst)                                         \
	const struct lpc55sxx_pll_pdec_config lpc55sxx_pdec_cfg_##inst = {     \
	 	.parent = CLOCK_DT_GET(DT_INST_PARENT(inst)),                  \
		.reg = (volatile uint32_t*)DT_INST_REG_ADDR(inst),             \
	};                                                                     \
	                                                                       \
	CLOCK_DT_INST_DEFINE(inst, &lpc55sxx_pdec_cfg_##inst,                  \
			     &nxp_syscon_pdec_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_LPC55SXX_PDEC_DEFINE)
