/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Register-level test for the NXP EVTG (Event Generator) peripheral.
 *
 * The EVTG output is a hardware event signal routed through the on-chip
 * interconnect; it is not readable through a status register. This test
 * therefore exercises the fsl_evtg HAL on real hardware and verifies that the
 * configuration is correctly programmed by reading the EVTG control and AOI
 * boolean-function registers back. Because the EVTG bus clock must be running
 * for those writes to take effect, a successful read-back also proves the
 * peripheral is present, clock-gated correctly, and reachable at the address
 * resolved from devicetree.
 */

#include <zephyr/ztest.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include <fsl_evtg.h>
#include <fsl_clock.h>

#define EVTG_NODE DT_NODELABEL(evtg0)

BUILD_ASSERT(DT_NODE_HAS_STATUS(EVTG_NODE, okay),
	     "evtg0 devicetree node must be enabled for this test");

/* MCXN parts expose a single EVTG instance, selected by index 0. */
#define TEST_EVTG_INDEX kEVTG_Index0

static EVTG_Type *const evtg = (EVTG_Type *)DT_REG_ADDR(EVTG_NODE);

static uint16_t evtg_ctrl(void)
{
	return evtg->EVTG_INST[(uint8_t)TEST_EVTG_INDEX].EVTG_CTRL;
}

static uint16_t evtg_aoi0_bft01(void)
{
	return evtg->EVTG_INST[(uint8_t)TEST_EVTG_INDEX].EVTG_AOI0_BFT01;
}

static void *nxp_evtg_setup(void)
{
	/*
	 * The fsl_evtg HAL does not manage the peripheral clock gate, so the
	 * EVTG bus clock must be enabled before any register access.
	 */
	CLOCK_EnableClock(kCLOCK_Evtg);

	return NULL;
}

ZTEST_SUITE(nxp_evtg, NULL, nxp_evtg_setup, NULL, NULL, NULL);

/* EVTG_Init() with the default (flip-flop bypass) configuration must run on
 * real hardware and leave the control register in bypass mode.
 */
ZTEST(nxp_evtg, test_init_default_bypass)
{
	evtg_config_t config;
	uint16_t mode;

	EVTG_GetDefaultConfig(&config, kEVTG_FFModeBypass);
	EVTG_Init(evtg, TEST_EVTG_INDEX, &config);

	mode = FIELD_GET(EVTG_EVTG_INST_EVTG_CTRL_MODE_SEL_MASK, evtg_ctrl());
	zassert_equal(mode, (uint16_t)kEVTG_FFModeBypass,
		      "flip-flop mode not programmed to bypass");
}

/*
 * The flip-flop initial output value must be programmable. INIT_EN is a
 * write-1 pulse control (reads back 0 by hardware design), so only the
 * latched FF_INIT value is read back and checked here.
 */
ZTEST(nxp_evtg, test_force_flipflop_init_output)
{
	EVTG_ForceFlipflopInitOutput(evtg, TEST_EVTG_INDEX, kEVTG_FFInitOut1);

	zassert_equal(FIELD_GET(EVTG_EVTG_INST_EVTG_CTRL_FF_INIT_MASK, evtg_ctrl()),
		      (uint16_t)kEVTG_FFInitOut1,
		      "flip-flop init output not set to 1");

	EVTG_ForceFlipflopInitOutput(evtg, TEST_EVTG_INDEX, kEVTG_FFInitOut0);

	zassert_equal(FIELD_GET(EVTG_EVTG_INST_EVTG_CTRL_FF_INIT_MASK, evtg_ctrl()),
		      (uint16_t)kEVTG_FFInitOut0,
		      "flip-flop init output not cleared to 0");
}

/* Each AOI product-term input must be programmable to all four input modes. */
ZTEST(nxp_evtg, test_set_product_term_input)
{
	static const evtg_aoi_input_config_t inputs[] = {
		kEVTG_InputLogicOne,
		kEVTG_InputLogicZero,
		kEVTG_InputComplement,
		kEVTG_InputDirectPass,
	};

	ARRAY_FOR_EACH(inputs, i) {
		uint16_t got;

		EVTG_SetProductTermInput(evtg, TEST_EVTG_INDEX, kEVTG_AOI0,
					 kEVTG_ProductTerm0, kEVTG_InputA, inputs[i]);

		got = FIELD_GET(EVTG_EVTG_INST_EVTG_AOI0_BFT01_PT0_AC_MASK,
				evtg_aoi0_bft01());
		zassert_equal(got, (uint16_t)inputs[i],
			      "product term 0 input A read-back mismatch (input %u)",
			      (unsigned int)inputs[i]);
	}
}
