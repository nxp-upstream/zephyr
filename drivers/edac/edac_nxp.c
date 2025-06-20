/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/edac.h>
#include <fsl_erm.h>
#include <fsl_eim.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(edac_nxp, CONFIG_EDAC_LOG_LEVEL);

struct edac_nxp_config {
	ERM_Type *erm_base;
	EIM_Type *eim_base;
};

struct edac_nxp_data {
	uint32_t channel;
	uint32_t mask;
	edac_notify_callback_f cb;
#ifdef CONFIG_EDAC_ERROR_INJECT
	uint32_t inject_error_type;
#endif /* CONFIG_EDAC_ERROR_INJECT */
	/* Error count */
	unsigned int errors_cor;
	unsigned int errors_uc;
};

#ifdef CONFIG_EDAC_ERROR_INJECT
static int inject_set_param1(const struct device *dev, uint64_t addr)
{
	/**
	 * Error injection channel descriptor
	 * Bit 15-4: Channel number
	 * Bit 3-0:  DATA_MASK index, 0 represents check bit
	 */
	struct edac_nxp_data *data = dev->data;

	data->channel = (uint32_t)(addr & 0xFFFFFFFF);
	return 0;
}

static int inject_get_param1(const struct device *dev, uint64_t *value)
{
	struct edac_nxp_data *data = dev->data;

	*value = (uint64_t)data->channel;
	return 0;
}

static int inject_set_param2(const struct device *dev, uint64_t mask)
{
	struct edac_nxp_data *data = dev->data;

	data->mask = (uint32_t)(mask & 0xFFFFFFFF);
	return 0;
}

static int inject_get_param2(const struct device *dev, uint64_t *value)
{
	const struct edac_nxp_config *config = dev->config;
	struct edac_nxp_data *data = dev->data;

	*value = (uint64_t)EIM_GetDataBitMask(config->eim_base, data->channel);
	return 0;
}

static int inject_set_error_type(const struct device *dev, uint32_t inject_error_type)
{
	struct edac_nxp_data *data = dev->data;

	data->inject_error_type = inject_error_type;
	return 0;
}

static int inject_get_error_type(const struct device *dev, uint32_t *inject_error_type)
{
	struct edac_nxp_data *data = dev->data;

	*inject_error_type = data->inject_error_type;
	return 0;
}

static int inject_error_trigger(const struct device *dev)
{
	const struct edac_nxp_config *config = dev->config;
	struct edac_nxp_data *data = dev->data;
	uint32_t temp;

	/* Enable memory channel RAMA ECC functionality. */
	SYSCON->ECC_ENABLE_CTRL = 0x2U;
	/* RAMA3(8KB): 0x20008000 ~ 0x2000FFFF */
	volatile uint32_t *ramAddr = (volatile uint32_t *)0x20008000U;
	EIM_InjectDataBitError(config->eim_base, data->channel, data->mask);
    	EIM_EnableErrorInjectionChannels(config->eim_base, 0x20000000U);
    	EIM_EnableGlobalErrorInjection(config->eim_base, true);
    	ERM_EnableInterrupts(config->erm_base, data->channel, kERM_SingleCorrectionIntEnable);
    	temp = *ramAddr;
	return 0;
}
#endif /* CONFIG_EDAC_ERROR_INJECT */
// static int ecc_error_log_get(const struct device *dev, uint64_t *value)
// {

// 	return 0;
// }

// static int ecc_error_log_clear(const struct device *dev)
// {
// 	return 0;
// }

// static int parity_error_log_get(const struct device *dev, uint64_t *value)
// {
// 	*value = 0;
// 	return 0;
// }

// static int parity_error_log_clear(const struct device *dev)
// {
// 	return 0;
// }

static int errors_cor_get(const struct device *dev)
{
	const struct edac_nxp_config *config = dev->config;
	struct edac_nxp_data *data = dev->data;
	return ERM_GetErrorCount(config->erm_base, data->channel);
}

// static int errors_uc_get(const struct device *dev)
// {
// 	return 0;
// }

static int notify_callback_set(const struct device *dev, edac_notify_callback_f cb)
{
	struct edac_nxp_data *data = dev->data;

	data->cb = cb;
	return 0;
}

static DEVICE_API(edac, edac_nxp_api) = {
#if defined(CONFIG_EDAC_ERROR_INJECT)
	/* Error Injection functions */
	.inject_set_param1 = inject_set_param1,
	.inject_get_param1 = inject_get_param1,
	.inject_set_param2 = inject_set_param2,
	.inject_get_param2 = inject_get_param2,
	.inject_set_error_type = inject_set_error_type,
	.inject_get_error_type = inject_get_error_type,
	.inject_error_trigger = inject_error_trigger,
#endif /* CONFIG_EDAC_ERROR_INJECT */

	/* Error reporting & clearing functions */
	// .ecc_error_log_get = ecc_error_log_get,
	// .ecc_error_log_clear = ecc_error_log_clear,
	// .parity_error_log_get = parity_error_log_get,
	// .parity_error_log_clear = parity_error_log_clear,

	/* Get error stats */
	.errors_cor_get = errors_cor_get,
	// .errors_uc_get = errors_uc_get,

	/* Notification callback set */
	.notify_cb_set = notify_callback_set,
};

static int edac_nxp_init(const struct device *dev)
{
	const struct edac_nxp_config *config = dev->config;

	EIM_Init(config->eim_base);
	ERM_Init(config->erm_base);

	return 0;
}


#define DT_DRV_COMPAT nxp_edac
#define EDAC_NXP_DEVICE_INIT(n) \
	static const struct edac_nxp_config edac_nxp_##n##_config = {				\
		.erm_base = (ERM_Type *)DT_REG_ADDR(DT_INST_PHANDLE(n, erm)), \
		.eim_base = (EIM_Type *)DT_REG_ADDR(DT_INST_PHANDLE(n, eim)), \
	};	\
	static struct edac_nxp_data edac_nxp_##n##_data;				\
	/* Init parent device in order to handle ISR and init. */				\
	DEVICE_DT_INST_DEFINE(n, &edac_nxp_init, NULL,				\
			&edac_nxp_##n##_data, &edac_nxp_##n##_config, POST_KERNEL,			\
			CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &edac_nxp_api);

DT_INST_FOREACH_STATUS_OKAY(EDAC_NXP_DEVICE_INIT)
