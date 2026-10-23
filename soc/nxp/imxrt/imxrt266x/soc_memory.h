/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_NXP_IMXRT_IMXRT266X_SOC_MEMORY_H_
#define SOC_NXP_IMXRT_IMXRT266X_SOC_MEMORY_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Power mode of an on-chip memory block. */
enum soc_memory_mode {
	SOC_MEMORY_MODE_ACTIVE,     /**< Full power. */
	SOC_MEMORY_MODE_RETENTION,  /**< Contents preserved. */
	SOC_MEMORY_MODE_POWER_DOWN, /**< Powered off, contents lost. */
};

/**
 * @brief Set the power mode of an on-chip memory block while the CPU runs.
 *
 * The new mode applies at once. A block goes between retention and power down
 * only through active: the hardware does not switch between the two directly.
 *
 * @param mem_id Memory block, as a kPOWER_Mem_* identifier of the MCUX HAL
 *               fsl_power.h, for example kPOWER_Mem_ComputeSram6.
 * @param mode   Power mode to apply.
 *
 * @retval 0 Mode applied.
 * @retval -EINVAL The block does not support a software-triggered mode change.
 */
int soc_memory_set_mode(uint32_t mem_id, enum soc_memory_mode mode);

#ifdef __cplusplus
}
#endif

#endif /* SOC_NXP_IMXRT_IMXRT266X_SOC_MEMORY_H_ */
