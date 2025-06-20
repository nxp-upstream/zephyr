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

#ifdef CONFIG_EDAC_ERROR_INJECT
#define EDAC_NXP_SINGLE_BIT_ERROR_MASK 0x1U
#define EDAC_NXP_DOUBLE_BIT_ERROR_MASK 0x3U

struct edac_nxp_eim_channel {
	uint32_t start_address;
	uint32_t inject_enable;
	uint32_t ecc_enable;
	uint8_t channel_id;
};
#endif /* CONFIG_EDAC_ERROR_INJECT */

struct edac_nxp_config {
	ERM_Type *erm_base;
#ifdef CONFIG_EDAC_ERROR_INJECT
	EIM_Type *eim_base;
	const struct edac_nxp_eim_channel *eim_channels;
	uint8_t eim_channel_num;
#endif /* CONFIG_EDAC_ERROR_INJECT */
	const int *erm_channels;
	uint8_t erm_channel_num;
	void (*irq_config_func)(const struct device *dev);
};

struct edac_nxp_data {
	edac_notify_callback_f cb;
#ifdef CONFIG_EDAC_ERROR_INJECT
	uint32_t eim_channel;
	uint32_t inject_error_type;
#endif /* CONFIG_EDAC_ERROR_INJECT */
	uint32_t erm_channel;
};

#ifdef CONFIG_SOC_SERIES_MCXN
static void enable_ecc(uint32_t mask)
{
	/* Enable memory channel RAM ECC functionality. */
	SYSCON->ECC_ENABLE_CTRL = mask;
}
#elif CONFIG_SOC_SERIES_MCXA
static void enable_ecc(uint32_t mask)
{
	/* Enable memory channel RAM ECC functionality. */
	SYSCON->RAM_CTRL = mask;
}
#else
static void enable_ecc(uint32_t mask)
{
}
#endif

static bool check_erm_channel(const int *erm_channels, size_t size, uint32_t value) {
    for (size_t i = 0; i < size; i++) {
        if (erm_channels[i] == value) {
            return true;
        }
    }
    return false;
}

static bool check_eim_channel(const struct edac_nxp_eim_channel *eim_channels, size_t size, uint32_t value) {
    for (size_t i = 0; i < size; i++) {
	if (eim_channels[i].channel_id == value) {
	    return true;
	}
    }
    return false;
}

#ifdef CONFIG_EDAC_ERROR_INJECT
static int inject_set_param1(const struct device *dev, uint64_t channel)
{
	struct edac_nxp_data *data = dev->data;
	const struct edac_nxp_config *config = dev->config;
	if (!check_eim_channel(config->eim_channels, config->eim_channel_num, (uint32_t)channel)) {
		LOG_ERR("Invalid EIM channel %llx", channel);
		return -EINVAL;
	}

	data->eim_channel = (uint32_t)(channel & 0xFFFFFFFF);
	return 0;
}

static int inject_get_param1(const struct device *dev, uint64_t *value)
{
	struct edac_nxp_data *data = dev->data;

	*value = (uint64_t)data->eim_channel;
	return 0;
}

static int inject_set_param2(const struct device *dev, uint64_t channel)
{
	struct edac_nxp_data *data = dev->data;
	const struct edac_nxp_config *config = dev->config;
	if (!check_erm_channel(config->erm_channels, config->erm_channel_num, (uint32_t)channel)) {
		LOG_ERR("Invalid ERM channel %llx", channel);
		return -EINVAL;
	}

	data->erm_channel = (uint32_t)(channel & 0xFFFFFFFF);
	return 0;
}

