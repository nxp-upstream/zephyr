/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_FSL_WWDT_H_
#define TEST_FSL_WWDT_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#define WWDT_MOD_WDEN_MASK    BIT(0)
#define WWDT_MOD_WDRESET_MASK BIT(1)
#define WWDT_MOD_WDTOF_MASK   BIT(2)
#define WWDT_MOD_WDINT_MASK   BIT(3)
#define WWDT_TC_COUNT_MASK    0xFFFFFFU

typedef struct {
	uint32_t MOD;
	uint32_t TC;
	uint32_t FEED;
	uint32_t TV;
	uint32_t WARNINT;
	uint32_t WINDOW;
} WWDT_Type;

typedef struct {
	bool enableWwdt;
	bool enableWatchdogReset;
	bool enableWatchdogProtect;
	bool enableLockOscillator;
	uint32_t windowValue;
	uint32_t timeoutValue;
	uint32_t warningValue;
	uint32_t clockFreq_Hz;
} wwdt_config_t;

void WWDT_GetDefaultConfig(wwdt_config_t *config);
void WWDT_Init(WWDT_Type *base, const wwdt_config_t *config);
void WWDT_Deinit(WWDT_Type *base);
void WWDT_Refresh(WWDT_Type *base);
uint32_t WWDT_GetStatusFlags(WWDT_Type *base);
void WWDT_ClearStatusFlags(WWDT_Type *base, uint32_t flags);

#endif
