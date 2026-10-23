/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup wuc_interface
 * @brief NXP POWERCON wakeup source encodings.
 *
 * The layout is the one of power_wakeup_source_t in the MCUX HAL: bits [31:28]
 * select the wakeup path, the remaining bits are the source index on that path.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_WUC_WUC_NXP_POWERCON_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_WUC_WUC_NXP_POWERCON_H_

/** Bit position of the wakeup path field in an encoded wakeup source. */
#define NXP_POWERCON_WUC_PATH_POS 28

/** @name Wakeup paths */
/** @{ */
/** Interrupt wakeup through the CMC0 interrupt wakeup mask; indexed by NVIC interrupt number. */
#define NXP_POWERCON_WUC_PATH_IRQ      0U
/** MAIN domain DMA request wakeup through the CMC1 DMA wakeup mask; indexed as in the RM. */
#define NXP_POWERCON_WUC_PATH_MAIN_DMA 1U
/** WAKE domain DMA request wakeup through the CMC2 DMA wakeup mask; indexed as in the RM. */
#define NXP_POWERCON_WUC_PATH_WAKE_DMA 2U
/** @} */

/**
 * @brief Encode a wakeup source.
 *
 * @param path  Wakeup path, one of the NXP_POWERCON_WUC_PATH_* values.
 * @param index Source index on that path.
 */
#define NXP_POWERCON_WUC_ENCODE(path, index) (((path) << NXP_POWERCON_WUC_PATH_POS) | (index))

/**
 * @brief Encode an interrupt wakeup source.
 *
 * @param irqn NVIC interrupt number of the source.
 */
#define NXP_POWERCON_WUC_IRQ(irqn) NXP_POWERCON_WUC_ENCODE(NXP_POWERCON_WUC_PATH_IRQ, irqn)

/**
 * @brief Encode a MAIN domain DMA request wakeup source.
 *
 * @param index DMA wakeup source index of the requesting peripheral as listed in the RM,
 *              not an eDMA channel number.
 */
#define NXP_POWERCON_WUC_MAIN_DMA(index)                                                           \
	NXP_POWERCON_WUC_ENCODE(NXP_POWERCON_WUC_PATH_MAIN_DMA, index)

/**
 * @brief Encode a WAKE domain DMA request wakeup source.
 *
 * @param index DMA wakeup source index of the requesting peripheral as listed in the RM,
 *              not an eDMA channel number.
 */
#define NXP_POWERCON_WUC_WAKE_DMA(index)                                                           \
	NXP_POWERCON_WUC_ENCODE(NXP_POWERCON_WUC_PATH_WAKE_DMA, index)

/** @name Named wakeup sources */
/** @{ */
/** VBAT LPTMR interrupt (VBAT_LPTMR_IRQn). */
#define NXP_POWERCON_WUC_VBAT_LPTMR    NXP_POWERCON_WUC_IRQ(250U)
/** VBAT GPIO interrupt channel 0 (VBAT_GPIO_CH0_IRQn). */
#define NXP_POWERCON_WUC_VBAT_GPIO_CH0 NXP_POWERCON_WUC_IRQ(252U)
/** VBAT GPIO interrupt channel 1 (VBAT_GPIO_CH1_IRQn). */
#define NXP_POWERCON_WUC_VBAT_GPIO_CH1 NXP_POWERCON_WUC_IRQ(253U)
/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_WUC_WUC_NXP_POWERCON_H_ */
