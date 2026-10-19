/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_FSL_DEVICE_REGISTERS_H_
#define TEST_FSL_DEVICE_REGISTERS_H_

#include <stdint.h>
#include <zephyr/sys/util.h>

typedef struct {
	volatile uint32_t CSR;
	volatile uint32_t UTRIM;
} VREF_Type;

typedef struct {
	volatile uint32_t ACTIVE_CFG1;
} SPC_Type;

#define SPC_ACTIVE_CFG1_SOC_CNTRL(value) (value)

#define VREF_CSR_HCBGEN_MASK BIT(0)
#if defined(FSL_FEATURE_VREF_HAS_LPBGEN) && !FSL_FEATURE_VREF_HAS_LPBGEN
#define VREF_CSR_IBIAS_EN_MASK BIT(1)
#else
#define VREF_CSR_LPBGEN_MASK BIT(1)
#endif
#if !defined(FSL_FEATURE_VREF_HAS_LOWPOWER_BUFFER) || FSL_FEATURE_VREF_HAS_LOWPOWER_BUFFER
#define VREF_CSR_LPBG_BUF_EN_MASK BIT(2)
#endif
#define VREF_CSR_CHOPEN_MASK BIT(3)
#define VREF_CSR_ICOMPEN_MASK BIT(4)
#define VREF_CSR_REGEN_MASK BIT(5)
#define VREF_CSR_HI_PWR_LV_MASK BIT(11)
#define VREF_CSR_BUF21EN_MASK BIT(16)
#define VREF_CSR_VREFST_MASK BIT(31)
#define VREF_UTRIM_TRIM2V1_MASK 0xFU
#define VREF_UTRIM_TRIM2V1_SHIFT 0U
#define VREF_UTRIM_VREFTRIM_MASK 0x3F00U
#define VREF_UTRIM_VREFTRIM_SHIFT 8U

#endif
