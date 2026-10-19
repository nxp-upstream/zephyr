/*
 * Copyright 2023-2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * The first regulator enable holds a runtime PM reference until the last
 * disable. Configuration operations take temporary references without
 * enabling the output.
 */

#define DT_DRV_COMPAT nxp_vref

#include <errno.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/dt-bindings/regulator/nxp_vref.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/linear_range.h>
#include <zephyr/sys/util.h>

#include <fsl_device_registers.h>

LOG_MODULE_REGISTER(nxp_vref, CONFIG_REGULATOR_LOG_LEVEL);

#if defined(FSL_FEATURE_VREF_HAS_TRIM2V1) && (FSL_FEATURE_VREF_HAS_TRIM2V1 == 0)
#define NXP_VREF_TRIM_MASK       VREF_UTRIM_VREFTRIM_MASK
#define NXP_VREF_TRIM_SHIFT      VREF_UTRIM_VREFTRIM_SHIFT
#define NXP_VREF_TRIM_IS_FACTORY 1
static const struct linear_range utrim_ranges[] = {
	LINEAR_RANGE_INIT(1200000 - (0x20 * 667), 667U, 0x0U, 0x3FU),
};
#else
#define NXP_VREF_TRIM_MASK       VREF_UTRIM_TRIM2V1_MASK
#define NXP_VREF_TRIM_SHIFT      VREF_UTRIM_TRIM2V1_SHIFT
#define NXP_VREF_TRIM_IS_FACTORY 0
static const struct linear_range utrim_ranges[] = {
#if defined(FSL_FEATURE_VREF_SUPPORT_2V5) && FSL_FEATURE_VREF_SUPPORT_2V5
	LINEAR_RANGE_INIT(1000000, 250000U, 0x0U, 0x2U),
	LINEAR_RANGE_INIT(1800000, 0U, 0x3U, 0x3U),
	LINEAR_RANGE_INIT(2048000, 0U, 0x4U, 0x4U),
	LINEAR_RANGE_INIT(2500000, 0U, 0x5U, 0x5U),
#else
	LINEAR_RANGE_INIT(1000000, 100000U, 0x0U, 0xBU),
#endif
};
#endif

#if defined(FSL_FEATURE_VREF_HAS_LPBGEN) && (FSL_FEATURE_VREF_HAS_LPBGEN == 0)
#define NXP_VREF_BIAS_MASK VREF_CSR_IBIAS_EN_MASK
#else
#define NXP_VREF_BIAS_MASK VREF_CSR_LPBGEN_MASK
#endif

#if defined(FSL_FEATURE_VREF_HAS_LOWPOWER_BUFFER) && (FSL_FEATURE_VREF_HAS_LOWPOWER_BUFFER == 0)
#define NXP_VREF_LP_BUFFER_MASK 0U
#else
#define NXP_VREF_LP_BUFFER_MASK VREF_CSR_LPBG_BUF_EN_MASK
#endif

#define NXP_VREF_ENABLE_MASK (VREF_CSR_HCBGEN_MASK | VREF_CSR_BUF21EN_MASK | \
			     NXP_VREF_BIAS_MASK | NXP_VREF_LP_BUFFER_MASK)
#define NXP_VREF_CONFIG_MASK (VREF_CSR_ICOMPEN_MASK | VREF_CSR_CHOPEN_MASK | VREF_CSR_REGEN_MASK)

struct regulator_nxp_vref_data {
	struct regulator_common_data common;
	struct k_mutex lock;
	uint16_t trim;
	regulator_mode_t mode;
	bool trim_set;
	bool enabled;
	bool power_locked;
};

struct regulator_nxp_vref_config {
	struct regulator_common_config common;
	VREF_Type *base;
	uint16_t buf_start_delay;
	uint16_t bg_start_time;
	bool current_compensation_en;
	bool chop_oscillator_en;
	bool internal_voltage_regulator_en;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
#if defined(FSL_FEATURE_SOC_SPC_COUNT) && FSL_FEATURE_SOC_SPC_COUNT > 0
	SPC_Type *spc;
#endif
};

static void regulator_nxp_vref_power_lock(const struct device *dev, bool enabled)
{
	struct regulator_nxp_vref_data *data = dev->data;

	if (enabled == data->power_locked) {
		return;
	}
	if (enabled) {
		pm_policy_device_power_lock_get(dev);
	} else {
		pm_policy_device_power_lock_put(dev);
	}
	data->power_locked = enabled;
}

static void regulator_nxp_vref_disable_hw(const struct device *dev)
{
	const struct regulator_nxp_vref_config *config = dev->config;

	config->base->CSR &= ~NXP_VREF_ENABLE_MASK;
}

