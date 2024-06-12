/*
 * Copyright (c) 2016 Freescale Semiconductor, Inc.
 * Copyright (c) 2018 Phytec Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_fxls8974

#include "fxls8974.h"
#include <zephyr/sys/util.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(FXLS8974, CONFIG_SENSOR_LOG_LEVEL);

/* Convert the range (16g, 8g, 4g, 2g) to the encoded FS register field value */
#define RANGE2FS(x) (__builtin_ctz(x) - 1)

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
#define DIR_READ(a)  ((a) & 0x7f)
#define DIR_WRITE(a) ((a) | BIT(7))
#define ADDR_7(a) ((a) & BIT(7))

int fxls8974_transceive(const struct device *dev,
				void *data, size_t length)
{
	const struct fxls8974_config *cfg = dev->config;
	const struct spi_buf buf = { .buf = data, .len = length };
	const struct spi_buf_set s = { .buffers = &buf, .count = 1 };

	return spi_transceive_dt(&cfg->bus_cfg.spi, &s, &s);
}

int fxls8974_read_spi(const struct device *dev,
		      uint8_t reg,
		      void *data,
		      size_t length)
{
	const struct fxls8974_config *cfg = dev->config;

	/* Reads must clock out a dummy byte after sending the address. */
	uint8_t reg_buf[3] = { DIR_READ(reg), ADDR_7(reg), 0 };
	const struct spi_buf buf[2] = {
		{ .buf = reg_buf, .len = 3 },
		{ .buf = data, .len = length }
	};
	const struct spi_buf_set tx = { .buffers = buf, .count = 1 };
	const struct spi_buf_set rx = { .buffers = buf, .count = 2 };

	return spi_transceive_dt(&cfg->bus_cfg.spi, &tx, &rx);
}

int fxls8974_byte_read_spi(const struct device *dev,
			   uint8_t reg,
			   uint8_t *byte)
{
	/* Reads must clock out a dummy byte after sending the address. */
	uint8_t data[] = { DIR_READ(reg), ADDR_7(reg), 0};
	int ret;

	ret = fxls8974_transceive(dev, data, sizeof(data));

	*byte = data[2];

	return ret;
}

int fxls8974_byte_write_spi(const struct device *dev,
			    uint8_t reg,
			    uint8_t byte)
{
	uint8_t data[] = { DIR_WRITE(reg), ADDR_7(reg), byte };

	return fxls8974_transceive(dev, data, sizeof(data));
}

int fxls8974_reg_field_update_spi(const struct device *dev,
				  uint8_t reg,
				  uint8_t mask,
				  uint8_t val)
{
	uint8_t old_val;

	if (fxls8974_byte_read_spi(dev, reg, &old_val) < 0) {
		return -EIO;
	}

	return fxls8974_byte_write_spi(dev, reg, (old_val & ~mask) | (val & mask));
}

static const struct fxls8974_io_ops fxls8974_spi_ops = {
	.read = fxls8974_read_spi,
	.byte_read = fxls8974_byte_read_spi,
	.byte_write = fxls8974_byte_write_spi,
	.reg_field_update = fxls8974_reg_field_update_spi,
};
#endif

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
int fxls8974_read_i2c(const struct device *dev,
		      uint8_t reg,
		      void *data,
		      size_t length)
{
	const struct fxls8974_config *config = dev->config;

	return i2c_burst_read_dt(&config->bus_cfg.i2c, reg, data, length);
}

int fxls8974_byte_read_i2c(const struct device *dev,
			   uint8_t reg,
			   uint8_t *byte)
{
	const struct fxls8974_config *config = dev->config;

	return i2c_reg_read_byte_dt(&config->bus_cfg.i2c, reg, byte);
}

int fxls8974_byte_write_i2c(const struct device *dev,
			    uint8_t reg,
			    uint8_t byte)
{
	const struct fxls8974_config *config = dev->config;

	return i2c_reg_write_byte_dt(&config->bus_cfg.i2c, reg, byte);
}

int fxls8974_reg_field_update_i2c(const struct device *dev,
				  uint8_t reg,
				  uint8_t mask,
				  uint8_t val)
{
	const struct fxls8974_config *config = dev->config;

	return i2c_reg_update_byte_dt(&config->bus_cfg.i2c, reg, mask, val);
}
static const struct fxls8974_io_ops fxls8974_i2c_ops = {
	.read = fxls8974_read_i2c,
	.byte_read = fxls8974_byte_read_i2c,
	.byte_write = fxls8974_byte_write_i2c,
	.reg_field_update = fxls8974_reg_field_update_i2c,
};
#endif

