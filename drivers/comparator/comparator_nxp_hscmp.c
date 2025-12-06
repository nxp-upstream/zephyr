/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/comparator.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nxp_hscmp, CONFIG_COMPARATOR_LOG_LEVEL);

#define DT_DRV_COMPAT nxp_hscmp

struct nxp_hscmp_config {
	HSCMP_Type *base;
	bool enable_stop_mode;
	bool invert_output;
	bool enable_pin_out;
	bool use_unfiltered_output;
	uint8_t filter_count;
	uint8_t filter_period;
	uint8_t positive_mux_input;
	uint8_t negative_mux_input;
	uint8_t dac_value;
	uint8_t dac_vref_source;
	uint8_t hysteresis_mode;
	uint8_t power_mode;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	void (*irq_config_func)(const struct device *dev);
};

struct nxp_hscmp_data {
	uint8_t interrupt_mask;
	comparator_callback_t callback;
	void *user_data;
};

static int nxp_hscmp_get_output(const struct device *dev)
{
	const struct nxp_hscmp_config *config = dev->config;

	return (config->base->CSR & HSCMP_CSR_COUT_MASK) ? 1 : 0;
}

static int nxp_hscmp_set_trigger(const struct device *dev,
				   enum comparator_trigger trigger)
{
	const struct nxp_hscmp_config *config = dev->config;
	struct nxp_hscmp_data *data = dev->data;

	config->base->IER &= ~(HSCMP_IER_CFR_IE_MASK | HSCMP_IER_CFF_IE_MASK);
	data->interrupt_mask = 0U;

	switch (trigger) {
	case COMPARATOR_TRIGGER_NONE:
		break;
	case COMPARATOR_TRIGGER_RISING_EDGE:
		data->interrupt_mask = HSCMP_IER_CFR_IE_MASK;
		break;
	case COMPARATOR_TRIGGER_FALLING_EDGE:
		data->interrupt_mask = HSCMP_IER_CFF_IE_MASK;
		break;
	case COMPARATOR_TRIGGER_BOTH_EDGES:
		data->interrupt_mask = HSCMP_IER_CFR_IE_MASK | HSCMP_IER_CFF_IE_MASK;
		break;
	default:
		LOG_ERR("Invalid trigger type.");
		return -EINVAL;
	}

	/* Clear latched status flags before enabling interrupts. */
	config->base->CSR |= (HSCMP_CSR_CFF_MASK | HSCMP_CSR_CFR_MASK);

	if ((data->interrupt_mask != 0U) && (data->callback != NULL)) {
		config->base->IER |= data->interrupt_mask;
	}

	return 0;
}

static int nxp_hscmp_trigger_is_pending(const struct device *dev)
{
	const struct nxp_hscmp_config *config = dev->config;
	struct nxp_hscmp_data *data = dev->data;
	uint32_t status_flags;

	status_flags = config->base->CSR & (HSCMP_CSR_CFF_MASK | HSCMP_CSR_CFR_MASK);
	config->base->CSR |= (HSCMP_CSR_CFF_MASK | HSCMP_CSR_CFR_MASK);

	if (((data->interrupt_mask & HSCMP_IER_CFF_IE_MASK) != 0U) &&
	    ((status_flags & HSCMP_CSR_CFF_MASK) != 0U)) {
		return 1;
	}

	if (((data->interrupt_mask & HSCMP_IER_CFR_IE_MASK) != 0U) &&
	    ((status_flags & HSCMP_CSR_CFR_MASK) != 0U)) {
		return 1;
	}

	return 0;
}

static int nxp_hscmp_set_trigger_callback(const struct device *dev,
					 comparator_callback_t callback,
					 void *user_data)
{
	const struct nxp_hscmp_config *config = dev->config;
	struct nxp_hscmp_data *data = dev->data;

	config->base->CCR0 &= ~HSCMP_CCR0_CMP_EN_MASK;

	data->callback = callback;
	data->user_data = user_data;

	/* Clear any pending flags when (re)arming the callback. */
	config->base->CSR |= (HSCMP_CSR_CFF_MASK | HSCMP_CSR_CFR_MASK);

	if ((data->callback != NULL) && (data->interrupt_mask != 0U)) {
		config->base->IER |= data->interrupt_mask;
	} else {
		config->base->IER &= ~(HSCMP_IER_CFR_IE_MASK | HSCMP_IER_CFF_IE_MASK);
	}

	config->base->CCR0 |= HSCMP_CCR0_CMP_EN_MASK;

	return 0;
}

static void nxp_hscmp_irq_handler(const struct device *dev)
{
	const struct nxp_hscmp_config *config = dev->config;
	struct nxp_hscmp_data *data = dev->data;

	/* Clear interrupt status flags */
	config->base->CSR |= (HSCMP_CSR_CFF_MASK | HSCMP_CSR_CFR_MASK);

	if (data->callback == NULL) {
		LOG_WRN("No callback can be executed.");
		return;
	}

	data->callback(dev, data->user_data);
}