static int inject_get_param2(const struct device *dev, uint64_t *value)
{
	struct edac_nxp_data *data = dev->data;

	*value = (uint64_t)data->erm_channel;
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
	uint32_t interrupt_type;
	uint32_t inject_data;
	const struct edac_nxp_eim_channel eim_channel = config->eim_channels[data->eim_channel];

	switch (data->inject_error_type) {
	case EDAC_ERROR_TYPE_DRAM_COR:
		interrupt_type = kERM_SingleCorrectionIntEnable;
		inject_data = EDAC_NXP_SINGLE_BIT_ERROR_MASK;
		break;
	case EDAC_ERROR_TYPE_DRAM_UC:
		interrupt_type = kERM_NonCorrectableIntEnable;
		inject_data = EDAC_NXP_DOUBLE_BIT_ERROR_MASK;
		break;
	default:
		inject_data = 0;
		return 0;
	}

	enable_ecc(eim_channel.ecc_enable);
	volatile uint32_t *ramAddr = (volatile uint32_t *)eim_channel.start_address;
	EIM_InjectDataBitError(config->eim_base, data->eim_channel, inject_data);
	EIM_EnableErrorInjectionChannels(config->eim_base, eim_channel.inject_enable);
	EIM_EnableGlobalErrorInjection(config->eim_base, true);
	if (data->eim_channel != data->erm_channel) {
		LOG_WRN("EIM inject error on channel %x but ERM was listening on channel %x",
			data->eim_channel, data->erm_channel);
	}
	ERM_EnableInterrupts(config->erm_base, data->erm_channel, interrupt_type);
	/* Read the memory to trigger an ECC error */
    	interrupt_type = *ramAddr;
	return 0;
}
#endif /* CONFIG_EDAC_ERROR_INJECT */

static int ecc_error_log_get(const struct device *dev, uint64_t *value)
{
	const struct edac_nxp_config *config = dev->config;
	struct edac_nxp_data *data = dev->data;
	*value = ERM_GetSyndrome(config->erm_base, data->erm_channel);
	return 0;
}

static int ecc_error_log_clear(const struct device *dev)
{
	const struct edac_nxp_config *config = dev->config;
	struct edac_nxp_data *data = dev->data;
	ERM_ClearInterruptStatus(config->erm_base, data->erm_channel, kERM_AllInterruptsEnable);
	return 0;
}

static int errors_cor_get(const struct device *dev)
{
	const struct edac_nxp_config *config = dev->config;
	struct edac_nxp_data *data = dev->data;
	return ERM_GetErrorCount(config->erm_base, data->erm_channel);
}

static int notify_callback_set(const struct device *dev, edac_notify_callback_f cb)
{
	struct edac_nxp_data *data = dev->data;
	data->cb = cb;
	return 0;
}

static void edac_nxp_isr(const struct device *dev)
{
	const struct edac_nxp_config *config = dev->config;
	struct edac_nxp_data *data = dev->data;

	LOG_DBG("ERM channel %d interrupt received, address 0x%x", data->erm_channel, ERM_GetMemoryErrorAddr(config->erm_base, data->erm_channel));
	ecc_error_log_clear(dev);
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
	.ecc_error_log_get = ecc_error_log_get,
	.ecc_error_log_clear = ecc_error_log_clear,

	/* Get error stats */
	.errors_cor_get = errors_cor_get,

	/* Notification callback set */
	.notify_cb_set = notify_callback_set,
};

static int edac_nxp_init(const struct device *dev)
{
	const struct edac_nxp_config *config = dev->config;
	struct edac_nxp_data *data = dev->data;
#ifdef CONFIG_EDAC_ERROR_INJECT

	EIM_Init(config->eim_base);
#endif /* CONFIG_EDAC_ERROR_INJECT */
	ERM_Init(config->erm_base);
	config->irq_config_func(dev);
	if (!check_erm_channel(config->erm_channels, config->erm_channel_num, CONFIG_EDAC_NXP_ERM_DEFAULT_CHANNEL)) {
		LOG_ERR("Invalid ERM channel %d", CONFIG_EDAC_NXP_ERM_DEFAULT_CHANNEL);
		return -EINVAL;
	}
	data->erm_channel = CONFIG_EDAC_NXP_ERM_DEFAULT_CHANNEL;

	return 0;
}