static int fxls8974_set_odr(const struct device *dev,
		const struct sensor_value *val)
{
	const struct fxls8974_config *config = dev->config;
	uint8_t dr;
	enum fxls8974_power power;

	if (val->val1 == 800 && val->val2 == 0) {
		dr = FXLS8974_CTRLREG1_DR_RATE_800;
	} else if (val->val1 == 400 && val->val2 == 0) {
		dr = FXLS8974_CTRLREG1_DR_RATE_400;
	} else if (val->val1 == 200 && val->val2 == 0) {
		dr = FXLS8974_CTRLREG1_DR_RATE_200;
	} else if (val->val1 == 100 && val->val2 == 0) {
		dr = FXLS8974_CTRLREG1_DR_RATE_100;
	} else if (val->val1 == 50 && val->val2 == 0) {
		dr = FXLS8974_CTRLREG1_DR_RATE_50;
	} else if (val->val1 == 12 && val->val2 == 500000) {
		dr = FXLS8974_CTRLREG1_DR_RATE_12_5;
	} else if (val->val1 == 6 && val->val2 == 250000) {
		dr = FXLS8974_CTRLREG1_DR_RATE_6_25;
	} else if (val->val1 == 1 && val->val2 == 562500) {
		dr = FXLS8974_CTRLREG1_DR_RATE_1_56;
	} else {
		return -EINVAL;
	}

	LOG_DBG("Set ODR to 0x%x", dr);

	/*
	 * Modify FXLS8974_REG_CTRLREG1 can only occur when the device
	 * is in standby mode. Get the current power mode to restore it later.
	 */
	if (fxls8974_get_power(dev, &power)) {
		LOG_ERR("Could not get power mode");
		return -EIO;
	}

	/* Set standby power mode */
	if (fxls8974_set_power(dev, FXLS8974_POWER_STANDBY)) {
		LOG_ERR("Could not set standby");
		return -EIO;
	}

	/* Change the attribute and restore power mode. */
	return config->ops->reg_field_update(dev, FXLS8974_REG_CTRLREG1,
				      FXLS8974_CTRLREG1_DR_MASK | FXLS8974_CTRLREG1_ACTIVE_MASK,
				      dr | power);
}

static int fxls8974_set_mt_ths(const struct device *dev,
			       const struct sensor_value *val)
{
#ifdef CONFIG_FXLS8974_MOTION
	const struct fxls8974_config *config = dev->config;
	uint64_t micro_ms2 = abs(val->val1 * 1000000LL + val->val2);
	uint64_t ths = micro_ms2 / FXLS8974_FF_MT_THS_SCALE;

	if (ths > FXLS8974_FF_MT_THS_MASK) {
		LOG_ERR("Threshold value is out of range");
		return -EINVAL;
	}

	LOG_DBG("Set FF_MT_THS to %d", (uint8_t)ths);

	return config->ops->reg_field_update(dev, FXLS8974_REG_FF_MT_THS,
				      FXLS8974_FF_MT_THS_MASK, (uint8_t)ths);
#else
	return -ENOTSUP;
#endif
}

static int fxls8974_attr_set(const struct device *dev,
			     enum sensor_channel chan,
			     enum sensor_attribute attr,
			     const struct sensor_value *val)
{
	if (chan != SENSOR_CHAN_ALL) {
		return -ENOTSUP;
	}

	if (attr == SENSOR_ATTR_SAMPLING_FREQUENCY) {
		return fxls8974_set_odr(dev, val);
	} else if (attr == SENSOR_ATTR_SLOPE_TH) {
		return fxls8974_set_mt_ths(dev, val);
	} else {
		return -ENOTSUP;
	}

	return 0;
}

static int fxls8974_sample_fetch(const struct device *dev,
				 enum sensor_channel chan)
{
	const struct fxls8974_config *config = dev->config;
	struct fxls8974_data *data = dev->data;
	uint8_t buffer[FXLS8974_MAX_NUM_BYTES];
	uint8_t num_bytes;
	int16_t *raw;
	int ret = 0;
	int i;

	if (chan != SENSOR_CHAN_ALL) {
		LOG_ERR("Unsupported sensor channel");
		return -ENOTSUP;
	}

	k_sem_take(&data->sem, K_FOREVER);

