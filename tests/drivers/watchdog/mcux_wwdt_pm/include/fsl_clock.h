/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_FSL_CLOCK_H_
#define TEST_FSL_CLOCK_H_

#include <stdint.h>

enum clock_div_name {
	kCLOCK_DivWdt0Clk,
	kCLOCK_DivWdt1Clk,
};

void CLOCK_SetClkDiv(enum clock_div_name divider, uint32_t value);

#endif
