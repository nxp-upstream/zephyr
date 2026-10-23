/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include <zephyr/ztest.h>

#include <fsl_memcon.h>
#include <fsl_power.h>
#include <soc_memory.h>

#include "stay_awake.h"

/*
 * SRAMCTL_1 Mem 0, the first 144 KB of sram1. The image places nothing there: it
 * runs from the XIP flash with its RAM in the PSRAM and the TCMs. Its MEMCON slice
 * is the COMPUTE one, which stays powered while the NPU, COMM and MEDIA domains
 * are off; the slices inside those domains must not be touched while they are.
 */
#define UNUSED_BLOCK kPOWER_Mem_ComputeSram6

BUILD_ASSERT(!DT_SAME_NODE(DT_CHOSEN(zephyr_sram), DT_NODELABEL(sram1)),
	     "The memory block under test is in the system RAM");

/* power_memory_t encodes (slice << 8) | block. */
#define POWER_MEMORY_SLICE_SHIFT 8U
#define POWER_MEMORY_BLOCK_MASK  0xFFU

static MEMCON_SLICE_Type *const memcon_slices[] = MEMCON_SLICE_BASE_PTRS;

static memcon_power_mode_t active_block_mode(uint32_t mem_id)
{
	MEMCON_SLICE_Type *slice = memcon_slices[mem_id >> POWER_MEMORY_SLICE_SHIFT];

	return MEMCON_SLICE_GetActiveBlockMode(slice, (uint8_t)(mem_id & POWER_MEMORY_BLOCK_MASK));
}

static void assert_block_mode(memcon_power_mode_t expected, const char *when)
{
	memcon_power_mode_t mode = active_block_mode(UNUSED_BLOCK);

	zassert_equal(mode, expected, "Block mode is %d %s, expected %d", mode, when, expected);
}

ZTEST(soc_memory, test_retention_reads_back_and_returns_to_active)
{
	assert_block_mode(kMEMCON_PowerModeActive, "before the test");

	zassert_ok(soc_memory_set_mode(UNUSED_BLOCK, SOC_MEMORY_MODE_RETENTION),
		   "Cannot set the block to retention");
	assert_block_mode(kMEMCON_PowerModeRetention, "after setting retention");

	zassert_ok(soc_memory_set_mode(UNUSED_BLOCK, SOC_MEMORY_MODE_ACTIVE),
		   "Cannot set the block back to active");
	assert_block_mode(kMEMCON_PowerModeActive, "after setting active");
}

/* POWER_Init() leaves every block active: a failed test must not leave it otherwise. */
static void soc_memory_after(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(soc_memory_set_mode(UNUSED_BLOCK, SOC_MEMORY_MODE_ACTIVE),
		   "Cannot restore the block to active");
}

ZTEST_SUITE(soc_memory, NULL, stay_awake_setup, NULL, soc_memory_after, stay_awake_teardown);