static void regulator_nxp_vref_apply_mode(const struct device *dev)
{
	const struct regulator_nxp_vref_config *config = dev->config;
	const struct regulator_nxp_vref_data *data = dev->data;
	uint32_t csr = config->base->CSR;

	csr &= ~(VREF_CSR_HI_PWR_LV_MASK | VREF_CSR_BUF21EN_MASK);
	if (data->mode != NXP_VREF_MODE_STANDBY) {
		csr |= VREF_CSR_BUF21EN_MASK;
	}
	if (data->mode == NXP_VREF_MODE_HIGH_POWER) {
		csr |= VREF_CSR_HI_PWR_LV_MASK;
	}
	config->base->CSR = csr;
	k_busy_wait(config->buf_start_delay);
}

static int regulator_nxp_vref_enable_hw(const struct device *dev)
{
	const struct regulator_nxp_vref_config *config = dev->config;
	VREF_Type *base = config->base;

	base->CSR |= NXP_VREF_BIAS_MASK | NXP_VREF_LP_BUFFER_MASK;
	k_busy_wait(config->bg_start_time);
	base->CSR |= VREF_CSR_HCBGEN_MASK;

	if (!WAIT_FOR((base->CSR & VREF_CSR_VREFST_MASK) != 0U,
		      config->buf_start_delay + config->bg_start_time, k_busy_wait(1))) {
		regulator_nxp_vref_disable_hw(dev);
		LOG_ERR("Reference did not stabilize");
		return -ETIMEDOUT;
	}

	regulator_nxp_vref_apply_mode(dev);
	return 0;
}

static int regulator_nxp_vref_release(const struct device *dev, int result)
{
	int ret = pm_device_runtime_put(dev);

	if (ret < 0) {
		LOG_ERR("Failed to release runtime reference: %d", ret);
	}
	return result < 0 ? result : ret;
}

static int regulator_nxp_vref_acquire(const struct device *dev)
{
	enum pm_device_state state;
	int ret;

	ret = pm_device_runtime_get(dev);
	if (ret < 0) {
		return ret;
	}
	ret = pm_device_state_get(dev, &state);
	if (ret < 0 || state != PM_DEVICE_STATE_ACTIVE) {
		return regulator_nxp_vref_release(dev, ret < 0 ? ret : -EBUSY);
	}
	return 0;
}