	/* Read all the channels in one I2C/SPI transaction. The number of bytes to
	 * read and the starting register address depend on the mode
	 * configuration (accel-only, mag-only, or hybrid).
	 */
	num_bytes = config->num_channels * FXLS8974_BYTES_PER_CHANNEL_NORMAL;

	__ASSERT(num_bytes <= sizeof(buffer), "Too many bytes to read");

	if (config->ops->read(dev, config->start_addr, buffer, num_bytes)) {
		LOG_ERR("Could not fetch sample");
		ret = -EIO;
		goto exit;
	}

	/* Parse the buffer into raw channel data (16-bit integers). To save
	 * RAM, store the data in raw format and wait to convert to the
	 * normalized sensor_value type until later.
	 */
	__ASSERT(config->start_channel + config->num_channels
			<= ARRAY_SIZE(data->raw),
			"Too many channels");

	raw = &data->raw[config->start_channel];

	for (i = 0; i < num_bytes; i += 2) {
		*raw++ = (buffer[i] << 8) | (buffer[i+1]);
	}

exit:
	k_sem_give(&data->sem);

	return ret;
}

static void fxls8974_accel_convert(struct sensor_value *val, int16_t raw,
				   uint8_t range)
{
	uint8_t frac_bits;
	int64_t micro_ms2;

	/* The range encoding is convenient to compute the number of fractional
	 * bits:
	 * - 2g mode (fs = 0) has 14 fractional bits
	 * - 4g mode (fs = 1) has 13 fractional bits
	 * - 8g mode (fs = 2) has 12 fractional bits
	 */
	frac_bits = 14 - RANGE2FS(range);

	/* Convert units to micro m/s^2. Intermediate results before the shift
	 * are 40 bits wide.
	 */
	micro_ms2 = (raw * SENSOR_G) >> frac_bits;

	/* The maximum possible value is 8g, which in units of micro m/s^2
	 * always fits into 32-bits. Cast down to int32_t so we can use a
	 * faster divide.
	 */
	val->val1 = (int32_t) micro_ms2 / 1000000;
	val->val2 = (int32_t) micro_ms2 % 1000000;
}

static int fxls8974_channel_get(const struct device *dev,
				enum sensor_channel chan,
				struct sensor_value *val)
{
	const struct fxls8974_config *config = dev->config;
	struct fxls8974_data *data = dev->data;
	int start_channel;
	int num_channels;
	int16_t *raw;
	int ret;
	int i;

	k_sem_take(&data->sem, K_FOREVER);

	/* Start with an error return code by default, then clear it if we find
	 * a supported sensor channel.
	 */
	ret = -ENOTSUP;

	/* If we're in an accelerometer-enabled mode (accel-only or hybrid),
	 * then convert raw accelerometer data to the normalized sensor_value
	 * type.
	 */
	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
		start_channel = FXLS8974_CHANNEL_ACCEL_X;
		num_channels = 1;
		break;
	case SENSOR_CHAN_ACCEL_Y:
		start_channel = FXLS8974_CHANNEL_ACCEL_Y;
		num_channels = 1;
		break;
	case SENSOR_CHAN_ACCEL_Z:
		start_channel = FXLS8974_CHANNEL_ACCEL_Z;
		num_channels = 1;
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		start_channel = FXLS8974_CHANNEL_ACCEL_X;
		num_channels = 3;
		break;
	default:
		start_channel = 0;
		num_channels = 0;
		break;
	}

	raw = &data->raw[start_channel];
	for (i = 0; i < num_channels; i++) {
		fxls8974_accel_convert(val++, *raw++, config->range);
	}

	if (num_channels > 0) {
		ret = 0;
	}

	if (ret != 0) {
		LOG_ERR("Unsupported sensor channel");
	}

	k_sem_give(&data->sem);

	return ret;
}

int fxls8974_get_power(const struct device *dev, enum fxls8974_power *power)
{
	const struct fxls8974_config *config = dev->config;
	uint8_t val;

	if (config->ops->byte_read(dev, FXLS8974_REG_CTRLREG1, &val)) {
		LOG_ERR("Could not get power setting");
		return -EIO;
	}
	val &= FXLS8974_M_CTRLREG1_MODE_MASK;
	*power = val;

	return 0;
}

int fxls8974_set_power(const struct device *dev, enum fxls8974_power power)
{
	const struct fxls8974_config *config = dev->config;

	return config->ops->reg_field_update(dev, FXLS8974_REG_CTRLREG1,
				      FXLS8974_CTRLREG1_ACTIVE_MASK, power);
}

