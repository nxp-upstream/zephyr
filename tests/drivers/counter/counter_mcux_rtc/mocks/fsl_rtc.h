/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#if !defined(TEST_HAL_MOCKS)
#include_next <fsl_rtc.h>
#else

#ifndef TEST_FSL_RTC_H_
#define TEST_FSL_RTC_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#undef FSL_FEATURE_SOC_OSC32_COUNT
#define FSL_FEATURE_SOC_OSC32_COUNT IS_ENABLED(TEST_OSC32)
#undef FSL_FEATURE_RTC_HAS_NO_CR_OSCE
#define FSL_FEATURE_RTC_HAS_NO_CR_OSCE IS_ENABLED(TEST_NO_OSCE)
#undef FSL_FEATURE_RTC_HAS_LPO_ADJUST
#define FSL_FEATURE_RTC_HAS_LPO_ADJUST 1

/* Keep these mocks independent of any device headers included by the test board. */
#define RTC_Type test_rtc_registers
#define rtc_config_t test_rtc_config
#define RTC_GetDefaultConfig test_rtc_get_default_config
#define RTC_Init test_rtc_init
#define RTC_StartTimer test_rtc_start
#define RTC_StopTimer test_rtc_stop
#define RTC_EnableInterrupts test_rtc_enable_interrupts
#define RTC_DisableInterrupts test_rtc_disable_interrupts
#define RTC_GetStatusFlags test_rtc_get_status
#define RTC_ClearStatusFlags test_rtc_clear_status
#define RTC_EnableLPOClock test_rtc_enable_lpo
#define RTC_EnableOscillatorClock test_rtc_enable_oscillator

typedef struct {
	volatile uint32_t TSR;
	volatile uint32_t TPR;
	volatile uint32_t TAR;
	volatile uint32_t TCR;
	volatile uint32_t CR;
	volatile uint32_t SR;
	volatile uint32_t IER;
} RTC_Type;

typedef struct {
	uint32_t unused;
} rtc_config_t;

static RTC_Type rtc_registers;
static unsigned int init_calls;
static unsigned int oscillator_enable_calls;
static bool lpo_enabled;

#ifndef RTC_SR_TIF_MASK
#define RTC_SR_TIF_MASK 0x1U
#define RTC_SR_TOF_MASK 0x2U
#define RTC_SR_TAF_MASK 0x4U
#endif

#define kRTC_TimeInvalidFlag RTC_SR_TIF_MASK
#define kRTC_TimeOverflowFlag RTC_SR_TOF_MASK
#define kRTC_AlarmFlag RTC_SR_TAF_MASK
#define kRTC_TimeInvalidInterruptEnable RTC_SR_TIF_MASK
#define kRTC_TimeOverflowInterruptEnable RTC_SR_TOF_MASK
#define kRTC_AlarmInterruptEnable RTC_SR_TAF_MASK

static inline void RTC_GetDefaultConfig(rtc_config_t *config)
{
	memset(config, 0, sizeof(*config));
}

static inline void RTC_Init(RTC_Type *base, const rtc_config_t *config)
{
	ARG_UNUSED(config);
	memset(base, 0, sizeof(*base));
	init_calls++;
}

static inline void RTC_StartTimer(RTC_Type *base)
{
	base->SR |= 0x10U;
}

static inline void RTC_StopTimer(RTC_Type *base)
{
	base->SR &= ~0x10U;
}

static inline void RTC_EnableInterrupts(RTC_Type *base, uint32_t mask)
{
	base->IER |= mask;
}

static inline void RTC_DisableInterrupts(RTC_Type *base, uint32_t mask)
{
	base->IER &= ~mask;
}

static inline uint32_t RTC_GetStatusFlags(RTC_Type *base)
{
	return base->SR;
}

static inline void RTC_ClearStatusFlags(RTC_Type *base, uint32_t mask)
{
	base->SR &= ~mask;
}

static inline void RTC_EnableLPOClock(RTC_Type *base, bool enable)
{
	ARG_UNUSED(base);
	lpo_enabled = enable;
}

static inline void RTC_EnableOscillatorClock(RTC_Type *base, bool enable)
{
	ARG_UNUSED(base);
	zassert_true(enable);
	oscillator_enable_calls++;
}

#endif /* TEST_FSL_RTC_H_ */
#endif /* TEST_HAL_MOCKS */
