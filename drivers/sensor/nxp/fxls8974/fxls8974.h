/*
 * Copyright (c) 2016 Freescale Semiconductor, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
#include <zephyr/drivers/spi.h>
#endif

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
#include <zephyr/drivers/i2c.h>
#endif
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define FXLS8974_BUS_I2C			(1<<0)
#define FXLS8974_BUS_SPI			(1<<1)
#define FXLS8974_REG_STATUS			0x00
#define FXLS8974_REG_OUTXMSB		0x05
#define FXLS8974_REG_OUTYMSB		0x07
#define FXLS8974_REG_OUTZMSB		0x09
#define FXLS8974_REG_INT_SOURCE		0x0c
#define FXLS8974_REG_WHOAMI			0x13
#define FXLS8974_REG_XYZ_DATA_CFG	0x0e
#define FXLS8974_REG_CTRLREG1		0x15
#define FXLS8974_REG_CTRLREG2		0x16
#define FXLS8974_REG_CTRLREG3		0x17
#define FXLS8974_REG_CTRLREG4		0x18
#define FXLS8974_REG_CTRLREG5		0x19

#define FXLS8974_WAKE_IDLE_LSB          0x1a /*[7:0]*/
#define FXLS8974_WAKE_IDLE_MSB          0x1b /*[11:8]*/
#define FXLS8974_SLEEP_IDLE_LSB         0x1c /*[7:0]*/
#define FXLS8974_SLEEP_IDLE_MSB         0x1d /*[11:8]*/
#define FXLS8974_ASLP_COUNT_LSB         0x1e /*[7:0]*/
#define FXLS8974_ASLP_COUNT_MSB         0x1f /*[11:8]*/

/* Devices that are compatible with this driver: */
#define WHOAMI_ID_FXLS8974			0x86

#define FXLS8974_DRDY_MASK			(1 << 0)
#define FXLS8974_VECM_MASK			(1 << 1)
#define FXLS8974_MOTION_MASK			(1 << 2)

#define FXLS8974_XYZ_DATA_CFG_FS_MASK		0x03

#define FXLS8974_PULSE_SRC_DPE			(1 << 3)

#define FXLS8974_CTRLREG1_ACTIVE_MASK	0x01
#define FXLS8974_CTRLREG1_RST_MASK      0x80
#define FXLS8974_CTRLREG1_FSR_MASK      0x06
#define FXLS8974_CTRLREG1_FSR_2G	0x00
#define FXLS8974_CTRLREG1_FSR_4G	0x01
#define FXLS8974_CTRLREG1_FSR_8G	0x02
#define FXLS8974_CTRLREG1_FSR_16G	0x03

#define FXLS8974_CTRLREG3_ODR_MASK 0xf0

#define FXLS8974_CTRLREG3_ODR_RATE_3200   (0 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_1600   (1 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_800    (2 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_400    (3 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_200    (4 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_100    (5 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_50     (6 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_25     (7 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_12_5   (8 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_6_25   (9 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_3_125  (10 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_1_563  (11 << 4)
#define FXLS8974_CTRLREG3_ODR_RATE_0_781  (12 << 4)

#define FXLS8974_FF_MT_CFG_ELE			BIT(7)
#define FXLS8974_FF_MT_CFG_OAE			BIT(6)
#define FXLS8974_FF_MT_CFG_ZEFE			BIT(5)
#define FXLS8974_FF_MT_CFG_YEFE			BIT(4)
#define FXLS8974_FF_MT_CFG_XEFE			BIT(3)
#define FXLS8974_FF_MT_THS_MASK			0x7f
#define FXLS8974_FF_MT_THS_SCALE		(SENSOR_G * 63000LL / 1000000LL)

#define FXLS8974_M_CTRLREG1_MODE_MASK		0x03

#define FXLS8974_M_CTRLREG2_AUTOINC_MASK	(1 << 5)

#define FXLS8974_NUM_ACCEL_CHANNELS		3
#define FXLS8974_MAX_NUM_CHANNELS		6

#define FXLS8974_BYTES_PER_CHANNEL_NORMAL	2
#define FXLS8974_BYTES_PER_CHANNEL_FAST		1

#define FXLS8974_MAX_NUM_BYTES		(FXLS8974_BYTES_PER_CHANNEL_NORMAL * \
					 FXLS8974_MAX_NUM_CHANNELS)

enum fxls8974_power {
	FXLS8974_POWER_STANDBY		= 0,
	FXLS8974_POWER_ACTIVE,
};