static int nxp_hscmp_pm_callback(const struct device *dev,
			     enum pm_device_action action)
{
	const struct nxp_hscmp_config *config = dev->config;

	if (action == PM_DEVICE_ACTION_RESUME) {
		config->base->CCR0 |= HSCMP_CCR0_CMP_EN_MASK;
		return 0;
	}

	if (action == PM_DEVICE_ACTION_SUSPEND) {
		config->base->CCR0 &= ~HSCMP_CCR0_CMP_EN_MASK;
		return 0;
	}

	return -ENOTSUP;
}

static int nxp_hscmp_init(const struct device *dev)
{
	const struct nxp_hscmp_config *config = dev->config;
	HSCMP_Type *base = config->base;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		LOG_ERR("Clock device is not ready");
		return -ENODEV;
	}

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		LOG_ERR("Device clock turn on failed");
		return ret;
	}

	if (!device_is_ready(config->reset.dev)) {
		LOG_ERR("Reset device is not ready");
		return -ENODEV;
	}

	ret = reset_line_assert(config->reset.dev, config->reset.id);
	if (ret != 0) {
		LOG_ERR("Failed to assert reset line");
		return ret;
	}

	ret = reset_line_deassert(config->reset.dev, config->reset.id);
	if (ret != 0) {
		LOG_ERR("Failed to deassert reset line");
		return ret;
	}

	/* Disable comparator before configuring. */
	base->CCR0 &= ~HSCMP_CCR0_CMP_EN_MASK;

	uint32_t ccr0 = base->CCR0;
	ccr0 &= ~(HSCMP_CCR0_CMP_STOP_EN_MASK | HSCMP_CCR0_LINKEN_MASK);
	ccr0 |= HSCMP_CCR0_CMP_STOP_EN(config->enable_stop_mode ? 1U : 0U);
	base->CCR0 = ccr0;

	uint32_t ccr1 = base->CCR1;
	ccr1 &= ~(HSCMP_CCR1_COUT_INV_MASK | HSCMP_CCR1_COUT_PEN_MASK |
		  HSCMP_CCR1_COUT_SEL_MASK | HSCMP_CCR1_FILT_CNT_MASK |
		  HSCMP_CCR1_FILT_PER_MASK | HSCMP_CCR1_SAMPLE_EN_MASK |
		  HSCMP_CCR1_WINDOW_EN_MASK);
	ccr1 |= HSCMP_CCR1_COUT_INV(config->invert_output ? 1U : 0U);
	ccr1 |= HSCMP_CCR1_COUT_PEN(config->enable_pin_out ? 1U : 0U);

	if (config->use_unfiltered_output) {
		ccr1 |= HSCMP_CCR1_COUT_SEL_MASK;
	} else {
		ccr1 &= ~HSCMP_CCR1_COUT_SEL_MASK;
		if (config->filter_count != 0U) {
			ccr1 |= HSCMP_CCR1_FILT_CNT(config->filter_count);
			ccr1 |= HSCMP_CCR1_FILT_PER(config->filter_period);
		}
	}

	base->CCR1 = ccr1;

	uint32_t ccr2 = base->CCR2;
	ccr2 &= ~(HSCMP_CCR2_CMP_HPMD_MASK | HSCMP_CCR2_CMP_NPMD_MASK |
		  HSCMP_CCR2_HYSTCTR_MASK | HSCMP_CCR2_PSEL_MASK |
		  HSCMP_CCR2_MSEL_MASK | HSCMP_CCR2_INPSEL_MASK |
		  HSCMP_CCR2_INMSEL_MASK);

	switch (config->power_mode) {
	case 1U: /* high speed */
		ccr2 |= HSCMP_CCR2_CMP_HPMD_MASK;
		break;
	case 2U: /* nano power */
		ccr2 |= HSCMP_CCR2_CMP_NPMD_MASK;
		break;
	default:
		break;
	}

	ccr2 |= HSCMP_CCR2_HYSTCTR(config->hysteresis_mode);
	ccr2 |= HSCMP_CCR2_PSEL(config->positive_mux_input);
	ccr2 |= HSCMP_CCR2_MSEL(config->negative_mux_input);

	if (config->positive_mux_input == 7U) {
		ccr2 |= HSCMP_CCR2_INPSEL(0U);
	} else {
		ccr2 |= HSCMP_CCR2_INPSEL(1U);
	}

	if (config->negative_mux_input == 7U) {
		ccr2 |= HSCMP_CCR2_INMSEL(0U);
	} else {
		ccr2 |= HSCMP_CCR2_INMSEL(1U);
	}

	base->CCR2 = ccr2;

	bool use_dac = (config->positive_mux_input == 7U) || (config->negative_mux_input == 7U);
	uint32_t dcr = base->DCR;
	dcr &= ~(HSCMP_DCR_DAC_EN_MASK | HSCMP_DCR_DAC_HPMD_MASK | HSCMP_DCR_VRSEL_MASK |
		HSCMP_DCR_DACOE_MASK | HSCMP_DCR_DAC_DATA_MASK);

	if (use_dac) {
		dcr |= HSCMP_DCR_VRSEL(config->dac_vref_source);
		dcr |= HSCMP_DCR_DAC_DATA(config->dac_value);
		dcr |= HSCMP_DCR_DAC_EN_MASK;
	}

	base->DCR = dcr;

	/* Clear status flags before enabling interrupts or the comparator. */
	base->CSR |= (HSCMP_CSR_CFF_MASK | HSCMP_CSR_CFR_MASK | HSCMP_CSR_RRF_MASK);
	base->IER &= ~(HSCMP_IER_CFR_IE_MASK | HSCMP_IER_CFF_IE_MASK);

	config->irq_config_func(dev);

	base->CCR0 |= HSCMP_CCR0_CMP_EN_MASK;

	return pm_device_driver_init(dev, nxp_hscmp_pm_callback);
}

