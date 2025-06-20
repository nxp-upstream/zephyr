/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/edac.h>
#include <fsl_erm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(edac_nxp_erm, CONFIG_EDAC_LOG_LEVEL);

struct nxp_erm_config {
	ERM_Type *base;
};

struct nxp_erm_data {
	mem_addr_t mchbar;
	edac_notify_callback_f cb;
#ifdef CONFIG_EDAC_ERROR_INJECT
	uint32_t inject_error_type;
#endif /* CONFIG_EDAC_ERROR_INJECT */
	/* Error count */
	unsigned int errors_cor;
	unsigned int errors_uc;
};

static int inject_set_param1(const struct device *dev, uint64_t addr)
{
	return 0;
}
static int inject_get_param1(const struct device *dev, uint64_t *value)
{

	return 0;
}
static int inject_set_param2(const struct device *dev, uint64_t mask)
{
	return 0;
}
static int inject_get_param2(const struct device *dev, uint64_t *value)
{
	return 0;
}
static int inject_set_error_type(const struct device *dev, uint32_t error_type)
{
	return 0;
}
static int inject_get_error_type(const struct device *dev, uint32_t *error_type)
{
	return 0;
}
static int inject_error_trigger(const struct device *dev)
{
	return 0;
}

static int ecc_error_log_get(const struct device *dev, uint64_t *value)
{
	return 0;
}

static int ecc_error_log_clear(const struct device *dev)
{
	return 0;
}
static int parity_error_log_get(const struct device *dev, uint64_t *value)
{
	*value = 0;
	return 0;
}
static int parity_error_log_clear(const struct device *dev)
{
	return 0;
}
static int nxp_erm_errors_cor_get(const struct device *dev)
{
	const struct nxp_erm_config *config = dev->config;
	return ERM_GetErrorCount(config->base, 0);
}
static int nxp_erm_errors_uc_get(const struct device *dev)
{
	return 0;
}
static int nxp_erm_notify_callback_set(const struct device *dev, edac_notify_callback_f cb)
{
	struct nxp_erm_data *data = dev->data;

	data->cb = cb;
	return 0;
}

static DEVICE_API(edac, nxp_edac_api) = {
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
	.ecc_error_log_get = ecc_error_log_get,
	.ecc_error_log_clear = ecc_error_log_clear,
	.parity_error_log_get = parity_error_log_get,
	.parity_error_log_clear = parity_error_log_clear,

	/* Get error stats */
	.errors_cor_get = nxp_erm_errors_cor_get,
	.errors_uc_get = nxp_erm_errors_uc_get,

	/* Notification callback set */
	.notify_cb_set = nxp_erm_notify_callback_set,
};

static int nxp_erm_init(const struct device *dev)
{
	const struct nxp_erm_config *config = dev->config;

	ERM_Init(config->base);

	return 0;
}


#define DT_DRV_COMPAT nxp_erm
#define EDAC_NXP_ERM_DEVICE_INIT(n) \
	static const struct nxp_erm_config nxp_erm_##n##_config = {				\
		.base = (ERM_Type *)DT_INST_REG_ADDR(n),	\
	};	\
	static struct nxp_erm_data nxp_erm_##n##_data;				\
	/* Init parent device in order to handle ISR and init. */				\
	DEVICE_DT_INST_DEFINE(n, &nxp_erm_init, NULL,				\
			&nxp_erm_##n##_data, &nxp_erm_##n##_config, POST_KERNEL,			\
			CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &nxp_edac_api);

DT_INST_FOREACH_STATUS_OKAY(EDAC_NXP_ERM_DEVICE_INIT)
