/*
 * Copyright (c) 2023, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT adi_adp5585_gpio

LOG_MODULE_REGISTER(adp5585_gpio, CONFIG_GPIO_LOG_LEVEL);

#define ADP5585_ID			0x00
#define ADP5585_INT_STATUS		0x01
#define ADP5585_STATUS			0x02
#define ADP5585_FIFO_1			0x03
#define ADP5585_FIFO_2			0x04
#define ADP5585_FIFO_3			0x05
#define ADP5585_FIFO_4			0x06
#define ADP5585_FIFO_5			0x07
#define ADP5585_FIFO_6			0x08
#define ADP5585_FIFO_7			0x09
#define ADP5585_FIFO_8			0x0A
#define ADP5585_FIFO_9			0x0B
#define ADP5585_FIFO_10			0x0C
#define ADP5585_FIFO_11			0x0D
#define ADP5585_FIFO_12			0x0E
#define ADP5585_FIFO_13			0x0F
#define ADP5585_FIFO_14			0x10
#define ADP5585_FIFO_15			0x11
#define ADP5585_FIFO_16			0x12
#define ADP5585_GPI_INT_STAT_A		0x13
#define ADP5585_GPI_INT_STAT_B		0x14
#define ADP5585_GPI_STATUS_A		0x15
#define ADP5585_GPI_STATUS_B		0x16
#define ADP5585_RPULL_CONFIG_A		0x17
#define ADP5585_RPULL_CONFIG_B		0x18
#define ADP5585_RPULL_CONFIG_C		0x19
#define ADP5585_RPULL_CONFIG_D		0x1A
#define ADP5585_GPI_INT_LEVEL_A		0x1B
#define ADP5585_GPI_INT_LEVEL_B		0x1C
#define ADP5585_GPI_EVENT_EN_A		0x1D
#define ADP5585_GPI_EVENT_EN_B		0x1E
#define ADP5585_GPI_INTERRUPT_EN_A	0x1F
#define ADP5585_GPI_INTERRUPT_EN_B	0x20
#define ADP5585_DEBOUNCE_DIS_A		0x21
#define ADP5585_DEBOUNCE_DIS_B		0x22
#define ADP5585_GPO_DATA_OUT_A		0x23
#define ADP5585_GPO_DATA_OUT_B		0x24
#define ADP5585_GPO_OUT_MODE_A		0x25
#define ADP5585_GPO_OUT_MODE_B		0x26
#define ADP5585_GPIO_DIRECTION_A	0x27
#define ADP5585_GPIO_DIRECTION_B	0x28
#define ADP5585_RESET1_EVENT_A		0x29
#define ADP5585_RESET1_EVENT_B		0x2A
#define ADP5585_RESET1_EVENT_C		0x2B
#define ADP5585_RESET2_EVENT_A		0x2C
#define ADP5585_RESET2_EVENT_B		0x2D
#define ADP5585_RESET_CFG		0x2E
#define ADP5585_PWM_OFFT_LOW		0x2F
#define ADP5585_PWM_OFFT_HIGH		0x30
#define ADP5585_PWM_ONT_LOW		0x31
#define ADP5585_PWM_ONT_HIGH		0x32
#define ADP5585_PWM_CFG			0x33
#define ADP5585_LOGIC_CFG		0x34
#define ADP5585_LOGIC_FF_CFG		0x35
#define ADP5585_LOGIC_INT_EVENT_EN	0x36
#define ADP5585_POLL_PTIME_CFG		0x37
#define ADP5585_PIN_CONFIG_A		0x38
#define ADP5585_PIN_CONFIG_B		0x39
#define ADP5585_PIN_CONFIG_C		0x3A
#define ADP5585_GENERAL_CFG		0x3B
#define ADP5585_INT_EN			0x3C

/* ID Register */
#define ADP5585_DEVICE_ID_MASK	0xF
#define ADP5585_MAN_ID_MASK	0xF
#define ADP5585_MAN_ID_SHIFT	4
#define ADP5585_MAN_ID		0x02

