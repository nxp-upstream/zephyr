/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree constants for the NXP NAFE13388 analog front-end.
 *
 * Register field encodings are taken from the NAFE13388 data sheet
 * (Rev. 5.0), table "16-bit channel-based configuration".
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ADC_NAFE13388_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ADC_NAFE13388_H_

/**
 * @name High voltage input selection
 *
 * Values for zephyr,input-positive (HV_AIP) and zephyr,input-negative (HV_AIN)
 * when the channel selects the high voltage multiplexer. The same code selects
 * the AIxP pin on the positive side and the AIxN pin on the negative side.
 * @{
 */
#define NAFE13388_HV_GND   0 /**< Internal ground */
#define NAFE13388_HV_AI1   1 /**< AI1P / AI1N */
#define NAFE13388_HV_AI2   2 /**< AI2P / AI2N */
#define NAFE13388_HV_AI3   3 /**< AI3P / AI3N */
#define NAFE13388_HV_AI4   4 /**< AI4P / AI4N */
#define NAFE13388_HV_REFH  5 /**< REFH diagnostic source */
#define NAFE13388_HV_REFL  6 /**< REFL diagnostic source */
#define NAFE13388_HV_AICOM 7 /**< Analog common input */
#define NAFE13388_HV_VEXC  8 /**< Excitation voltage output */
/** @} */

/**
 * @name Low voltage signal selection
 *
 * Values for nxp,lv-signal (LVSIG_IN) when nxp,input-select is "lv".
 * @{
 */
#define NAFE13388_LV_HALF_REF    0 /**< (REF/2, REF/2) */
#define NAFE13388_LV_GPIO0_GPIO1 1 /**< (GPIO0, GPIO1) */
#define NAFE13388_LV_REF_COARSE  2 /**< Coarse reference */
#define NAFE13388_LV_VADD        3 /**< Analog/digital supply */
#define NAFE13388_LV_VHDD        4 /**< Positive high voltage supply */
#define NAFE13388_LV_VHSS        5 /**< Negative high voltage supply */
/** @} */

/**
 * @name Second stage SINC filter selection
 *
 * Values for nxp,sinc (ADC_SINC).
 * @{
 */
#define NAFE13388_SINC_BYPASS 0
#define NAFE13388_SINC1       1
#define NAFE13388_SINC2       2
#define NAFE13388_SINC3       3
#define NAFE13388_SINC4       4
/** @} */

/** Highest ADC data rate code (29 programmable rates, 0..28) */
#define NAFE13388_DATA_RATE_CODE_MAX 28

/** Highest channel delay code (CH_DELAY[5:0]) */
#define NAFE13388_CHANNEL_DELAY_CODE_MAX 63

/**
 * @name Clock source selection
 *
 * Values for nxp,clock-source (CK_SRC_SEL).
 * @{
 */
#define NAFE13388_CLK_INTERNAL 0 /**< Internal RC oscillator */
#define NAFE13388_CLK_EXTERNAL 2 /**< External 18.432 MHz clock at XI */
#define NAFE13388_CLK_CRYSTAL  3 /**< 18.432 MHz crystal at XI/XO */
/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ADC_NAFE13388_H_ */
