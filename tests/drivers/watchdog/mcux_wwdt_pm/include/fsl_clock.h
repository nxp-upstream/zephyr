/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_FSL_CLOCK_H_
#define TEST_FSL_CLOCK_H_

#include <stdint.h>

typedef enum clock_div_name {
	kCLOCK_DivWdt0Clk,
	kCLOCK_DivWdt1Clk,
} clock_div_name_t;

void CLOCK_SetClkDiv(enum clock_div_name divider, uint32_t value);
uint32_t CLOCK_GetClkDiv(enum clock_div_name divider);

#endif