#define ADP5585_PWM_CFG_EN		0x1
#define ADP5585_PWM_CFG_MODE		0x2
#define ADP5585_PIN_CONFIG_R3_PWM	0x8
#define ADP5585_PIN_CONFIG_R3_MASK	0xC
#define ADP5585_GENERAL_CFG_OSC_EN	0x80

#define ADP5585_REG_MASK		0xFF

#define ADP5585_BANK(offs)		(offs >> 3)
#define ADP5585_BIT(offs)		(offs & 0b0111)

/* Number of pins supported by the device */
#define NUM_PINS (10U)

/* Max to select all pins supported on the device. */
#define ALL_PINS (uint16_t)((BIT_MASK(5U) << 8U) | (BIT_MASK(5U)))

struct gpio_pin_gaps {
	uint8_t start;
	uint8_t len;
};

enum adp5585_gpio_pin_direction {
	adp5585_pin_input = 0U,
	adp5585_pin_output,
};

enum adp5585_gpio_pin_drive_mode {
	adp5585_pin_drive_pp = 0U,
	adp5585_pin_drive_od,
};

enum adp5585_gpio_pull_config {
	adp5585_pull_up_300k = 0U,
	adp5585_pull_dn_300k,
	adp5585_pull_up_100k,
	adp5585_pull_disable,
};

/** Runtime driver data */
struct adp5585_gpio_drv_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	struct k_sem lock;
	uint16_t output;
	const struct device *dev;
};

/** Configuration data */
struct adp5585_gpio_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	struct i2c_dt_spec i2c;
	const struct gpio_dt_spec gpio_int;
	const struct gpio_pin_gaps *pin_gaps;
	const uint8_t gap_count;
};

static int gpio_adp5585_gpio_config(const struct device *dev, gpio_pin_t pin,
			       gpio_flags_t flags)
{
	const struct adp5585_gpio_config *cfg = dev->config;
	struct adp5585_gpio_drv_data *drv_data = dev->data;
	int rc = 0;
	uint8_t reg_value;
	bool set_output = false;

	/* ADP5585 has non-contiguous gpio pin layouts, account for this */
	for (int i = 0; i < cfg->gap_count; i++) {
		if ((pin >= cfg->pin_gaps[i].start) &&
		  (pin < (cfg->pin_gaps[i].start + cfg->pin_gaps[i].len))) {
			/* Pin is reserved unavailable */
			LOG_ERR("pin %d is invalid for this device", pin);
			return -ENOTSUP;
		}
	}

	uint8_t bank = ADP5585_BANK(pin);
	uint8_t bank_pin = ADP5585_BIT(pin);

	/* Can't do I2C bus operations from an ISR */
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	/* Single Ended lines (Open drain and open source) not supported */
	if ((flags & GPIO_SINGLE_ENDED) != 0) {
		return -ENOTSUP;
	}

	/* Simultaneous PU & PD mode not supported */
	if (((flags & GPIO_PULL_UP) != 0) && ((flags & GPIO_PULL_DOWN) != 0)) {
		return -ENOTSUP;
	}

	/* Simultaneous input & output mode not supported */
	if (((flags & GPIO_INPUT) != 0) && ((flags & GPIO_OUTPUT) != 0)) {
		return -ENOTSUP;
	}

	k_sem_take(&drv_data->lock, K_FOREVER);

	if ((flags & GPIO_SINGLE_ENDED) != 0)
		reg_value = adp5585_pin_drive_od << bank_pin;
	else
		reg_value = adp5585_pin_drive_pp << bank_pin;
	rc = i2c_reg_update_byte_dt(&cfg->i2c, ADP5585_GPO_OUT_MODE_A + bank, (0b1U << bank_pin), reg_value);
	if (rc != 0)
		goto out;

	{
		uint8_t regaddr = ADP5585_RPULL_CONFIG_A + (bank << 1);
		uint8_t shift = bank_pin << 1;
		if (bank_pin > 3U) {
			regaddr += 1U;
			shift = (bank_pin - 3U) << 1;
		}
		if ((flags & GPIO_PULL_UP) != 0)
			reg_value = adp5585_pull_up_300k;
		else if ((flags & GPIO_PULL_DOWN) != 0)
			reg_value = adp5585_pull_dn_300k;
		else
			reg_value = adp5585_pull_disable;

		rc = i2c_reg_update_byte_dt(&cfg->i2c, regaddr, 0b11U << shift, (reg_value << shift));
		if (rc != 0)
			goto out;
	}

	/* Ensure either Output or Input is specified */
	if ((flags & GPIO_OUTPUT) != 0) {
		if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
			drv_data->output &= ~BIT(pin);
			set_output = true;
		} else if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
			drv_data->output |= BIT(pin);
			set_output = true;
		}
		reg_value = adp5585_pin_output << bank_pin;
	} else if ((flags & GPIO_INPUT) != 0) {
		reg_value = adp5585_pin_input << bank_pin;
	} else {
		rc = -ENOTSUP;
		goto out;
	}

	if (set_output)
		rc = i2c_burst_write_dt(&cfg->i2c, ADP5585_GPO_DATA_OUT_A, &drv_data->output, 2U);

	if (rc == 0)
		rc = i2c_reg_update_byte_dt(&cfg->i2c, ADP5585_GPIO_DIRECTION_A + bank, (1U << bank_pin), reg_value);