#define DT_DRV_COMPAT nxp_edac

#define EDAC_NXP_COMMON_CHANNELS(n) \
    static const int edac_nxp_##n##_channels[] = \
        DT_PROP(DT_INST_PHANDLE(n, erm), channels);

#define EDAC_NXP_IRQ_CONFIG(n) \
    static void edac_nxp_irq_##n(const struct device *dev) \
    { \
        IRQ_CONNECT(DT_IRQ_BY_IDX(DT_INST_PHANDLE(n, erm), 0, irq), \
            DT_IRQ_BY_IDX(DT_INST_PHANDLE(n, erm), 0, priority), \
            edac_nxp_isr, DEVICE_DT_INST_GET(n), 0); \
        irq_enable(DT_IRQ_BY_IDX(DT_INST_PHANDLE(n, erm), 0, irq)); \
        IRQ_CONNECT(DT_IRQ_BY_IDX(DT_INST_PHANDLE(n, erm), 1, irq), \
            DT_IRQ_BY_IDX(DT_INST_PHANDLE(n, erm), 1, priority), \
            edac_nxp_isr, DEVICE_DT_INST_GET(n), 0); \
        irq_enable(DT_IRQ_BY_IDX(DT_INST_PHANDLE(n, erm), 1, irq)); \
    }

#ifdef CONFIG_EDAC_ERROR_INJECT
/* Initializes an element of the eim channel device pointer array */
#define NXP_EIM_CHANNEL_DEV_ARRAY_INIT(node)					\
{                     								\
	.channel_id = DT_PROP(node, channel_id),				\
    	.start_address = DT_PROP(node, start_address), 				\
    	.inject_enable = DT_PROP(node, inject_enable), 				\
    	.ecc_enable = DT_PROP(node, ecc_enable), 				\
},
#define EDAC_NXP_EIM_CHANNELS(n) \
    static const struct edac_nxp_eim_channel nxp_eim_##n##_channels[] = { \
        DT_FOREACH_CHILD_STATUS_OKAY(DT_INST_PHANDLE(n, eim), \
            NXP_EIM_CHANNEL_DEV_ARRAY_INIT) \
    };
#define EDAC_NXP_EIM_CONFIG_FIELDS(n) \
    .eim_base = (EIM_Type *)DT_REG_ADDR(DT_INST_PHANDLE(n, eim)), \
    .eim_channels = nxp_eim_##n##_channels, \
    .eim_channel_num = ARRAY_SIZE(nxp_eim_##n##_channels),
#else
#define EDAC_NXP_EIM_CHANNELS(n)
#define EDAC_NXP_EIM_CONFIG_FIELDS(n)
#endif

#define EDAC_NXP_DEVICE_CONFIG_INIT(n) \
    EDAC_NXP_COMMON_CHANNELS(n) \
    EDAC_NXP_EIM_CHANNELS(n) \
    static const struct edac_nxp_config edac_nxp_##n##_config = { \
        .erm_base = (ERM_Type *)DT_REG_ADDR(DT_INST_PHANDLE(n, erm)), \
        EDAC_NXP_EIM_CONFIG_FIELDS(n) \
        .erm_channels = edac_nxp_##n##_channels, \
        .erm_channel_num = ARRAY_SIZE(edac_nxp_##n##_channels), \
        .irq_config_func = edac_nxp_irq_##n, \
    };

#define EDAC_NXP_DEVICE_INIT(n) \
    EDAC_NXP_IRQ_CONFIG(n) \
    EDAC_NXP_DEVICE_CONFIG_INIT(n) \
    static struct edac_nxp_data edac_nxp_##n##_data; \
    DEVICE_DT_INST_DEFINE(n, &edac_nxp_init, NULL, \
        &edac_nxp_##n##_data, &edac_nxp_##n##_config, \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &edac_nxp_api);

DT_INST_FOREACH_STATUS_OKAY(EDAC_NXP_DEVICE_INIT)
