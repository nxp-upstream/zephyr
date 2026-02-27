/*
 * Copyright 2025 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_mcux_xbar

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/drivers/mux.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nxp_mcux_xbar, CONFIG_MUX_CONTROL_LOG_LEVEL);


#if defined(FSL_FEATURE_XBAR_DSC_REG_WIDTH) && (FSL_FEATURE_XBAR_DSC_REG_WIDTH == 32)
#define NXP_XBAR_REG_WIDTH          32
#define NXP_XBAR_SEL_MASK           0x1FFU
#define NXP_XBAR_REG_READ(base, offset) sys_read32((base) + (offset))
#define NXP_XBAR_REG_WRITE(base, offset, value) sys_write32((value), (base) + (offset))
#else
#define NXP_XBAR_REG_WIDTH          16
#define NXP_XBAR_SEL_MASK           0xFFU
#define NXP_XBAR_REG_READ(base, offset) sys_read16((base) + (offset))
#define NXP_XBAR_REG_WRITE(base, offset, value) sys_write16((value), (base) + (offset))
#endif

struct nxp_xbar_config {
	DEVICE_MMIO_ROM;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
};

struct nxp_xbar_data {
	DEVICE_MMIO_RAM;
	struct k_mutex lock;
};

struct nxp_xbar_control {
	uint32_t input;
	uint32_t output;
	uint32_t value;
};

static void nxp_xbar_calc_sel_offset_shift(uint32_t output, uint32_t *offset, uint32_t *shift)
{
#if (NXP_XBAR_REG_WIDTH == 32)
	/* 32-bit registers: one output per register */
	*offset = output * sizeof(uint32_t);
	*shift = 0;
#else
	/* 16-bit registers: two outputs per register */
	*offset = (output / 2) * sizeof(uint16_t);
	*shift = (output % 2) * 8;
#endif
}

/**
 * @brief Configure XBAR signal connection
 *
 * Configures XBAR to route an input signal to an output signal.
 * The mux_control cells contain: [input_signal, output_signal]
 *
 * @param dev XBAR device
 * @param mux Mux control structure containing cell data
 * @param state State value (currently unused, reserved for future features)
 * @return 0 on success, negative errno on error
 */
static int nxp_xbar_configure(const struct device *dev,
			      struct mux_control *mux, uint32_t state)
{
	struct nxp_xbar_data *data = dev->data;
	uint32_t base = DEVICE_MMIO_GET(dev);
	uint32_t input;
	uint32_t output;
	uint32_t sel_reg_offset;
	uint32_t sel_reg_val;
	uint32_t shift;

	/* Parse cells: first cell is input signal, second is output signal */
	if (mux->len != 3) {
		LOG_ERR("Expected 3 cells (input, output, state), got %u", mux->len);
		return -EINVAL;
	}

	const struct nxp_xbar_control *control = (const struct nxp_xbar_control *)mux->cells;

	input = control->input;
	output = control->output;

	nxp_xbar_calc_sel_offset_shift(output, &sel_reg_offset, &shift);

	k_mutex_lock(&data->lock, K_FOREVER);

	sel_reg_val = NXP_XBAR_REG_READ(base, sel_reg_offset);
	sel_reg_val &= ~(NXP_XBAR_SEL_MASK << shift);
	sel_reg_val |= (input << shift);
	NXP_XBAR_REG_WRITE(base, sel_reg_offset, sel_reg_val);

	k_mutex_unlock(&data->lock);

	LOG_DBG("Configuring XBAR: input=%u -> output=%u", input, output);

	return 0;
}

static DEVICE_API(mux_control, nxp_xbar_driver_api) = {
	.configure = nxp_xbar_configure,
};

/**
 * @brief Initialize XBAR peripheral
 *
 * Enables the XBAR clock and initializes the peripheral.
 * The clock remains enabled during operation to maintain signal routing.
 *
 * @param dev XBAR device
 * @return 0 on success, negative errno on error
 */
static int nxp_xbar_init(const struct device *dev)
{
	const struct nxp_xbar_config *config = dev->config;
	struct nxp_xbar_data *data = dev->data;
	int ret;

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	if (!device_is_ready(config->clock_dev)) {
		LOG_ERR("Clock device not ready");
		return -ENODEV;
	}

	/* Enable XBAR clock */
	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret < 0) {
		LOG_ERR("Failed to enable XBAR clock: %d", ret);
		return ret;
	}

	/* Initialize mutex */
	k_mutex_init(&data->lock);

	return 0;
}

#define NXP_XBAR_INIT(inst)							\
	static const struct nxp_xbar_config nxp_xbar_config_##inst = {		\
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(inst)),			\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),		\
		.clock_subsys = (clock_control_subsys_t)			\
				DT_INST_CLOCKS_CELL(inst, name),		\
	};									\
										\
	static struct nxp_xbar_data nxp_xbar_data_##inst;			\
	DEVICE_DT_INST_DEFINE(inst,						\
			      &nxp_xbar_init,					\
			      NULL,						\
			      &nxp_xbar_data_##inst,				\
			      &nxp_xbar_config_##inst,				\
			      PRE_KERNEL_1,					\
			      CONFIG_MUX_CONTROL_INIT_PRIORITY,			\
			      &nxp_xbar_driver_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_XBAR_INIT)