out:
	k_sem_give(&drv_data->lock);
	if (rc != 0)
		LOG_ERR("pin configure error: %d", rc);
	return rc;
}

static int gpio_adp5585_gpio_port_read(const struct device *dev,
				 gpio_port_value_t *value)
{
	const struct adp5585_gpio_config *cfg = dev->config;
	struct adp5585_gpio_drv_data *drv_data = dev->data;
	uint16_t input_data;
	int rc = 0;

	/* Can't do I2C bus operations from an ISR */
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	k_sem_take(&drv_data->lock, K_FOREVER);

	/* Read Input Register */
	rc = i2c_burst_read_dt(&cfg->i2c, ADP5585_GPI_STATUS_A, &input_data, 2U);
	LOG_DBG("read %x got %d", input_data, rc);
	if (rc == 0) {
		*value = input_data;
	}

	k_sem_give(&drv_data->lock);
	return rc;
}

static int gpio_adp5585_gpio_port_write(const struct device *dev,
				   gpio_port_pins_t mask,
				   gpio_port_value_t value,
				   gpio_port_value_t toggle)
{
	const struct adp5585_gpio_config *cfg = dev->config;
	struct adp5585_gpio_drv_data *drv_data = dev->data;
	uint16_t orig_out;
	uint16_t out;
	int rc;

	/* Can't do I2C bus operations from an ISR */
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	k_sem_take(&drv_data->lock, K_FOREVER);

	orig_out = drv_data->output;
	out = ((orig_out & ~mask) | (value & mask)) ^ toggle;

	rc = i2c_burst_write_dt(&cfg->i2c, ADP5585_GPO_DATA_OUT_A, &out, 2U);

	if (rc == 0) {
		drv_data->output = out;
	}

	k_sem_give(&drv_data->lock);

	LOG_DBG("write %x msk %08x val %08x => %x: %d", orig_out, mask,
		value, out, rc);

	return rc;
}

static int gpio_adp5585_gpio_port_set_masked(const struct device *dev,
					gpio_port_pins_t mask,
					gpio_port_value_t value)
{
	return gpio_adp5585_gpio_port_write(dev, mask, value, 0);
}

static int gpio_adp5585_gpio_port_set_bits(const struct device *dev,
				      gpio_port_pins_t pins)
{
	return gpio_adp5585_gpio_port_write(dev, pins, pins, 0);
}

static int gpio_adp5585_gpio_port_clear_bits(const struct device *dev,
					gpio_port_pins_t pins)
{
	return gpio_adp5585_gpio_port_write(dev, pins, 0, 0);
}