static int fxls8974_init(const struct device *dev)
{
	const struct fxls8974_config *config = dev->config;
	struct fxls8974_data *data = dev->data;
	struct sensor_value odr = {.val1 = 6, .val2 = 250000};

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
	if (config->inst_on_bus == FXLS8974_BUS_I2C) {
		if (!device_is_ready(config->bus_cfg.i2c.bus)) {
			LOG_ERR("I2C bus device not ready");
			return -ENODEV;
		}
	}
#endif

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
	if (config->inst_on_bus == FXLS8974_BUS_SPI) {
		if (!device_is_ready(config->bus_cfg.spi.bus)) {
			LOG_ERR("SPI bus device not ready");
			return -ENODEV;
		}
	}
#endif

	if (config->reset_gpio.port) {
		/* Pulse RST pin high to perform a hardware reset of
		 * the sensor.
		 */

		if (!gpio_is_ready_dt(&config->reset_gpio)) {
			LOG_ERR("GPIO device not ready");
			return -ENODEV;
		}

		gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);

		gpio_pin_set_dt(&config->reset_gpio, 1);
		/* The datasheet does not mention how long to pulse
		 * the RST pin high in order to reset. Stay on the
		 * safe side and pulse for 1 millisecond.
		 */
		k_busy_wait(USEC_PER_MSEC);
		gpio_pin_set_dt(&config->reset_gpio, 0);
	} else {
		/* Software reset the sensor. Upon issuing a software
		 * reset command over the I2C interface, the sensor
		 * immediately resets and does not send any
		 * acknowledgment (ACK) of the written byte to the
		 * master. Therefore, do not check the return code of
		 * the I2C transaction.
		 */
		config->ops->byte_write(dev, FXLS8974_REG_CTRLREG2,
				      FXLS8974_CTRLREG2_RST_MASK);
	}

	/* The sensor requires us to wait 1 ms after a reset before
	 * attempting further communications.
	 */
	k_busy_wait(USEC_PER_MSEC);

	/*
	 * Read the WHOAMI register to make sure we are talking to FXLS8974 or
	 * compatible device and not some other type of device that happens to
	 * have the same I2C address.
	 */
	if (config->ops->byte_read(dev, FXLS8974_REG_WHOAMI,
				 &data->whoami)) {
		LOG_ERR("Could not get WHOAMI value");
		return -EIO;
	}

	if (data->whoami == WHOAMI_ID_FXLS8974){
		LOG_DBG("Device ID 0x%x", data->whoami);
	} else {
		LOG_ERR("Unknown Device ID 0x%x", data->whoami);
		return -EIO;
	}

	if (fxls8974_set_odr(dev, &odr)) {
		LOG_ERR("Could not set default data rate");
		return -EIO;
	}

	if (config->ops->reg_field_update(dev, FXLS8974_REG_CTRLREG2,
				   FXLS8974_CTRLREG2_MODS_MASK,
				   config->power_mode)) {
		LOG_ERR("Could not set power scheme");
		return -EIO;
	}


	/* Set hybrid autoincrement so we can read accel and mag channels in
	 * one I2C/SPI transaction.
	 */
	if (config->ops->reg_field_update(dev, FXLS8974_REG_M_CTRLREG2,
				   FXLS8974_M_CTRLREG2_AUTOINC_MASK,
				   FXLS8974_M_CTRLREG2_AUTOINC_MASK)) {
		LOG_ERR("Could not set hybrid autoincrement");
		return -EIO;
	}

	/* Set the full-scale range */
	if (config->ops->reg_field_update(dev, FXLS8974_REG_XYZ_DATA_CFG,
				   FXLS8974_XYZ_DATA_CFG_FS_MASK,
				   RANGE2FS(config->range))) {
		LOG_ERR("Could not set range");
		return -EIO;
	}

	k_sem_init(&data->sem, 0, K_SEM_MAX_LIMIT);

#if CONFIG_FXLS8974_TRIGGER
	if (fxls8974_trigger_init(dev)) {
		LOG_ERR("Could not initialize interrupts");
		return -EIO;
	}
#endif

	/* Set active */
	if (fxls8974_set_power(dev, FXLS8974_POWER_ACTIVE)) {
		LOG_ERR("Could not set active");
		return -EIO;
	}
	k_sem_give(&data->sem);

	LOG_DBG("Init complete");

	return 0;
}