static int regulator_nxp_vref_enable(const struct device *dev)
{
	struct regulator_nxp_vref_data *data = dev->data;
	int ret;

	ret = regulator_nxp_vref_acquire(dev);
	if (ret < 0) {
		return ret;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	ret = regulator_nxp_vref_enable_hw(dev);
	if (ret == 0) {
		data->enabled = true;
		regulator_nxp_vref_power_lock(dev, true);
	}
	k_mutex_unlock(&data->lock);

	return ret < 0 ? regulator_nxp_vref_release(dev, ret) : 0;
}

static int regulator_nxp_vref_disable(const struct device *dev)
{
	struct regulator_nxp_vref_data *data = dev->data;
	enum pm_device_state state;
	int ret;

	ret = pm_device_state_get(dev, &state);
	if (ret < 0 || state != PM_DEVICE_STATE_ACTIVE) {
		return ret < 0 ? ret : -EBUSY;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	regulator_nxp_vref_disable_hw(dev);
	data->enabled = false;
	regulator_nxp_vref_power_lock(dev, false);
	k_mutex_unlock(&data->lock);

	ret = regulator_nxp_vref_release(dev, 0);
	if (ret < 0) {
		k_mutex_lock(&data->lock, K_FOREVER);
		data->enabled = true;
		if (regulator_nxp_vref_enable_hw(dev) == 0) {
			regulator_nxp_vref_power_lock(dev, true);
		}
		k_mutex_unlock(&data->lock);
	}
	return ret;
}

static int regulator_nxp_vref_set_mode(const struct device *dev, regulator_mode_t mode)
{
	struct regulator_nxp_vref_data *data = dev->data;
	int ret;

	if (mode != NXP_VREF_MODE_STANDBY && mode != NXP_VREF_MODE_LOW_POWER &&
	    mode != NXP_VREF_MODE_HIGH_POWER) {
		return -EINVAL;
	}
	ret = regulator_nxp_vref_acquire(dev);
	if (ret < 0) {
		return ret;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	data->mode = mode;
	if (data->enabled) {
		regulator_nxp_vref_apply_mode(dev);
	}
	k_mutex_unlock(&data->lock);
	return regulator_nxp_vref_release(dev, 0);
}

static int regulator_nxp_vref_get_mode(const struct device *dev, regulator_mode_t *mode)
{
	struct regulator_nxp_vref_data *data = dev->data;
	int ret = regulator_nxp_vref_acquire(dev);

	if (ret < 0) {
		return ret;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	*mode = data->mode;
	k_mutex_unlock(&data->lock);
	return regulator_nxp_vref_release(dev, 0);
}

static unsigned int regulator_nxp_vref_count_voltages(const struct device *dev)
{
	ARG_UNUSED(dev);
	return linear_range_group_values_count(utrim_ranges, ARRAY_SIZE(utrim_ranges));
}

static int regulator_nxp_vref_list_voltage(const struct device *dev, unsigned int idx,
					 int32_t *volt_uv)
{
	ARG_UNUSED(dev);
	if (idx > UINT16_MAX) {
		return -EINVAL;
	}
	return linear_range_group_get_value(utrim_ranges, ARRAY_SIZE(utrim_ranges), idx, volt_uv);
}

static int regulator_nxp_vref_set_voltage(const struct device *dev, int32_t min_uv, int32_t max_uv)
{
	const struct regulator_nxp_vref_config *config = dev->config;
	struct regulator_nxp_vref_data *data = dev->data;
	int32_t voltage;
	uint16_t idx;
	int ret;

	if (min_uv > max_uv) {
		return -EINVAL;
	}
	ret = linear_range_group_get_win_index(utrim_ranges, ARRAY_SIZE(utrim_ranges),
					      min_uv, max_uv, &idx);
	if (ret < 0 && ret != -ERANGE) {
		return ret;
	}
	ret = linear_range_group_get_value(utrim_ranges, ARRAY_SIZE(utrim_ranges), idx, &voltage);
	if (ret < 0 || voltage < min_uv || voltage > max_uv) {
		return -EINVAL;
	}
	ret = regulator_nxp_vref_acquire(dev);
	if (ret < 0) {
		return ret;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	config->base->UTRIM = (config->base->UTRIM & ~NXP_VREF_TRIM_MASK) |
			     (((uint32_t)idx << NXP_VREF_TRIM_SHIFT) & NXP_VREF_TRIM_MASK);
	data->trim = idx;
	data->trim_set = true;
	k_mutex_unlock(&data->lock);
	return regulator_nxp_vref_release(dev, 0);
}

static int regulator_nxp_vref_get_voltage(const struct device *dev, int32_t *volt_uv)
{
	const struct regulator_nxp_vref_config *config = dev->config;
	struct regulator_nxp_vref_data *data = dev->data;
	uint16_t idx;
	int ret = regulator_nxp_vref_acquire(dev);

	if (ret < 0) {
		return ret;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	idx = (config->base->UTRIM & NXP_VREF_TRIM_MASK) >> NXP_VREF_TRIM_SHIFT;
	ret = linear_range_group_get_value(utrim_ranges, ARRAY_SIZE(utrim_ranges), idx, volt_uv);
	k_mutex_unlock(&data->lock);
	return regulator_nxp_vref_release(dev, ret);
}

static DEVICE_API(regulator, api) = {
	.enable = regulator_nxp_vref_enable,
	.disable = regulator_nxp_vref_disable,
	.set_mode = regulator_nxp_vref_set_mode,
	.get_mode = regulator_nxp_vref_get_mode,
	.set_voltage = regulator_nxp_vref_set_voltage,
	.get_voltage = regulator_nxp_vref_get_voltage,
	.list_voltage = regulator_nxp_vref_list_voltage,
	.count_voltages = regulator_nxp_vref_count_voltages,
};

static int regulator_nxp_vref_configure_hw(const struct device *dev)
{
	const struct regulator_nxp_vref_config *config = dev->config;
	struct regulator_nxp_vref_data *data = dev->data;
	VREF_Type *base = config->base;
	uint32_t csr;
	int ret;

	if (config->clock_dev != NULL) {
		if (!device_is_ready(config->clock_dev)) {
			LOG_ERR("Clock device not ready");
			return -ENODEV;
		}
		ret = clock_control_configure(config->clock_dev, config->clock_subsys, NULL);
		if (ret != 0 && ret != -ENOSYS) {
			LOG_ERR("Failed to configure clock: %d", ret);
			return ret;
		}
		ret = clock_control_on(config->clock_dev, config->clock_subsys);
		if (ret != 0) {
			LOG_ERR("Failed to enable clock: %d", ret);
			return ret;
		}
	}

#if defined(FSL_FEATURE_SOC_SPC_COUNT) && FSL_FEATURE_SOC_SPC_COUNT > 0
	/* SPC SOC_CNTRL bit 0 supplies the VREF analog block. */
	if (config->spc != NULL) {
		config->spc->ACTIVE_CFG1 |= SPC_ACTIVE_CFG1_SOC_CNTRL(BIT(0));
	}
#endif

	csr = base->CSR & ~(NXP_VREF_CONFIG_MASK | NXP_VREF_ENABLE_MASK | VREF_CSR_HI_PWR_LV_MASK);
	if (config->current_compensation_en) {
		csr |= VREF_CSR_ICOMPEN_MASK;
	}
	if (config->chop_oscillator_en) {
		csr |= VREF_CSR_CHOPEN_MASK;
	}
	if (config->internal_voltage_regulator_en) {
		csr |= VREF_CSR_REGEN_MASK;
	}
	base->CSR = csr;

	if (data->trim_set) {
		base->UTRIM = (base->UTRIM & ~NXP_VREF_TRIM_MASK) |
			      (((uint32_t)data->trim << NXP_VREF_TRIM_SHIFT) & NXP_VREF_TRIM_MASK);
	} else if (!NXP_VREF_TRIM_IS_FACTORY) {
		/* Preserve the factory fine trim while selecting the minimum coarse voltage. */
		base->UTRIM &= ~NXP_VREF_TRIM_MASK;
	}
	return 0;
}

static int regulator_nxp_vref_pm_action(const struct device *dev, enum pm_device_action action)
{
	struct regulator_nxp_vref_data *data = dev->data;
	int ret = 0;

	switch (action) {
	case PM_DEVICE_ACTION_TURN_ON:
		ret = regulator_nxp_vref_configure_hw(dev);
		break;
	case PM_DEVICE_ACTION_RESUME:
		if (data->enabled) {
			ret = regulator_nxp_vref_enable_hw(dev);
			if (ret == 0) {
				regulator_nxp_vref_power_lock(dev, true);
			}
		}
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		regulator_nxp_vref_disable_hw(dev);
		regulator_nxp_vref_power_lock(dev, false);
		break;
	case PM_DEVICE_ACTION_TURN_OFF:
		/* SUSPEND already disabled the analog circuitry. */
		break;
	default:
		ret = -ENOTSUP;
		break;
	}
	return ret;
}

static int regulator_nxp_vref_init(const struct device *dev)
{
	struct regulator_nxp_vref_data *data = dev->data;
	int ret;

	regulator_common_data_init(dev);
	k_mutex_init(&data->lock);
	data->mode = NXP_VREF_MODE_STANDBY;

	ret = pm_device_driver_init(dev, regulator_nxp_vref_pm_action);
	if (ret < 0) {
		return ret;
	}
	ret = pm_device_runtime_enable(dev);
	if (ret < 0) {
		return ret;
	}
	ret = regulator_nxp_vref_acquire(dev);
	if (ret < 0) {
		return ret;
	}
	ret = regulator_common_init(dev, false);
	return regulator_nxp_vref_release(dev, ret);
}

#if defined(FSL_FEATURE_SOC_SPC_COUNT) && FSL_FEATURE_SOC_SPC_COUNT > 0
#define NXP_VREF_SPC_CONFIG(inst) \
	.spc = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, nxp_spc), \
			  ((SPC_Type *)DT_REG_ADDR(DT_INST_PHANDLE(inst, nxp_spc))), (NULL)),
#else
#define NXP_VREF_SPC_CONFIG(inst)
#endif

#define REGULATOR_NXP_VREF_DEFINE(inst)								\
	static struct regulator_nxp_vref_data data_##inst;				\
	static const struct regulator_nxp_vref_config config_##inst = {			\
		.common = REGULATOR_DT_INST_COMMON_CONFIG_INIT(inst),			\
		.base = (VREF_Type *)DT_INST_REG_ADDR(inst),				\
		.buf_start_delay = DT_INST_PROP(inst, nxp_buffer_startup_delay_us),	\
		.bg_start_time = DT_INST_PROP(inst, nxp_bandgap_startup_time_us),		\
		.current_compensation_en = DT_INST_PROP(inst, nxp_current_compensation_en),\
		.chop_oscillator_en = DT_INST_PROP(inst, nxp_chop_oscillator_en),		\
		.internal_voltage_regulator_en =						\
			DT_INST_PROP(inst, nxp_internal_voltage_regulator_en),		\
		.clock_dev = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, clocks),		\
				       (DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst))), (NULL)),\
		.clock_subsys = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, clocks),		\
			((clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, name)),		\
			((clock_control_subsys_t)0)),					\
		NXP_VREF_SPC_CONFIG(inst)						\
	};										\
	PM_DEVICE_DT_INST_DEFINE(inst, regulator_nxp_vref_pm_action);			\
	DEVICE_DT_INST_DEFINE(inst, regulator_nxp_vref_init, PM_DEVICE_DT_INST_GET(inst),	\
			      &data_##inst, &config_##inst, POST_KERNEL,			\
			      CONFIG_REGULATOR_NXP_VREF_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(REGULATOR_NXP_VREF_DEFINE)