static int gpio_adp5585_gpio_port_toggle_bits(const struct device *dev,
					 gpio_port_pins_t pins)
{
	return gpio_adp5585_gpio_port_write(dev, 0, 0, pins);
}

/**
 * @brief Initialization function of ADP5585_GPIO
 *
 * This sets initial input/ output configuration and output states.
 * The interrupt is configured if this is enabled.
 *
 * @param dev Device struct
 * @return 0 if successful, failed otherwise.
 */
static int gpio_adp5585_gpio_init(const struct device *dev)
{
	const struct adp5585_gpio_config *cfg = dev->config;
	struct adp5585_gpio_drv_data *drv_data = dev->data;
	int rc = 0;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("I2C bus device not found");
		goto out;
	}

	/** Set CONFIG to gpio by default */
	if (rc == 0)
		rc = i2c_reg_write_byte_dt(&cfg->i2c, ADP5585_PIN_CONFIG_A, 0x00U);
	if (rc == 0)
		rc = i2c_reg_write_byte_dt(&cfg->i2c, ADP5585_PIN_CONFIG_B, 0x00U);


	/** Set RPULL to high-z by default */
	if (rc == 0)
		rc = i2c_reg_write_byte_dt(&cfg->i2c, ADP5585_RPULL_CONFIG_A, 0xffU);
	if (rc == 0)
		rc = i2c_reg_write_byte_dt(&cfg->i2c, ADP5585_RPULL_CONFIG_B, 0x03U);
	if (rc == 0)
		rc = i2c_reg_write_byte_dt(&cfg->i2c, ADP5585_RPULL_CONFIG_C, 0xffU);
	if (rc == 0)
		rc = i2c_reg_write_byte_dt(&cfg->i2c, ADP5585_RPULL_CONFIG_D, 0x03U);

out:
	if (rc) {
		LOG_ERR("%s init failed: %d", dev->name, rc);
	} else {
		LOG_INF("%s init ok", dev->name);
	}
	return rc;
}

static const struct gpio_driver_api api_table = {
	.pin_configure = gpio_adp5585_gpio_config,
	.port_get_raw = gpio_adp5585_gpio_port_read,
	.port_set_masked_raw = gpio_adp5585_gpio_port_set_masked,
	.port_set_bits_raw = gpio_adp5585_gpio_port_set_bits,
	.port_clear_bits_raw = gpio_adp5585_gpio_port_clear_bits,
	.port_toggle_bits = gpio_adp5585_gpio_port_toggle_bits,
};

#define GPIO_ADP5585_GPIO_INIT(n)							\
	const uint8_t adp5585_gpio_pin_gaps_##n[] =				\
		DT_INST_PROP_OR(n, gpio_reserved_ranges, {}); \
		 								\
	static const struct adp5585_gpio_config adp5585_gpio_cfg_##n = {			\
		.i2c = I2C_DT_SPEC_INST_GET(n),					\
		.common = {							\
			.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n),	\
		},								\
		.gpio_int = GPIO_DT_SPEC_INST_GET_OR(n, nint_gpios, {0}),	\
		.pin_gaps = (const struct gpio_pin_gaps *)adp5585_gpio_pin_gaps_##n,	\
		.gap_count = (ARRAY_SIZE(adp5585_gpio_pin_gaps_##n) / 2) \
	};									\
										\
	static struct adp5585_gpio_drv_data adp5585_gpio_drvdata_##n = {			\
		.lock = Z_SEM_INITIALIZER(adp5585_gpio_drvdata_##n.lock, 1, 1),	\
		.output = ALL_PINS,					\
	};									\
	DEVICE_DT_INST_DEFINE(n,						\
		gpio_adp5585_gpio_init,						\
		NULL,								\
		&adp5585_gpio_drvdata_##n,						\
		&adp5585_gpio_cfg_##n,						\
		POST_KERNEL,							\
		CONFIG_GPIO_ADP5585_GPIO_INIT_PRIORITY,				\
		&api_table);

DT_INST_FOREACH_STATUS_OKAY(GPIO_ADP5585_GPIO_INIT)
