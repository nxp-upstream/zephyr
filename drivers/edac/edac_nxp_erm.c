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
static int errors_cor_get(const struct device *dev)
{
	return 0;
}
static int errors_uc_get(const struct device *dev)
{
	return 0;
}
static int notify_callback_set(const struct device *dev, edac_notify_callback_f cb)
{
	return 0;
}

static DEVICE_API(edac, api) = {
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
	.errors_cor_get = errors_cor_get,
	.errors_uc_get = errors_uc_get,

	/* Notification callback set */
	.notify_cb_set = notify_callback_set,
};
