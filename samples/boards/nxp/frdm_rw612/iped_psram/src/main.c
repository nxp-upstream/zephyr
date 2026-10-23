/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reproduction for psram-iped.md Issue 2:
 *
 *   With IPED enabled and a GCM region provisioned on FlexSPI A1, a 32-bit
 *   CPU store/load round trip to the aps6404l pSRAM on FlexSPI B1 - an
 *   address outside every configured IPED region - does not come back
 *   unchanged.
 *
 * Prerequisite: provision the IPED region on ext_flash first, using
 * ../iped/scripts/provision_and_test.py against this board (region 1,
 * 0x320000-0x324000, CTR mode is enough - GCM is not required to reproduce
 * this, since even AHBGCMRD alone toggles the failure per psram-iped.md).
 * This app only touches the pSRAM; it does not read or write ext_flash.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <fsl_flexspi.h>

LOG_MODULE_REGISTER(iped_psram_repro, LOG_LEVEL_INF);

#define FLEXSPI_REG_BASE DT_REG_ADDR_BY_NAME(DT_NODELABEL(flexspi), reg_base)

#define PSRAM_TEST_ADDR   ((volatile uint32_t *)0x1c000000U)
#define PSRAM_TEST_VALUE  0xa5a5a5a5U

struct ipedctrl_case {
	const char *name;
	uint32_t clear_mask;
};

/* Bits cleared cumulatively, matching the sweep customer ran in psram-iped.md */
static const struct ipedctrl_case sweep[] = {
	{ "baseline (no bits cleared)",   0U },
	{ "AHBGCMRD cleared",             FLEXSPI_IPEDCTRL_AHBGCMRD_MASK },
	{ "AHBGCMRD + AHBRD_EN cleared",  FLEXSPI_IPEDCTRL_AHBGCMRD_MASK |
					   FLEXSPI_IPEDCTRL_AHBRD_EN_MASK },
	{ "IPED_EN also cleared (control)", FLEXSPI_IPEDCTRL_AHBGCMRD_MASK |
					     FLEXSPI_IPEDCTRL_AHBRD_EN_MASK |
					     FLEXSPI_IPEDCTRL_IPED_EN_MASK },
};

static uint32_t psram_round_trip_once(FLEXSPI_Type *flexspi, uint32_t clear_mask)
{
	uint32_t saved_ctrl;
	uint32_t key;
	uint32_t readback;

	key = irq_lock();

	saved_ctrl = flexspi->IPEDCTRL;
	flexspi->IPEDCTRL = saved_ctrl & ~clear_mask;

	*PSRAM_TEST_ADDR = PSRAM_TEST_VALUE;
	barrier_dsync_fence_full();
	readback = *PSRAM_TEST_ADDR;

	flexspi->IPEDCTRL = saved_ctrl;

	irq_unlock(key);

	return readback;
}

int main(void)
{
	FLEXSPI_Type *flexspi = (FLEXSPI_Type *)FLEXSPI_REG_BASE;

	LOG_INF("IPEDCTRL at boot: 0x%08x", flexspi->IPEDCTRL);
	LOG_INF("pSRAM test address: %p, wrote 0x%08x", (void *)PSRAM_TEST_ADDR,
		PSRAM_TEST_VALUE);

	for (size_t i = 0; i < ARRAY_SIZE(sweep); i++) {
		uint32_t readback = psram_round_trip_once(flexspi, sweep[i].clear_mask);
		bool round_trips = (readback == PSRAM_TEST_VALUE);

		LOG_INF("[%s] readback=0x%08x -> %s", sweep[i].name, readback,
			round_trips ? "OK" : "CORRUPT");
	}

	LOG_INF("Repro done. Issue 2 reproduces if any non-control case is CORRUPT "
		"while pSRAM sits outside every provisioned IPED region.");

	return 0;
}
