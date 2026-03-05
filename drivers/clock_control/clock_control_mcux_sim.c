/*
 * Copyright 2017, 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_kinetis_sim
#include <errno.h>
#include <soc.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/kinetis_sim.h>
#include <zephyr/sys/util.h>
#include <fsl_clock.h>

#define LOG_LEVEL CONFIG_CLOCK_CONTROL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(clock_control);

static inline uint32_t mcux_sim_subsys_val(clock_control_subsys_t sub_system)
{
	return POINTER_TO_UINT(sub_system);
}

static bool mcux_sim_subsys_is_encoded(uint32_t subsys)
{
	return KINETIS_SIM_SUBSYS_IS_ENCODED(subsys);
}

static clock_ip_name_t mcux_sim_subsys_decode_gate(uint32_t subsys)
{
	uint32_t offset = KINETIS_SIM_SUBSYS_DECODE_OFFSET(subsys);
	uint32_t bit = KINETIS_SIM_SUBSYS_DECODE_BITS(subsys);

	return (clock_ip_name_t)CLK_GATE_DEFINE(offset, bit);
}

static uint32_t mcux_sim_subsys_decode_name(uint32_t subsys)
{
	return KINETIS_SIM_SUBSYS_DECODE_NAME(subsys);
}

static bool mcux_sim_subsys_is_gate_token(uint32_t subsys)
{
	/*
	 * The MCUX SIM uses CLK_GATE_DEFINE(offset, bit) to encode the gate.
	 * This yields a 32-bit token with a non-zero high 16-bit register offset.
	 */
	return (subsys & 0xFFFF0000U) != 0U;
}

static int mcux_sim_on(const struct device *dev,
		       clock_control_subsys_t sub_system)
{
	uint32_t subsys = mcux_sim_subsys_val(sub_system);
	clock_ip_name_t clock_ip_name;

	ARG_UNUSED(dev);

	if (mcux_sim_subsys_is_encoded(subsys)) {
		clock_ip_name = mcux_sim_subsys_decode_gate(subsys);
		CLOCK_EnableClock(clock_ip_name);
		return 0;
	}

	/*
	 * Legacy callers may pass an encoded SIM gate token.
	 * Name-only tokens (e.g. KINETIS_SIM_MCGPCLK) are not usable for gating.
	 */
	if (mcux_sim_subsys_is_gate_token(subsys)) {
		clock_ip_name = (clock_ip_name_t)subsys;
		CLOCK_EnableClock(clock_ip_name);
		return 0;
	}
	/* Name-only token: nothing to gate here. */
	return 0;
}

static int mcux_sim_off(const struct device *dev,
			clock_control_subsys_t sub_system)
{
	uint32_t subsys = mcux_sim_subsys_val(sub_system);
	clock_ip_name_t clock_ip_name;

	ARG_UNUSED(dev);

	if (mcux_sim_subsys_is_encoded(subsys)) {
		clock_ip_name = mcux_sim_subsys_decode_gate(subsys);
		CLOCK_DisableClock(clock_ip_name);
		return 0;
	}

	if (mcux_sim_subsys_is_gate_token(subsys)) {
		clock_ip_name = (clock_ip_name_t)subsys;
		CLOCK_DisableClock(clock_ip_name);
		return 0;
	}

	/* Name-only token: nothing to gate here. */
	return 0;
}

static int mcux_sim_get_subsys_rate(const struct device *dev,
				    clock_control_subsys_t sub_system,
				    uint32_t *rate)
{
	clock_name_t clock_name;
	uint32_t subsys = mcux_sim_subsys_val(sub_system);

	ARG_UNUSED(dev);

	if (mcux_sim_subsys_is_encoded(subsys)) {
		subsys = mcux_sim_subsys_decode_name(subsys);
	}

	/* Gate tokens do not carry rate information. */
	if (mcux_sim_subsys_is_gate_token(subsys)) {
		return -ENOTSUP;
	}

	switch (subsys) {
	case KINETIS_SIM_LPO_CLK:
		clock_name = kCLOCK_LpoClk;
		break;
	default:
		clock_name = (clock_name_t)subsys;
		break;
	}

	*rate = CLOCK_GetFreq(clock_name);

	return 0;
}

#if DT_NODE_HAS_STATUS_OKAY(DT_INST(0, nxp_kinetis_ke1xf_sim))
#define NXP_KINETIS_SIM_NODE DT_INST(0, nxp_kinetis_ke1xf_sim)
#if DT_NODE_HAS_PROP(DT_INST(0, nxp_kinetis_ke1xf_sim), clkout_source)
	#define NXP_KINETIS_SIM_CLKOUT_SOURCE \
			DT_PROP(DT_INST(0, nxp_kinetis_ke1xf_sim), clkout_source)
#endif
#if DT_NODE_HAS_PROP(DT_INST(0, nxp_kinetis_ke1xf_sim), clkout_divider)
	#define NXP_KINETIS_SIM_CLKOUT_DIVIDER \
			DT_PROP(DT_INST(0, nxp_kinetis_ke1xf_sim), clkout_divider)
#endif
#else
#define NXP_KINETIS_SIM_NODE DT_INST(0, nxp_kinetis_sim)
#if DT_NODE_HAS_PROP(DT_INST(0, nxp_kinetis_sim), clkout_source)
	#define NXP_KINETIS_SIM_CLKOUT_SOURCE \
		DT_PROP(DT_INST(0, nxp_kinetis_sim), clkout_source)
#endif
#if DT_NODE_HAS_PROP(DT_INST(0, nxp_kinetis_sim), clkout_divider)
	#define NXP_KINETIS_SIM_CLKOUT_DIVIDER \
		DT_PROP(DT_INST(0, nxp_kinetis_sim), clkout_divider)
#endif
#endif

static int mcux_sim_init(const struct device *dev)
{
#ifdef NXP_KINETIS_SIM_CLKOUT_DIVIDER
	SIM->CHIPCTL = (SIM->CHIPCTL & ~SIM_CHIPCTL_CLKOUTDIV_MASK)
		| SIM_CHIPCTL_CLKOUTDIV(NXP_KINETIS_SIM_CLKOUT_DIVIDER);
#endif
#ifdef NXP_KINETIS_SIM_CLKOUT_SOURCE
	SIM->CHIPCTL = (SIM->CHIPCTL & ~SIM_CHIPCTL_CLKOUTSEL_MASK)
		| SIM_CHIPCTL_CLKOUTSEL(NXP_KINETIS_SIM_CLKOUT_SOURCE);
#endif

	return 0;
}

static DEVICE_API(clock_control, mcux_sim_driver_api) = {
	.on = mcux_sim_on,
	.off = mcux_sim_off,
	.get_rate = mcux_sim_get_subsys_rate,
};

DEVICE_DT_DEFINE(NXP_KINETIS_SIM_NODE,
		    mcux_sim_init,
		    NULL,
		    NULL, NULL,
		    PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_INIT_PRIORITY,
		    &mcux_sim_driver_api);
