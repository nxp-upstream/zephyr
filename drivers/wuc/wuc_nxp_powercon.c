/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_powercon_wuc

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/wuc.h>
#include <zephyr/dt-bindings/wuc/wuc_nxp_powercon.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#include <fsl_power.h>

#define POWERCON_WUC_PATH(id)  ((uint32_t)(id) >> NXP_POWERCON_WUC_PATH_POS)
#define POWERCON_WUC_INDEX(id) ((uint32_t)(id) & BIT_MASK(NXP_POWERCON_WUC_PATH_POS))

/* Number of sources the CMC wakeup mask register array of each path has a bit for. */
#define POWERCON_WUC_IRQ_SOURCE_COUNT                                                              \
	(POWERCON_CMC_CTRL_IRQ_WAKEUP_MASK_COUNT * NUM_BITS(uint32_t))
#define POWERCON_WUC_DMA_SOURCE_COUNT                                                              \
	(POWERCON_CMC_CTRL_DMA_WAKEUP_MASK_COUNT * NUM_BITS(uint32_t))

#define POWERCON_WUC_ENCODING_MATCHES(encode, source)                                              \
	(encode(POWERCON_WUC_INDEX(source)) == (source))

/*
 * wuc_nxp_powercon.h restates the power_wakeup_source_t encoding for devicetree, so
 * each path's encoding and each named source is checked against fsl_power.h.
 */
BUILD_ASSERT(POWERCON_WUC_ENCODING_MATCHES(NXP_POWERCON_WUC_IRQ, kPOWER_WakeupIrq_VbatRtc));
BUILD_ASSERT(POWERCON_WUC_ENCODING_MATCHES(NXP_POWERCON_WUC_MAIN_DMA,
					   kPOWER_WakeupMainDma_Lpuart0));
BUILD_ASSERT(POWERCON_WUC_ENCODING_MATCHES(NXP_POWERCON_WUC_WAKE_DMA,
					   kPOWER_WakeupWakeDma_VbatLptmr));
BUILD_ASSERT(NXP_POWERCON_WUC_VBAT_LPTMR == kPOWER_WakeupIrq_VbatLptmr);
BUILD_ASSERT(NXP_POWERCON_WUC_VBAT_GPIO_CH0 == kPOWER_WakeupIrq_VbatGpioCh0);
BUILD_ASSERT(NXP_POWERCON_WUC_VBAT_GPIO_CH1 == kPOWER_WakeupIrq_VbatGpioCh1);

typedef void (*powercon_wuc_hal_op_t)(power_wakeup_source_t source);

/*
 * The HAL updates a shadow mask word shared by 32 sources with a plain
 * read-modify-write, and wakeup sources are also changed from interrupt context:
 * counter_mcux_lptmr.c disables its source from the alarm interrupt.
 */
static struct k_spinlock powercon_wuc_lock;

/*
 * The HAL ignores an unknown path or an out-of-range index without telling the
 * caller, so an invalid id is caught here.
 */
static bool powercon_wuc_id_is_valid(uint32_t id)
{
	uint32_t index = POWERCON_WUC_INDEX(id);

	switch (POWERCON_WUC_PATH(id)) {
	case NXP_POWERCON_WUC_PATH_IRQ:
		return index < POWERCON_WUC_IRQ_SOURCE_COUNT;
	case NXP_POWERCON_WUC_PATH_MAIN_DMA:
	case NXP_POWERCON_WUC_PATH_WAKE_DMA:
		return index < POWERCON_WUC_DMA_SOURCE_COUNT;
	default:
		return false;
	}
}

/*
 * Only the HAL's shadow state changes here: every POWER_Enter*() writes it to the
 * CMC wakeup mask registers before its WFI.
 */
static int powercon_wuc_update(uint32_t id, powercon_wuc_hal_op_t hal_op)
{
	if (!powercon_wuc_id_is_valid(id)) {
		return -EINVAL;
	}

	K_SPINLOCK(&powercon_wuc_lock) {
		hal_op((power_wakeup_source_t)id);
	}

	return 0;
}

static int powercon_wuc_enable(const struct device *dev, uint32_t id)
{
	ARG_UNUSED(dev);

	return powercon_wuc_update(id, POWER_EnableWakeupSource);
}

static int powercon_wuc_disable(const struct device *dev, uint32_t id)
{
	ARG_UNUSED(dev);

	return powercon_wuc_update(id, POWER_DisableWakeupSource);
}

/*
 * No .triggered/.clear: fsl_power.h has no per-source wakeup status, and the WUC
 * API reports the missing callbacks as -ENOSYS.
 */
static DEVICE_API(wuc, powercon_wuc_api) = {
	.enable = powercon_wuc_enable,
	.disable = powercon_wuc_disable,
};

#define POWERCON_WUC_DEFINE(inst)                                                                  \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, NULL, PRE_KERNEL_1,                          \
			      CONFIG_WUC_INIT_PRIORITY, &powercon_wuc_api);

DT_INST_FOREACH_STATUS_OKAY(POWERCON_WUC_DEFINE)