static DEVICE_API(comparator, nxp_hscmp_api) = {
	.get_output = nxp_hscmp_get_output,
	.set_trigger = nxp_hscmp_set_trigger,
	.set_trigger_callback = nxp_hscmp_set_trigger_callback,
	.trigger_is_pending = nxp_hscmp_trigger_is_pending,
};

#define HSCMP_DT_ENUM_IDX(inst, prop, default_val)						\
	DT_ENUM_IDX_OR(DT_DRV_INST(inst), prop, default_val)

#define HSCMP_DT_PSEL_IDX(inst, prop)								\
	COND_CODE_1(DT_ENUM_HAS_VALUE(DT_DRV_INST(inst), prop, DAC),				\
		(7U), (HSCMP_DT_ENUM_IDX(inst, prop, 0)))

#define HSCMP_DT_MSEL_IDX(inst, prop)	HSCMP_DT_PSEL_IDX(inst, prop)

#define NXP_HSCMP_DEVICE_INIT(inst)								\
	static struct nxp_hscmp_data _CONCAT(data, inst) = {					\
		.interrupt_mask = 0U,								\
	};											\
												\
	PM_DEVICE_DT_INST_DEFINE(inst, nxp_hscmp_pm_callback);					\
												\
	static void _CONCAT(nxp_hscmp_irq_config, inst)(const struct device *dev)		\
	{											\
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),			\
			nxp_hscmp_irq_handler, DEVICE_DT_INST_GET(inst), 0);			\
		irq_enable(DT_INST_IRQN(inst));							\
	}											\
												\
	static const struct nxp_hscmp_config _CONCAT(config, inst) = {				\
		.base = (HSCMP_Type *)DT_INST_REG_ADDR(inst),					\
		.enable_stop_mode = DT_INST_PROP(inst, enable_stop_mode),			\
		.invert_output = DT_INST_PROP(inst, invert_output),				\
		.enable_pin_out = DT_INST_PROP(inst, enable_pin_out),				\
		.use_unfiltered_output = DT_INST_PROP(inst, use_unfiltered_output),		\
		.filter_count = DT_INST_PROP_OR(inst, filter_count, 0),				\
		.filter_period = DT_INST_PROP_OR(inst, filter_period, 0),			\
		.positive_mux_input = HSCMP_DT_PSEL_IDX(inst, positive_mux_input),		\
		.negative_mux_input = HSCMP_DT_MSEL_IDX(inst, negative_mux_input),		\
		.dac_value = DT_INST_PROP_OR(inst, dac_value, 0),				\
		.dac_vref_source = HSCMP_DT_ENUM_IDX(inst, dac_vref_source, 0),			\
		.hysteresis_mode = DT_INST_ENUM_IDX_OR(inst, hysteresis_mode, 0),		\
		.power_mode = HSCMP_DT_ENUM_IDX(inst, power_mode, 0),				\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),				\
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, name),	\
		.reset = RESET_DT_SPEC_INST_GET(inst),						\
		.irq_config_func = _CONCAT(nxp_hscmp_irq_config, inst),				\
	};											\
												\
	DEVICE_DT_INST_DEFINE(inst, nxp_hscmp_init, PM_DEVICE_DT_INST_GET(inst),		\
		&_CONCAT(data, inst), &_CONCAT(config, inst), POST_KERNEL,			\
		CONFIG_COMPARATOR_INIT_PRIORITY, &nxp_hscmp_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_HSCMP_DEVICE_INIT)