static const struct sensor_driver_api fxls8974_driver_api = {
	.sample_fetch = fxls8974_sample_fetch,
	.channel_get = fxls8974_channel_get,
	.attr_set = fxls8974_attr_set,
#if CONFIG_FXLS8974_TRIGGER
	.trigger_set = fxls8974_trigger_set,
#endif
};

#define FXLS8974_MODE_PROPS_ACCEL					\
	.start_addr = FXLS8974_REG_OUTXMSB,				\
	.start_channel = FXLS8974_CHANNEL_ACCEL_X,			\
	.num_channels = FXLS8974_NUM_ACCEL_CHANNELS,

#define FXLS8974_RESET_PROPS(n)						\
	.reset_gpio = GPIO_DT_SPEC_INST_GET(n, reset_gpios),

#define FXLS8974_RESET(n)						\
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, reset_gpios),		\
		    (FXLS8974_RESET_PROPS(n)),				\
		    ())

#define FXLS8974_INTM_PROPS(n, m)					\
	.int_gpio = GPIO_DT_SPEC_INST_GET(n, int##m##_gpios),

#define FXLS8974_INT_PROPS(n)						\
	COND_CODE_1(CONFIG_FXLS8974_DRDY_INT1,				\
		    (FXLS8974_INTM_PROPS(n, 1)),			\
		    (FXLS8974_INTM_PROPS(n, 2)))

#define FXLS8974_INT(n)							\
	COND_CODE_1(CONFIG_FXLS8974_TRIGGER,				\
		    (FXLS8974_INT_PROPS(n)),				\
		    ())

#define FXLS8974_PULSE_PROPS(n)						\
	.pulse_cfg = DT_INST_PROP(n, pulse_cfg),			\
	.pulse_ths[0] = DT_INST_PROP(n, pulse_thsx),			\
	.pulse_ths[1] = DT_INST_PROP(n, pulse_thsy),			\
	.pulse_ths[2] = DT_INST_PROP(n, pulse_thsz),			\
	.pulse_tmlt = DT_INST_PROP(n, pulse_tmlt),			\
	.pulse_ltcy = DT_INST_PROP(n, pulse_ltcy),			\
	.pulse_wind = DT_INST_PROP(n, pulse_wind),

#define FXLS8974_PULSE(n)						\
	COND_CODE_1(CONFIG_FXLS8974_PULSE,				\
		    (FXLS8974_PULSE_PROPS(n)),				\
		    ())

#define FXLS8974_CONFIG_I2C(n)						\
		.bus_cfg = { .i2c = I2C_DT_SPEC_INST_GET(n) },		\
		.ops = &fxls8974_i2c_ops,				\
		.power_mode = DT_INST_PROP(n, power_mode),		\
		.range = DT_INST_PROP(n, range),			\
		.inst_on_bus = FXLS8974_BUS_I2C,

#define FXLS8974_CONFIG_SPI(n)						\
		.bus_cfg = { .spi = SPI_DT_SPEC_INST_GET(n,		\
			SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0) },	\
		.ops = &fxls8974_spi_ops,				\
		.power_mode =  DT_INST_PROP(n, power_mode),		\
		.range = DT_INST_PROP(n, range),			\
		.inst_on_bus = FXLS8974_BUS_SPI,			\

#define FXLS8974_SPI_OPERATION (SPI_WORD_SET(8) |			\
				SPI_OP_MODE_MASTER)			\

#define FXLS8974_INIT(n)						\
	static const struct fxls8974_config fxls8974_config_##n = {	\
	COND_CODE_1(DT_INST_ON_BUS(n, spi),				\
		(FXLS8974_CONFIG_SPI(n)),				\
		(FXLS8974_CONFIG_I2C(n)))				\
		FXLS8974_RESET(n)					\
		FXLS8974_INT(n)						\
		FXLS8974_PULSE(n)					\
	};								\
									\
	static struct fxls8974_data fxls8974_data_##n;			\
									\
	SENSOR_DEVICE_DT_INST_DEFINE(n,					\
				     fxls8974_init,			\
				     NULL,				\
				     &fxls8974_data_##n,		\
				     &fxls8974_config_##n,		\
				     POST_KERNEL,			\
				     CONFIG_SENSOR_INIT_PRIORITY,	\
				     &fxls8974_driver_api);

DT_INST_FOREACH_STATUS_OKAY(FXLS8974_INIT)