enum fxls8974_channel {
	FXLS8974_CHANNEL_ACCEL_X	= 0,
	FXLS8974_CHANNEL_ACCEL_Y,
	FXLS8974_CHANNEL_ACCEL_Z,
};

/* FXLS8974 specific triggers */
enum fxos_trigger_type {
	FXLS8974_TRIG_M_VECM,
};

struct fxls8974_io_ops {
	int (*read)(const struct device *dev,
		    uint8_t reg,
		    void *data,
		    size_t length);
	int (*byte_read)(const struct device *dev,
			 uint8_t reg,
			 uint8_t *byte);
	int (*byte_write)(const struct device *dev,
			  uint8_t reg,
			  uint8_t byte);
	int (*reg_field_update)(const struct device *dev,
				uint8_t reg,
				uint8_t mask,
				uint8_t val);
};

union fxls8974_bus_cfg {
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
	struct spi_dt_spec spi;
#endif

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
	struct i2c_dt_spec i2c;
#endif
};

struct fxls8974_config {
	const union fxls8974_bus_cfg bus_cfg;
	const struct fxls8974_io_ops *ops;
#ifdef CONFIG_FXLS8974_TRIGGER
	struct gpio_dt_spec int_gpio;
#endif
	struct gpio_dt_spec reset_gpio;
	uint8_t range;
	uint8_t start_addr;
	uint8_t start_channel;
	uint8_t num_channels;
#ifdef CONFIG_FXLS8974_PULSE
	uint8_t pulse_cfg;
	uint8_t pulse_ths[3];
	uint8_t pulse_tmlt;
	uint8_t pulse_ltcy;
	uint8_t pulse_wind;
#endif
	uint8_t inst_on_bus;
};

struct fxls8974_data {
	struct k_sem sem;
#ifdef CONFIG_FXLS8974_TRIGGER
	const struct device *dev;
	struct gpio_callback gpio_cb;
	sensor_trigger_handler_t drdy_handler;
	const struct sensor_trigger *drdy_trig;
#endif
#ifdef CONFIG_FXLS8974_PULSE
	sensor_trigger_handler_t tap_handler;
	const struct sensor_trigger *tap_trig;
	sensor_trigger_handler_t double_tap_handler;
	const struct sensor_trigger *double_tap_trig;
#endif
#ifdef CONFIG_FXLS8974_MOTION
	sensor_trigger_handler_t motion_handler;
	const struct sensor_trigger *motion_trig;
#endif
#ifdef CONFIG_FXLS8974_TRIGGER_OWN_THREAD
	K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_FXLS8974_THREAD_STACK_SIZE);
	struct k_thread thread;
	struct k_sem trig_sem;
#endif
#ifdef CONFIG_FXLS8974_TRIGGER_GLOBAL_THREAD
	struct k_work work;
#endif
	int16_t raw[FXLS8974_MAX_NUM_CHANNELS];
	uint8_t whoami;
};

int fxls8974_get_power(const struct device *dev, enum fxls8974_power *power);
int fxls8974_set_power(const struct device *dev, enum fxls8974_power power);

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(spi)
int fxls8974_byte_write_spi(const struct device *dev,
			    uint8_t reg,
			    uint8_t byte);

int fxls8974_byte_read_spi(const struct device *dev,
			   uint8_t reg,
			   uint8_t *byte);

int fxls8974_reg_field_update_spi(const struct device *dev,
				  uint8_t reg,
				  uint8_t mask,
				  uint8_t val);

int fxls8974_read_spi(const struct device *dev,
		      uint8_t reg,
		      void *data,
		      size_t length);
#endif
#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)
int fxls8974_byte_write_i2c(const struct device *dev,
			    uint8_t reg,
			    uint8_t byte);

int fxls8974_byte_read_i2c(const struct device *dev,
			   uint8_t reg,
			   uint8_t *byte);

int fxls8974_reg_field_update_i2c(const struct device *dev,
				  uint8_t reg,
				  uint8_t mask,
				  uint8_t val);

int fxls8974_read_i2c(const struct device *dev,
		      uint8_t reg,
		      void *data,
		      size_t length);
#endif
#if CONFIG_FXLS8974_TRIGGER
int fxls8974_trigger_init(const struct device *dev);
int fxls8974_trigger_set(const struct device *dev,
			 const struct sensor_trigger *trig,
			 sensor_trigger_handler_t handler);
#endif
