/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * NXP NAFE13388 analog front-end driver.
 *
 * The driver contains the SPI frame layer, register access, the DRDY interrupt,
 * the device initialisation (reset, chip ready, part number check, SYS_CONFIG0),
 * the standard ADC API (channel setup, synchronous and asynchronous reads) and
 * the extension API (complete channel configuration, reading modes, SYNC pin
 * trigger, continuous reading with a callback, raw register access, device
 * commands).
 *
 * The conversion engine has one user at a time (enum nafe13388_owner, under
 * the device lock): a read of the standard API, a reading of the extension API
 * or a continuous reading. The continuous reading and the standard reads run
 * on the same acquisition thread, so a callback runs in thread context.
 *
 * Sample layout of a read: one sample per channel of the sequence in channel
 * index order, int32_t (sign extended 24 bit) for a resolution of 24 and
 * int16_t for a resolution of 16.
 */

#define DT_DRV_COMPAT nxp_nafe13388

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/dt-bindings/adc/nafe13388.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/nafe13388.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define ADC_CONTEXT_USES_KERNEL_TIMER 1
#define ADC_CONTEXT_ENABLE_ON_COMPLETE
#include "adc_context.h"

LOG_MODULE_REGISTER(adc_nafe13388, CONFIG_ADC_LOG_LEVEL);

/* Number of logical channels */
#define NAFE13388_NUM_CHANNELS 16
#define NAFE13388_ALL_CHANNELS GENMASK(NAFE13388_NUM_CHANNELS - 1, 0)

/* The burst frame holds the command word and 3 bytes per channel at most */
#define NAFE13388_BURST_FRAME_MAX (2 + 3 * NAFE13388_NUM_CHANNELS)

/* Reference span of the 24 bit result: +-5 V at a PGA gain of 1 */
#define NAFE13388_REF_INTERNAL_MV 5000

/* SPI mode 1 (CPHA), MSB first, 8 bit words, the chip select comes from DT */
#define NAFE13388_SPI_OPERATION                                                                    \
	(SPI_OP_MODE_CONTROLLER | SPI_MODE_CPHA | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

/*
 * SPI command word (data sheet 7.8.2): bit 15 = device address (SPI_ADDR pin),
 * bit 14 = read, bits 13..1 = register address or command code.
 */
#define NAFE13388_CMD_DEV_ADDR BIT(15)
#define NAFE13388_CMD_READ     BIT(14)
#define NAFE13388_CMD_CODE_POS 1

/* Instruction commands (data sheet 7.8.3.1.3) */
#define NAFE13388_CMD_CH0         0x0000U /* + n selects logical channel n for CH_CONFIG0..3 */
#define NAFE13388_CMD_ABORT       0x0010U
#define NAFE13388_CMD_END         0x0011U
#define NAFE13388_CMD_CLEAR_ALARM 0x0012U
#define NAFE13388_CMD_CLEAR_DATA  0x0013U
#define NAFE13388_CMD_RESET       0x0014U
#define NAFE13388_CMD_CLEAR_REG   0x0015U
#define NAFE13388_CMD_RELOAD      0x0016U
#define NAFE13388_CMD_SS          0x2000U
#define NAFE13388_CMD_SC          0x2001U
#define NAFE13388_CMD_MM          0x2002U
#define NAFE13388_CMD_MC          0x2003U
#define NAFE13388_CMD_MS          0x2004U
#define NAFE13388_CMD_BURST_DATA  0x2005U

/* CH_CONFIG0 fields (data sheet Table 29) */
#define NAFE13388_CH_CONFIG0_HV_AIP_MASK   GENMASK(15, 12)
#define NAFE13388_CH_CONFIG0_HV_AIN_MASK   GENMASK(11, 8)
#define NAFE13388_CH_CONFIG0_CH_GAIN_MASK  GENMASK(7, 5)
#define NAFE13388_CH_CONFIG0_HV_SEL        BIT(4)
#define NAFE13388_CH_CONFIG0_LVSIG_IN_MASK GENMASK(3, 1)
#define NAFE13388_CH_CONFIG0_TCC_OFF       BIT(0)

/* CH_CONFIG1 fields */
#define NAFE13388_CH_CONFIG1_CAL_MASK  GENMASK(15, 12)
#define NAFE13388_CH_CONFIG1_THRS_MASK GENMASK(11, 8)
#define NAFE13388_CH_CONFIG1_RATE_MASK GENMASK(7, 3)
#define NAFE13388_CH_CONFIG1_SINC_MASK GENMASK(2, 0)

/* CH_CONFIG2 fields */
#define NAFE13388_CH_CONFIG2_DELAY_MASK      GENMASK(15, 10)
#define NAFE13388_CH_CONFIG2_NORMAL_SETTLING BIT(9)
#define NAFE13388_CH_CONFIG2_FILTER_RESET    BIT(8)
#define NAFE13388_CH_CONFIG2_CH_CHOP         BIT(7)

/* CH_CONFIG3 fields */
#define NAFE13388_CH_CONFIG3_VIEX_VI       BIT(15)
#define NAFE13388_CH_CONFIG3_VIEX_POL      BIT(14)
#define NAFE13388_CH_CONFIG3_VIEX_MAG_MASK GENMASK(13, 10)
#define NAFE13388_CH_CONFIG3_VEXC_EN       BIT(9)
#define NAFE13388_CH_CONFIG3_ODC           BIT(8)
#define NAFE13388_CH_CONFIG3_VIEX_CHOP     BIT(6)
#define NAFE13388_CH_CONFIG3_AIP_EN_MASK   GENMASK(5, 3)
#define NAFE13388_CH_CONFIG3_AIN_EN_MASK   GENMASK(2, 0)

/* Highest HV multiplexer input code (VEXC), and highest excitation pin code (AI4) */
#define NAFE13388_HV_INPUT_MAX 8
#define NAFE13388_VIEX_PIN_MAX 4

/*
 * Table 6: the low voltage signal LVSIG_IN is only routed with HV_AIP = HV_AIN
 * = 9 + LVSIG_IN (data sheet 7.3.4: LVSIG_IN 3 gives CH_CONFIG0 = 0xCC07 at gain
 * code 0 and TCC_OFF = 1). With 0/0 the codes 2..5 all read the same value.
 */
#define NAFE13388_LV_HV_INPUT_BASE 9

/* Register ranges that the raw register access accepts */
#define NAFE13388_REG16_USER_FIRST 0x20U
#define NAFE13388_REG16_USER_LAST  0x37U
/* The register map of the data sheet has no register at 0x28 (0x27 is followed by 0x29) */
#define NAFE13388_REG16_HOLE       0x28U
#define NAFE13388_REG16_ID_FIRST   0x7CU
#define NAFE13388_REG16_ID_LAST    0x7FU
#define NAFE13388_REG24_DATA_FIRST 0x40U
#define NAFE13388_REG24_DATA_LAST  0x6FU
#define NAFE13388_REG24_COEF_FIRST 0x80U
#define NAFE13388_REG24_COEF_LAST  0xAFU

/* 16-bit registers */
#define NAFE13388_REG_CH_CONFIG0  0x20U
#define NAFE13388_REG_CH_CONFIG1  0x21U
#define NAFE13388_REG_CH_CONFIG2  0x22U
#define NAFE13388_REG_CH_CONFIG4  0x24U /* MCH_EN */
#define NAFE13388_REG_SYS_CONFIG0 0x30U
#define NAFE13388_REG_SYS_STATUS0 0x31U
#define NAFE13388_REG_PN2         0x7CU
#define NAFE13388_REG_PN0         0x7EU /* bits 7:0 = REVISION_ID */

/* 24-bit registers */
#define NAFE13388_REG_CH_DATA0 0x40U /* + n */
#define NAFE13388_REG_SERIAL1  0xAEU
#define NAFE13388_REG_SERIAL0  0xAFU

/* SYS_CONFIG0 fields */
#define NAFE13388_SYS_CONFIG0_DRDY_PWDT          BIT(15)
#define NAFE13388_SYS_CONFIG0_ADC_DATA_OUT_16BIT BIT(14)
#define NAFE13388_SYS_CONFIG0_REF_SEL_MASK       GENMASK(11, 10)
#define NAFE13388_SYS_CONFIG0_CK_SRC_SEL_MASK    GENMASK(9, 8)
#define NAFE13388_SYS_CONFIG0_ADC_SYNC           BIT(5)
#define NAFE13388_SYS_CONFIG0_DRDY_PIN_EDGE      BIT(4)

/* SYS_STATUS0 fields */
#define NAFE13388_SYS_STATUS0_SC_ACTIVE BIT(15)
#define NAFE13388_SYS_STATUS0_MC_ACTIVE BIT(14)
#define NAFE13388_SYS_STATUS0_ACTIVE                                                               \
	(NAFE13388_SYS_STATUS0_SC_ACTIVE | NAFE13388_SYS_STATUS0_MC_ACTIVE)
#define NAFE13388_SYS_STATUS0_CHIP_READY BIT(13)

/*
 * PN2 holds the most significant part of the part number. The data sheet gives
 * 0x1338 as the example for NAFE13388B40BS.
 */
#define NAFE13388_PN2_EXPECTED 0x1338U

/*
 * Width of the RSTB pulse. The data sheet gives no minimum, so a conservative value is used.
 */
#define NAFE13388_RESET_PULSE_US 100

/*
 * Channel properties of the devicetree that the standard ADC API cannot
 * express, already encoded as the register bits they set. They are merged
 * into the registers that channel_setup builds.
 */
struct nafe13388_channel_extra {
	uint16_t config0;
	uint16_t config1;
	uint16_t config2;
	uint16_t config3;
	/* The channel has a node in the devicetree */
	bool present;
	/* Select the low voltage signals instead of the HV multiplexer */
	bool lv;
};

struct nafe13388_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec drdy;
	struct gpio_dt_spec reset;
	struct gpio_dt_spec sync;
	struct gpio_dt_spec intb;
	uint8_t dev_addr;
	uint8_t clock_source;
	uint8_t reference_select;
	bool drdy_wide_pulse;
	/* Channels of the devicetree, indexed by channel number */
	struct adc_channel_cfg dt_cfg[NAFE13388_NUM_CHANNELS];
	struct nafe13388_channel_extra extra[NAFE13388_NUM_CHANNELS];
};

/* Who uses the conversion engine, protected by the device lock */
enum nafe13388_owner {
	NAFE13388_OWNER_IDLE,
	/* A read of the standard ADC API is running */
	NAFE13388_OWNER_ADC_API,
	/* A reading started with the extension API is running */
	NAFE13388_OWNER_EXT,
	/* A continuous reading with a callback is running */
	NAFE13388_OWNER_EXT_CONT,
};

struct nafe13388_data {
	struct adc_context ctx;
	const struct device *dev;
	struct k_mutex lock;
	struct k_sem drdy_sem;
	struct gpio_callback drdy_cb;
	/* Last value written to SYS_CONFIG0 */
	uint16_t sys_config0;
	/* Milliseconds the chip needed to become ready after the reset */
	uint16_t chip_ready_ms;
	/* Last value written to CH_CONFIG4 (MCH_EN) */
	uint16_t mch_en;
	/* Channels that were set up since the reset */
	uint16_t configured;
	/* Current user of the conversion engine */
	enum nafe13388_owner owner;
	/* Reading started with the extension API */
	uint8_t ext_n;
	enum nafe13388_mode ext_mode;
	bool ext_16bit;
	bool ext_sync;
	/* Continuous reading: callback, stop request and the end of the loop */
	nafe13388_seq_cb cont_cb;
	void *cont_user_data;
	bool cont_stop;
	struct k_sem cont_done;
	int32_t cont_raw[NAFE13388_NUM_CHANNELS];
	/* CH_CONFIG0..3 written for every channel */
	uint16_t channel_regs[NAFE13388_NUM_CHANNELS][4];
	/* State of the sequence that is being read */
	void *buffer;
	void *repeat_buffer;
	uint32_t channels;
	/* Burst frames, only used with the lock held */
	uint8_t burst_tx[NAFE13388_BURST_FRAME_MAX];
	uint8_t burst_rx[NAFE13388_BURST_FRAME_MAX];
	struct k_thread thread;
	struct k_sem acq_sem;

	K_KERNEL_STACK_MEMBER(stack, CONFIG_ADC_NAFE13388_ACQUISITION_THREAD_STACK_SIZE);
};

/*
 * SPI frame layer. These are the only functions that call the SPI API. One
 * call is one transaction, so one chip select frame, and a command is never
 * split over two frames.
 */

static uint16_t nafe13388_cmd_word(const struct nafe13388_config *config, uint16_t code, bool read)
{
	uint16_t word = code << NAFE13388_CMD_CODE_POS;

	if (config->dev_addr != 0U) {
		word |= NAFE13388_CMD_DEV_ADDR;
	}
	if (read) {
		word |= NAFE13388_CMD_READ;
	}

	return word;
}

/* Instruction command without data: 2 bytes */
static int nafe13388_frame_cmd(const struct device *dev, uint16_t cmd)
{
	const struct nafe13388_config *config = dev->config;
	uint16_t word = nafe13388_cmd_word(config, cmd, false);
	uint8_t tx[2];
	const struct spi_buf buf = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf_set set = {.buffers = &buf, .count = 1};

	sys_put_be16(word, tx);

	return spi_write_dt(&config->spi, &set);
}

/* Write of a 16-bit register: command word and 2 data bytes */
static int nafe13388_frame_reg_write16(const struct device *dev, uint8_t reg, uint16_t val)
{
	const struct nafe13388_config *config = dev->config;
	uint16_t word = nafe13388_cmd_word(config, reg, false);
	uint8_t tx[4];
	const struct spi_buf buf = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf_set set = {.buffers = &buf, .count = 1};

	sys_put_be16(word, &tx[0]);
	sys_put_be16(val, &tx[2]);

	return spi_write_dt(&config->spi, &set);
}

/* Write of a 24-bit register: command word and 3 data bytes */
static int nafe13388_frame_reg_write24(const struct device *dev, uint8_t reg, uint32_t val)
{
	const struct nafe13388_config *config = dev->config;
	uint16_t word = nafe13388_cmd_word(config, reg, false);
	uint8_t tx[5];
	const struct spi_buf buf = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf_set set = {.buffers = &buf, .count = 1};

	sys_put_be16(word, &tx[0]);
	sys_put_be24(val, &tx[2]);

	return spi_write_dt(&config->spi, &set);
}

/* Read of a 16-bit register: the value is clocked out behind the command word */
static int nafe13388_frame_reg_read16(const struct device *dev, uint8_t reg, uint16_t *val)
{
	const struct nafe13388_config *config = dev->config;
	uint16_t word = nafe13388_cmd_word(config, reg, true);
	uint8_t tx[4] = {0};
	uint8_t rx[4];
	const struct spi_buf tx_buf = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf rx_buf = {.buf = rx, .len = sizeof(rx)};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
	int ret;

	sys_put_be16(word, tx);

	ret = spi_transceive_dt(&config->spi, &tx_set, &rx_set);
	if (ret == 0) {
		*val = sys_get_be16(&rx[2]);
	}

	return ret;
}

/* Read of a 24-bit register, returned as the raw unsigned 24-bit value */
static int nafe13388_frame_reg_read24(const struct device *dev, uint8_t reg, uint32_t *val)
{
	const struct nafe13388_config *config = dev->config;
	uint16_t word = nafe13388_cmd_word(config, reg, true);
	uint8_t tx[5] = {0};
	uint8_t rx[5];
	const struct spi_buf tx_buf = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf rx_buf = {.buf = rx, .len = sizeof(rx)};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
	int ret;

	sys_put_be16(word, tx);

	ret = spi_transceive_dt(&config->spi, &tx_set, &rx_set);
	if (ret == 0) {
		*val = sys_get_be24(&rx[2]);
	}

	return ret;
}

/*
 * Read of a data register with a 4 byte frame (16 data bits), for the 16 bit output mode.
 * The driver assumes that the frame returns the upper 16 bit of the 24 bit code (the
 * data sheet does not say it), so the value is returned as a signed 16 bit number.
 */
static int nafe13388_frame_reg_read24_16(const struct device *dev, uint8_t reg, int32_t *val)
{
	uint16_t word16;
	int ret;

	ret = nafe13388_frame_reg_read16(dev, reg, &word16);
	if (ret == 0) {
		*val = (int16_t)word16;
	}

	return ret;
}

/*
 * CMD_BURST_DATA: one frame that returns the data of all channels enabled in
 * CH_CONFIG4 (MCH_EN), in channel index order. The caller holds the lock.
 * wire_16bit is the width of the words on the bus (SYS_CONFIG0), the samples
 * are stored as sign extended int32_t or as int16_t (only with 16 bit words).
 */
static int nafe13388_frame_burst(const struct device *dev, void *out, unsigned int n_channels,
				 bool wire_16bit, bool out_int32)
{
	const struct nafe13388_config *config = dev->config;
	struct nafe13388_data *data = dev->data;
	const size_t word_len = wire_16bit ? 2 : 3;
	const size_t len = 2 + n_channels * word_len;
	const struct spi_buf tx_buf = {.buf = data->burst_tx, .len = len};
	const struct spi_buf rx_buf = {.buf = data->burst_rx, .len = len};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
	int ret;

	if (n_channels == 0U || n_channels > NAFE13388_NUM_CHANNELS) {
		return -EINVAL;
	}

	sys_put_be16(nafe13388_cmd_word(config, NAFE13388_CMD_BURST_DATA, false), data->burst_tx);

	ret = spi_transceive_dt(&config->spi, &tx_set, &rx_set);
	if (ret != 0) {
		return ret;
	}

	for (unsigned int i = 0; i < n_channels; i++) {
		const uint8_t *src = &data->burst_rx[2 + i * word_len];
		int32_t value;

		if (wire_16bit) {
			value = (int16_t)sys_get_be16(src);
		} else {
			value = sign_extend(sys_get_be24(src), 23);
		}

		if (out_int32) {
			((int32_t *)out)[i] = value;
		} else {
			((int16_t *)out)[i] = (int16_t)value;
		}
	}

	return 0;
}

/*
 * Register and command access with the device lock held. Sequences that need
 * several frames in a row (for example CMD_CHn followed by the CH_CONFIG
 * writes) take the lock once and use the frame functions directly.
 */

static int nafe13388_reg_write16(const struct device *dev, uint8_t reg, uint16_t val)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = nafe13388_frame_reg_write16(dev, reg, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int nafe13388_reg_read16(const struct device *dev, uint8_t reg, uint16_t *val)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = nafe13388_frame_reg_read16(dev, reg, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int nafe13388_reg_read24(const struct device *dev, uint8_t reg, uint32_t *val)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = nafe13388_frame_reg_read24(dev, reg, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

/* Instruction command with the lock held for the duration of the frame */
static int nafe13388_cmd(const struct device *dev, uint16_t cmd)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = nafe13388_frame_cmd(dev, cmd);
	k_mutex_unlock(&data->lock);

	return ret;
}

/* The DRDY pulse is only about 434 ns long, so it is caught as an edge event */
static void nafe13388_drdy_callback(const struct device *port, struct gpio_callback *cb,
				    uint32_t pins)
{
	struct nafe13388_data *data = CONTAINER_OF(cb, struct nafe13388_data, drdy_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	k_sem_give(&data->drdy_sem);
}

static int nafe13388_init_drdy(const struct device *dev)
{
	const struct nafe13388_config *config = dev->config;
	struct nafe13388_data *data = dev->data;
	int ret;

	ret = gpio_pin_configure_dt(&config->drdy, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("%s: failed to configure DRDY pin (%d)", dev->name, ret);
		return ret;
	}

	gpio_init_callback(&data->drdy_cb, nafe13388_drdy_callback, BIT(config->drdy.pin));

	ret = gpio_add_callback(config->drdy.port, &data->drdy_cb);
	if (ret != 0) {
		LOG_ERR("%s: failed to add DRDY callback (%d)", dev->name, ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->drdy, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("%s: failed to enable DRDY interrupt (%d)", dev->name, ret);
		return ret;
	}

	return 0;
}

/*
 * Wait for SYS_STATUS0.CHIP_READY. After a reset the device reloads its NVM
 * and detects the clock source. The data sheet gives up to 50 ms for the
 * clock switching with an external clock or a crystal and no time for the
 * internal oscillator; the default timeout is a margin chosen by the driver.
 * The first read of SYS_STATUS0 also clears the INTB indication of the power-up.
 *
 * A word counts as "ready" only if CHIP_READY is set and both conversion
 * active bits (SINGLE_CH_ACTIVE, MULTI_CH_ACTIVE) are clear. The caller has
 * no conversion running, and a word like 0xffff (all bits set) cannot be
 * a status of an idle chip. That such a word comes from a floating or not yet
 * driven MISO line is an assumption, the data sheet does not describe what the
 * device shows while it is not ready. The words that were skipped are counted
 * and logged at debug level.
 */
static int nafe13388_wait_ready(const struct device *dev)
{
	struct nafe13388_data *data = dev->data;
	const int64_t start = k_uptime_get();
	const int64_t deadline = start + CONFIG_ADC_NAFE13388_CHIP_READY_TIMEOUT_MS;
	uint16_t status = 0;
	uint16_t first_bogus = 0;
	uint32_t bogus = 0;
	uint32_t ms;
	int ret;

	/* The poll sleeps 1 ms, which can be a whole tick, so the time is taken from the clock */
	do {
		k_msleep(1);
		ms = (uint32_t)(k_uptime_get() - start);

		ret = nafe13388_reg_read16(dev, NAFE13388_REG_SYS_STATUS0, &status);
		if (ret != 0) {
			LOG_ERR("%s: failed to read SYS_STATUS0 (%d)", dev->name, ret);
			return ret;
		}

		if ((status & NAFE13388_SYS_STATUS0_CHIP_READY) != 0U) {
			if ((status & NAFE13388_SYS_STATUS0_ACTIVE) == 0U) {
				data->chip_ready_ms = ms;
				if (bogus != 0U) {
					/*
					 * Such a word (for example 0xffff) can happen right after a
					 * reset, so this is not a warning.
					 */
					LOG_DBG("%s: %u implausible SYS_STATUS0 words before ready "
						"(first 0x%04x), ready after %u ms",
						dev->name, bogus, first_bogus, ms);
				} else {
					LOG_DBG("%s: chip ready after %u ms, SYS_STATUS0 0x%04x",
						dev->name, ms, status);
				}
				return 0;
			}

			if (bogus == 0U) {
				first_bogus = status;
			}
			bogus++;
		}
	} while (k_uptime_get() < deadline);

	LOG_ERR("%s: chip not ready %d ms after reset (last SYS_STATUS0 0x%04x, %u implausible "
		"words, first 0x%04x)",
		dev->name, CONFIG_ADC_NAFE13388_CHIP_READY_TIMEOUT_MS, status, bogus, first_bogus);

	return -ETIMEDOUT;
}

/*
 * Conversion period of one channel in microseconds (rounded up) for the data rate
 * codes 0..28 with Normal Settling, from Table 7 (system clock 4.608 MHz).
 */
static const uint32_t nafe13388_conv_us[NAFE13388_DATA_RATE_CODE_MAX + 1] = {
	4,     6,     7,     11,    14,    21,    28,    42,     56,     84,
	112,   167,   223,   334,   445,   889,   1778,  2500,   3334,   5000,
	10000, 16667, 20000, 33334, 40000, 50000, 66667, 100000, 133334,
};

/* CH_DELAY codes 0..31 in system clock cycles (CH_CONFIG2 description), 32: 19200, 33..63: 23040 */
static const uint16_t nafe13388_delay_clk[32] = {
	0,  2,  4,  6,   8,   10,  12,  14,  16,  18,  20,  28,  38,   40,   42,   56,
	64, 76, 90, 128, 154, 178, 204, 224, 256, 358, 512, 716, 1024, 1664, 3276, 7680,
};

static uint32_t nafe13388_delay_us(uint8_t code)
{
	uint32_t clk;

	if (code < ARRAY_SIZE(nafe13388_delay_clk)) {
		clk = nafe13388_delay_clk[code];
	} else {
		clk = (code == 32U) ? 19200U : 23040U;
	}

	/* 1 / 4.608 MHz = 125 / 576 us */
	return DIV_ROUND_UP(clk * 125U, 576U);
}

/*
 * How long one sequence of the enabled channels (MCH_EN) can take: the sum over the
 * channels of the conversion period, the channel delay and a factor for the settling
 * mode and the chopping, plus CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS as a margin. The
 * period comes from the data rate code (Table 7). Single-cycle settling is four times
 * slower than Normal Settling, and up to five times with the second stage SINC filter
 * (Table 7, code 28: 7.5 sps against 1.5 sps), so five is used for every filter
 * (an upper bound, chosen by the driver). CH_CHOP and VIEX_CHOP take two conversions.
 * The caller holds the lock.
 */
static uint32_t nafe13388_timeout_ms(const struct nafe13388_data *data)
{
	uint32_t total_us = 0;

	for (unsigned int ch = 0; ch < NAFE13388_NUM_CHANNELS; ch++) {
		const uint16_t config1 = data->channel_regs[ch][1];
		const uint16_t config2 = data->channel_regs[ch][2];
		const uint16_t config3 = data->channel_regs[ch][3];
		uint32_t conv_us;

		if ((data->mch_en & BIT(ch)) == 0U) {
			continue;
		}

		conv_us = nafe13388_conv_us[MIN(FIELD_GET(NAFE13388_CH_CONFIG1_RATE_MASK, config1),
						NAFE13388_DATA_RATE_CODE_MAX)];
		if ((config2 & NAFE13388_CH_CONFIG2_NORMAL_SETTLING) == 0U) {
			conv_us *= 5U;
		}
		if ((config2 & NAFE13388_CH_CONFIG2_CH_CHOP) != 0U ||
		    (config3 & NAFE13388_CH_CONFIG3_VIEX_CHOP) != 0U) {
			conv_us *= 2U;
		}

		total_us += conv_us +
			    nafe13388_delay_us(FIELD_GET(NAFE13388_CH_CONFIG2_DELAY_MASK, config2));
	}

	return CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS + DIV_ROUND_UP(total_us, 1000U);
}

/*
 * After CMD_END the device finishes the running conversion first (data sheet
 * 7.5.2.2), so SINGLE_CH_ACTIVE or MULTI_CH_ACTIVE of SYS_STATUS0 can still be
 * set when the command returns (bit 4 of SYS_STATUS0 is ADC_CONV_CH, not an active
 * bit). Wait for both bits to
 * clear, at most nafe13388_timeout_ms(). On a timeout the device is aborted. The caller
 * holds the lock.
 */
static int nafe13388_wait_idle(const struct device *dev)
{
	const uint32_t timeout_ms = nafe13388_timeout_ms(dev->data);
	const int64_t deadline = k_uptime_get() + timeout_ms;
	uint16_t status;
	int ret;

	while (true) {
		ret = nafe13388_frame_reg_read16(dev, NAFE13388_REG_SYS_STATUS0, &status);
		if (ret != 0) {
			return ret;
		}

		if ((status & NAFE13388_SYS_STATUS0_ACTIVE) == 0U) {
			return 0;
		}

		if (k_uptime_get() >= deadline) {
			break;
		}

		k_msleep(1);
	}

	LOG_ERR("%s: still converting %u ms after END, aborting", dev->name, timeout_ms);
	(void)nafe13388_frame_cmd(dev, NAFE13388_CMD_ABORT);

	return -ETIMEDOUT;
}

/* Hardware reset with the RSTB pin, then wait for CHIP_READY */
static int nafe13388_reset(const struct device *dev)
{
	const struct nafe13388_config *config = dev->config;
	int ret;

	/* RSTB is active low, the devicetree flags make "active" mean "in reset" */
	ret = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		LOG_ERR("%s: failed to configure reset pin (%d)", dev->name, ret);
		return ret;
	}

	k_busy_wait(NAFE13388_RESET_PULSE_US);

	ret = gpio_pin_set_dt(&config->reset, 0);
	if (ret != 0) {
		LOG_ERR("%s: failed to release reset (%d)", dev->name, ret);
		return ret;
	}

	return nafe13388_wait_ready(dev);
}

/*
 * Check that the expected part answers on the bus. PN2 (0x7C) is the most
 * significant part of the part number and reads 0x1338 for the NAFE13388. A
 * missing or stuck MISO line reads 0x0000 or 0xFFFF, and the other members of
 * the NAFE family, which this driver does not support, report another value.
 */
static int nafe13388_check_part(const struct device *dev)
{
	uint16_t pn2;
	uint16_t pn0;
	uint32_t serial1 = 0;
	uint32_t serial0 = 0;
	int ret;

	ret = nafe13388_reg_read16(dev, NAFE13388_REG_PN2, &pn2);
	if (ret != 0) {
		LOG_ERR("%s: failed to read PN2 (%d)", dev->name, ret);
		return ret;
	}

	if (pn2 != NAFE13388_PN2_EXPECTED) {
		LOG_ERR("%s: unexpected part number 0x%04x (expected 0x%04x)", dev->name, pn2,
			NAFE13388_PN2_EXPECTED);
		return -ENODEV;
	}

	/* Identification for the log only, a failure here is not fatal */
	if (nafe13388_reg_read16(dev, NAFE13388_REG_PN0, &pn0) != 0 ||
	    nafe13388_reg_read24(dev, NAFE13388_REG_SERIAL1, &serial1) != 0 ||
	    nafe13388_reg_read24(dev, NAFE13388_REG_SERIAL0, &serial0) != 0) {
		LOG_WRN("%s: failed to read the device identification", dev->name);
		return 0;
	}

	LOG_DBG("%s: revision %u, serial 0x%06x%06x", dev->name, pn0 & 0xFFU, serial1, serial0);

	return 0;
}

/*
 * SYS_CONFIG0 is written as a whole. The bits taken from the devicetree are
 * the clock source, the reference selection and the DRDY pulse width. DRDY is
 * signalled once per multichannel sequence, conversions start with the SPI
 * command, 24 bit data without status byte, no CRC.
 */
static int nafe13388_init_sys_config(const struct device *dev)
{
	const struct nafe13388_config *config = dev->config;
	struct nafe13388_data *data = dev->data;
	uint16_t val = NAFE13388_SYS_CONFIG0_DRDY_PIN_EDGE;
	int ret;

	val |= FIELD_PREP(NAFE13388_SYS_CONFIG0_CK_SRC_SEL_MASK, config->clock_source);
	val |= FIELD_PREP(NAFE13388_SYS_CONFIG0_REF_SEL_MASK, config->reference_select);
	if (config->drdy_wide_pulse) {
		val |= NAFE13388_SYS_CONFIG0_DRDY_PWDT;
	}

	ret = nafe13388_reg_write16(dev, NAFE13388_REG_SYS_CONFIG0, val);
	if (ret != 0) {
		LOG_ERR("%s: failed to write SYS_CONFIG0 (%d)", dev->name, ret);
		return ret;
	}

	data->sys_config0 = val;

	return 0;
}

/*
 * Translate a standard ADC channel configuration, merged with the devicetree
 * properties of the channel, into the values of CH_CONFIG0..3.
 */
static int nafe13388_build_channel(const struct nafe13388_config *config,
				   const struct adc_channel_cfg *cfg, uint16_t regs[4])
{
	const struct nafe13388_channel_extra *extra = &config->extra[cfg->channel_id];
	uint16_t config0 = extra->config0;
	uint16_t config1 = extra->config1;
	uint16_t config3 = extra->config3;
	uint8_t gain;
	uint8_t rate = 0;
	uint8_t pin_p = 0;
	uint8_t pin_n = 0;

	switch (cfg->gain) {
	case ADC_GAIN_1_5:
		gain = 0;
		break;
	case ADC_GAIN_2_5:
		gain = 1;
		break;
	case ADC_GAIN_4_5:
		gain = 2;
		break;
	case ADC_GAIN_1:
		gain = 3;
		break;
	case ADC_GAIN_2:
		gain = 4;
		break;
	case ADC_GAIN_4:
		gain = 5;
		break;
	case ADC_GAIN_8:
		gain = 6;
		break;
	case ADC_GAIN_16:
		gain = 7;
		break;
	default:
		LOG_ERR("unsupported gain %d", cfg->gain);
		return -EINVAL;
	}

	if (cfg->reference != ADC_REF_INTERNAL) {
		LOG_ERR("unsupported reference %d", cfg->reference);
		return -EINVAL;
	}

	/* The acquisition time carries the ADC data rate code in ticks */
	if (cfg->acquisition_time != ADC_ACQ_TIME_DEFAULT) {
		if (ADC_ACQ_TIME_UNIT(cfg->acquisition_time) != ADC_ACQ_TIME_TICKS ||
		    ADC_ACQ_TIME_VALUE(cfg->acquisition_time) > NAFE13388_DATA_RATE_CODE_MAX) {
			LOG_ERR("unsupported acquisition time 0x%04x", cfg->acquisition_time);
			return -EINVAL;
		}
		rate = ADC_ACQ_TIME_VALUE(cfg->acquisition_time);
	}

	config0 |= FIELD_PREP(NAFE13388_CH_CONFIG0_CH_GAIN_MASK, gain);

	if (extra->lv) {
		const uint8_t pair = NAFE13388_LV_HV_INPUT_BASE +
				     FIELD_GET(NAFE13388_CH_CONFIG0_LVSIG_IN_MASK, config0);

		config0 |= FIELD_PREP(NAFE13388_CH_CONFIG0_HV_AIP_MASK, pair);
		config0 |= FIELD_PREP(NAFE13388_CH_CONFIG0_HV_AIN_MASK, pair);
	} else {
		/*
		 * The code of the device is the same signed 24 bit code for every
		 * input type (data sheet Table 5, Table 20), so adc_raw_to_millivolts()
		 * is only right with one bit less of resolution, which the helper
		 * subtracts for differential channels only. A single ended channel
		 * is therefore not accepted: use a differential channel with the
		 * negative input NAFE13388_HV_GND or NAFE13388_HV_AICOM.
		 */
		if (!cfg->differential) {
			LOG_ERR("single ended is not supported, use differential vs GND/AICOM");
			return -ENOTSUP;
		}

		if (cfg->input_positive > NAFE13388_HV_INPUT_MAX ||
		    cfg->input_negative > NAFE13388_HV_INPUT_MAX) {
			LOG_ERR("unsupported inputs %u/%u", cfg->input_positive,
				cfg->input_negative);
			return -EINVAL;
		}

		config0 |= NAFE13388_CH_CONFIG0_HV_SEL;
		config0 |= FIELD_PREP(NAFE13388_CH_CONFIG0_HV_AIP_MASK, cfg->input_positive);
		config0 |= FIELD_PREP(NAFE13388_CH_CONFIG0_HV_AIN_MASK, cfg->input_negative);
	}

	if (cfg->current_source_pin_set) {
		pin_p = cfg->current_source_pin[0];
		pin_n = cfg->current_source_pin[1];
		if (pin_p > NAFE13388_VIEX_PIN_MAX || pin_n > NAFE13388_VIEX_PIN_MAX) {
			LOG_ERR("unsupported excitation pins %u/%u", pin_p, pin_n);
			return -EINVAL;
		}
	}

	config1 |= FIELD_PREP(NAFE13388_CH_CONFIG1_RATE_MASK, rate);
	config3 |= FIELD_PREP(NAFE13388_CH_CONFIG3_AIP_EN_MASK, pin_p);
	config3 |= FIELD_PREP(NAFE13388_CH_CONFIG3_AIN_EN_MASK, pin_n);

	regs[0] = config0;
	regs[1] = config1;
	regs[2] = extra->config2;
	regs[3] = config3;

	return 0;
}

/*
 * Write CH_CONFIG0..3 of a channel and keep a copy of them. CMD_CHn selects
 * the channel that the register writes address.
 */
static int nafe13388_program_channel(const struct device *dev, uint8_t channel,
				     const uint16_t regs[4])
{
	struct nafe13388_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = nafe13388_frame_cmd(dev, NAFE13388_CMD_CH0 + channel);
	for (unsigned int i = 0; ret == 0 && i < 4U; i++) {
		ret = nafe13388_frame_reg_write16(dev, NAFE13388_REG_CH_CONFIG0 + i, regs[i]);
	}

	if (ret == 0) {
		memcpy(data->channel_regs[channel], regs, 4 * sizeof(regs[0]));
		data->configured |= BIT(channel);
	}

	k_mutex_unlock(&data->lock);

	if (ret != 0) {
		LOG_ERR("%s: failed to set up channel %u (%d)", dev->name, channel, ret);
	}

	return ret;
}

/*
 * Not while a reading owns the device: the data sheet (7.4.2.2) says a change of the
 * channel that is converting only takes effect with the next conversion command, so
 * a CH_CONFIG1 write during a running reading can be lost while the
 * driver had already updated its copy. The init code (init_channels() at init and after
 * RESET or CLEAR_REG) runs with the owner IDLE. The lock is recursive, it is held so
 * that the check and the write are one step.
 */
static int nafe13388_channel_setup(const struct device *dev,
				   const struct adc_channel_cfg *channel_cfg)
{
	const struct nafe13388_config *config = dev->config;
	struct nafe13388_data *data = dev->data;
	uint16_t regs[4];
	int ret;

	if (channel_cfg->channel_id >= NAFE13388_NUM_CHANNELS) {
		LOG_ERR("unsupported channel id %u", channel_cfg->channel_id);
		return -EINVAL;
	}

	ret = nafe13388_build_channel(config, channel_cfg, regs);
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->owner != NAFE13388_OWNER_IDLE) {
		ret = -EBUSY;
	} else {
		ret = nafe13388_program_channel(dev, channel_cfg->channel_id, regs);
	}
	k_mutex_unlock(&data->lock);

	return ret;
}

/* Extension API: channel setup with all register fields */
int nafe13388_channel_setup_ext(const struct device *dev, uint8_t channel,
				const struct nafe13388_channel_cfg *cfg)
{
	struct nafe13388_data *data = dev->data;
	uint16_t regs[4];
	int ret;

	if (cfg == NULL || channel >= NAFE13388_NUM_CHANNELS || cfg->gain_code > 7U ||
	    cfg->lv_signal > NAFE13388_LV_VHSS || cfg->cal_pair > 15U || cfg->thrs_pair > 15U ||
	    cfg->rate > NAFE13388_DATA_RATE_CODE_MAX || cfg->sinc > NAFE13388_SINC4 ||
	    cfg->delay > NAFE13388_CHANNEL_DELAY_CODE_MAX || cfg->viex_mag > 15U ||
	    cfg->viex_aip > NAFE13388_VIEX_PIN_MAX || cfg->viex_ain > NAFE13388_VIEX_PIN_MAX ||
	    (!cfg->lv &&
	     (cfg->hv_aip > NAFE13388_HV_INPUT_MAX || cfg->hv_ain > NAFE13388_HV_INPUT_MAX))) {
		return -EINVAL;
	}

	regs[0] = FIELD_PREP(NAFE13388_CH_CONFIG0_CH_GAIN_MASK, cfg->gain_code);
	if (cfg->lv) {
		const uint8_t pair = NAFE13388_LV_HV_INPUT_BASE + cfg->lv_signal;

		regs[0] |= FIELD_PREP(NAFE13388_CH_CONFIG0_LVSIG_IN_MASK, cfg->lv_signal);
		regs[0] |= FIELD_PREP(NAFE13388_CH_CONFIG0_HV_AIP_MASK, pair);
		regs[0] |= FIELD_PREP(NAFE13388_CH_CONFIG0_HV_AIN_MASK, pair);
	} else {
		regs[0] |= NAFE13388_CH_CONFIG0_HV_SEL;
		regs[0] |= FIELD_PREP(NAFE13388_CH_CONFIG0_HV_AIP_MASK, cfg->hv_aip);
		regs[0] |= FIELD_PREP(NAFE13388_CH_CONFIG0_HV_AIN_MASK, cfg->hv_ain);
	}
	regs[0] |= cfg->tcc_off ? NAFE13388_CH_CONFIG0_TCC_OFF : 0;

	regs[1] = FIELD_PREP(NAFE13388_CH_CONFIG1_CAL_MASK, cfg->cal_pair) |
		  FIELD_PREP(NAFE13388_CH_CONFIG1_THRS_MASK, cfg->thrs_pair) |
		  FIELD_PREP(NAFE13388_CH_CONFIG1_RATE_MASK, cfg->rate) |
		  FIELD_PREP(NAFE13388_CH_CONFIG1_SINC_MASK, cfg->sinc);

	regs[2] = FIELD_PREP(NAFE13388_CH_CONFIG2_DELAY_MASK, cfg->delay);
	regs[2] |= cfg->normal_settling ? NAFE13388_CH_CONFIG2_NORMAL_SETTLING : 0;
	regs[2] |= cfg->filter_reset ? NAFE13388_CH_CONFIG2_FILTER_RESET : 0;
	regs[2] |= cfg->chop ? NAFE13388_CH_CONFIG2_CH_CHOP : 0;

	regs[3] = FIELD_PREP(NAFE13388_CH_CONFIG3_VIEX_MAG_MASK, cfg->viex_mag) |
		  FIELD_PREP(NAFE13388_CH_CONFIG3_AIP_EN_MASK, cfg->viex_aip) |
		  FIELD_PREP(NAFE13388_CH_CONFIG3_AIN_EN_MASK, cfg->viex_ain);
	regs[3] |= cfg->viex_current ? NAFE13388_CH_CONFIG3_VIEX_VI : 0;
	regs[3] |= cfg->viex_negative ? NAFE13388_CH_CONFIG3_VIEX_POL : 0;
	regs[3] |= cfg->vexc_en ? NAFE13388_CH_CONFIG3_VEXC_EN : 0;
	regs[3] |= cfg->odc ? NAFE13388_CH_CONFIG3_ODC : 0;
	regs[3] |= cfg->viex_chop ? NAFE13388_CH_CONFIG3_VIEX_CHOP : 0;

	/*
	 * Not while a reading owns the device: the data sheet (7.4.2.2) says a change of
	 * the channel that is converting only takes effect with the next conversion
	 * command, so a cfg_rate from the callback of a running MC
	 * reading can be lost while the driver had already updated its copy.
	 * The lock is recursive, it is held so that the check and the write are one step.
	 */
	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->owner != NAFE13388_OWNER_IDLE) {
		ret = -EBUSY;
	} else {
		ret = nafe13388_program_channel(dev, channel, regs);
	}
	k_mutex_unlock(&data->lock);

	return ret;
}

/*
 * Extension API: change only the data rate and the delay of a set up channel.
 * Refused with -EBUSY while a reading owns the device, see nafe13388_channel_setup_ext().
 */
int nafe13388_cfg_rate(const struct device *dev, uint8_t channel, uint8_t rate, uint8_t delay)
{
	struct nafe13388_data *data = dev->data;
	uint16_t config1;
	uint16_t config2;
	int ret = 0;

	if (channel >= NAFE13388_NUM_CHANNELS || rate > NAFE13388_DATA_RATE_CODE_MAX ||
	    delay > NAFE13388_CHANNEL_DELAY_CODE_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->owner != NAFE13388_OWNER_IDLE) {
		ret = -EBUSY;
		goto out;
	}

	if ((data->configured & BIT(channel)) == 0U) {
		ret = -EINVAL;
		goto out;
	}

	config1 = data->channel_regs[channel][1];
	config1 &= ~NAFE13388_CH_CONFIG1_RATE_MASK;
	config1 |= FIELD_PREP(NAFE13388_CH_CONFIG1_RATE_MASK, rate);
	config2 = data->channel_regs[channel][2];
	config2 &= ~NAFE13388_CH_CONFIG2_DELAY_MASK;
	config2 |= FIELD_PREP(NAFE13388_CH_CONFIG2_DELAY_MASK, delay);

	if (config1 == data->channel_regs[channel][1] &&
	    config2 == data->channel_regs[channel][2]) {
		goto out;
	}

	ret = nafe13388_frame_cmd(dev, NAFE13388_CMD_CH0 + channel);
	if (ret == 0 && config1 != data->channel_regs[channel][1]) {
		ret = nafe13388_frame_reg_write16(dev, NAFE13388_REG_CH_CONFIG1, config1);
		if (ret == 0) {
			data->channel_regs[channel][1] = config1;
		}
	}
	if (ret == 0 && config2 != data->channel_regs[channel][2]) {
		ret = nafe13388_frame_reg_write16(dev, NAFE13388_REG_CH_CONFIG2, config2);
		if (ret == 0) {
			data->channel_regs[channel][2] = config2;
		}
	}

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

static bool nafe13388_reg16_valid(uint8_t reg)
{
	return (reg >= NAFE13388_REG16_USER_FIRST && reg <= NAFE13388_REG16_USER_LAST &&
		reg != NAFE13388_REG16_HOLE) ||
	       (reg >= NAFE13388_REG16_ID_FIRST && reg <= NAFE13388_REG16_ID_LAST);
}

static bool nafe13388_reg24_valid(uint8_t reg)
{
	return (reg >= NAFE13388_REG24_DATA_FIRST && reg <= NAFE13388_REG24_DATA_LAST) ||
	       (reg >= NAFE13388_REG24_COEF_FIRST && reg <= NAFE13388_REG24_COEF_LAST);
}

/* The registers of which the driver keeps a copy */
static bool nafe13388_reg16_shadowed(uint8_t reg)
{
	return (reg >= NAFE13388_REG_CH_CONFIG0 && reg <= NAFE13388_REG_CH_CONFIG4) ||
	       reg == NAFE13388_REG_SYS_CONFIG0;
}

int nafe13388_read_reg16(const struct device *dev, uint8_t reg, uint16_t *val)
{
	if (val == NULL || !nafe13388_reg16_valid(reg)) {
		return -EINVAL;
	}

	return nafe13388_reg_read16(dev, reg, val);
}

int nafe13388_write_reg16(const struct device *dev, uint8_t reg, uint16_t val)
{
	if (!nafe13388_reg16_valid(reg) || nafe13388_reg16_shadowed(reg)) {
		return -EINVAL;
	}

	return nafe13388_reg_write16(dev, reg, val);
}

int nafe13388_read_reg24(const struct device *dev, uint8_t reg, uint32_t *val)
{
	if (val == NULL || !nafe13388_reg24_valid(reg)) {
		return -EINVAL;
	}

	return nafe13388_reg_read24(dev, reg, val);
}

int nafe13388_write_reg24(const struct device *dev, uint8_t reg, uint32_t val)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	if (!nafe13388_reg24_valid(reg)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = nafe13388_frame_reg_write24(dev, reg, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

static size_t nafe13388_sample_size(uint8_t resolution)
{
	return (resolution == 16U) ? sizeof(int16_t) : sizeof(int32_t);
}

/* The next two write a register only when the value differs, the caller holds the lock */
static int nafe13388_update_sys_config0(const struct device *dev, uint16_t val)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	if (val == data->sys_config0) {
		return 0;
	}

	ret = nafe13388_frame_reg_write16(dev, NAFE13388_REG_SYS_CONFIG0, val);
	if (ret == 0) {
		data->sys_config0 = val;
	}

	return ret;
}

static int nafe13388_update_mch_en(const struct device *dev, uint16_t val)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	if (val == data->mch_en) {
		return 0;
	}

	ret = nafe13388_frame_reg_write16(dev, NAFE13388_REG_CH_CONFIG4, val);
	if (ret == 0) {
		data->mch_en = val;
	}

	return ret;
}

/* Forget a DRDY event, before the command that starts a conversion */
static void nafe13388_drdy_flush(struct nafe13388_data *data)
{
	k_sem_reset(&data->drdy_sem);
}

/* Wait for the DRDY event, 0 or -ETIMEDOUT. The lock is not needed. */
static int nafe13388_drdy_take(struct nafe13388_data *data, k_timeout_t timeout)
{
	return (k_sem_take(&data->drdy_sem, timeout) == 0) ? 0 : -ETIMEDOUT;
}

/*
 * Read one sampling of the sequence: configure, start the conversion, wait for
 * DRDY and fetch the results of all channels with one burst frame. The device lock
 * is not held while waiting for DRDY: the owner (ADC_API) already keeps every other
 * user of the conversion engine out, so a call that needs the lock only (a raw
 * register access, NAFE13388_CLEAR_ALARM) is not delayed by the conversion.
 */
static int nafe13388_sample(const struct device *dev)
{
	struct nafe13388_data *data = dev->data;
	const uint8_t resolution = data->ctx.sequence.resolution;
	const bool out_16bit = (resolution == 16U);
	const unsigned int n_channels = POPCOUNT(data->channels);
	uint32_t timeout_ms;
	uint16_t sys_config0;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	/*
	 * The extension API may have left another SYS_CONFIG0, so the bits this
	 * path depends on are set every time: the conversion starts with the SPI
	 * command, one DRDY per sequence and the width of the results. The
	 * registers are only written when a value differs.
	 */
	sys_config0 = data->sys_config0 &
		      ~(NAFE13388_SYS_CONFIG0_ADC_SYNC | NAFE13388_SYS_CONFIG0_ADC_DATA_OUT_16BIT);
	sys_config0 |= NAFE13388_SYS_CONFIG0_DRDY_PIN_EDGE;
	if (out_16bit) {
		sys_config0 |= NAFE13388_SYS_CONFIG0_ADC_DATA_OUT_16BIT;
	}

	ret = nafe13388_update_sys_config0(dev, sys_config0);
	if (ret != 0) {
		goto out;
	}

	ret = nafe13388_update_mch_en(dev, data->channels);
	if (ret != 0) {
		goto out;
	}

	if (n_channels == 1U) {
		ret = nafe13388_frame_cmd(dev,
					  NAFE13388_CMD_CH0 + find_lsb_set(data->channels) - 1);
		if (ret != 0) {
			goto out;
		}
	}

	/* Drop an old DRDY before the command that starts the conversion */
	nafe13388_drdy_flush(data);

	ret = nafe13388_frame_cmd(dev, (n_channels == 1U) ? NAFE13388_CMD_SS : NAFE13388_CMD_MM);
	if (ret != 0) {
		goto out;
	}

	timeout_ms = nafe13388_timeout_ms(data);

	k_mutex_unlock(&data->lock);
	ret = nafe13388_drdy_take(data, K_MSEC(timeout_ms));
	k_mutex_lock(&data->lock, K_FOREVER);

	if (ret != 0) {
		LOG_ERR("%s: no DRDY within %u ms", dev->name, timeout_ms);
		(void)nafe13388_frame_cmd(dev, NAFE13388_CMD_ABORT);
		ret = -ETIMEDOUT;
		goto out;
	}

	ret = nafe13388_frame_burst(dev, data->buffer, n_channels, out_16bit, !out_16bit);
	if (ret == 0) {
		data->buffer =
			(uint8_t *)data->buffer + n_channels * nafe13388_sample_size(resolution);
	}

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

/*
 * Continuous reading: wait for DRDY, read the results of the sequence and hand
 * them to the callback, until a stop request, a callback that returns non-zero
 * or a fatal error. A DRDY that is already pending when the loop is about to
 * wait means the previous sequence took longer than the period: -EOVERFLOW.
 */
static void nafe13388_continuous_run(const struct device *dev)
{
	struct nafe13388_data *data = dev->data;
	nafe13388_seq_cb cb;
	void *user_data;
	unsigned int n;
	uint32_t timeout_ms;
	bool stopped;
	bool late;
	bool first = true;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	cb = data->cont_cb;
	user_data = data->cont_user_data;
	n = data->ext_n;
	timeout_ms = nafe13388_timeout_ms(data);
	k_mutex_unlock(&data->lock);

	while (true) {
		/*
		 * A DRDY that is already pending when the first wait starts is the first
		 * one of this reading (nafe13388_ext_begin() dropped the old ones before the
		 * command), the thread was only slow to start. It is not a late sequence.
		 */
		late = !first && k_sem_count_get(&data->drdy_sem) > 0U;
		first = false;

		ret = nafe13388_drdy_take(data, K_MSEC(timeout_ms));

		k_mutex_lock(&data->lock, K_FOREVER);
		stopped = data->cont_stop;
		if (!stopped) {
			if (ret == 0) {
				ret = nafe13388_frame_burst(dev, data->cont_raw, n, data->ext_16bit,
							    true);
			} else {
				LOG_ERR("%s: no DRDY within %u ms", dev->name, timeout_ms);
			}
			if (ret != 0) {
				(void)nafe13388_frame_cmd(dev, NAFE13388_CMD_ABORT);
			}
		}
		k_mutex_unlock(&data->lock);

		if (stopped) {
			break;
		}

		if (ret != 0) {
			(void)cb(dev, NULL, 0, ret, user_data);
			break;
		}

		ret = cb(dev, data->cont_raw, n, late ? -EOVERFLOW : 0, user_data);
		if (ret != 0) {
			k_mutex_lock(&data->lock, K_FOREVER);
			if (!data->cont_stop) {
				(void)nafe13388_frame_cmd(dev, NAFE13388_CMD_ABORT);
			}
			k_mutex_unlock(&data->lock);
			break;
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->cont_cb = NULL;
	data->owner = NAFE13388_OWNER_IDLE;
	k_mutex_unlock(&data->lock);

	k_sem_give(&data->cont_done);
}

static bool nafe13388_continuous_requested(struct nafe13388_data *data)
{
	bool requested;

	k_mutex_lock(&data->lock, K_FOREVER);
	requested = (data->owner == NAFE13388_OWNER_EXT_CONT);
	k_mutex_unlock(&data->lock);

	return requested;
}

static void nafe13388_acquisition_thread(void *p1, void *p2, void *p3)
{
	struct nafe13388_data *data = p1;
	int ret;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_sem_take(&data->acq_sem, K_FOREVER);

		if (nafe13388_continuous_requested(data)) {
			nafe13388_continuous_run(data->dev);
			continue;
		}

		ret = nafe13388_sample(data->dev);
		if (ret != 0) {
			/*
			 * adc_context_on_sampling_done() stops the interval timer only when
			 * the sequence ends well. After an error the timer would keep
			 * running and write -EBUSY into the status of the next read.
			 */
			if (data->ctx.sequence.options != NULL &&
			    data->ctx.options.interval_us != 0U) {
				adc_context_disable_timer(&data->ctx);
			}
			adc_context_complete(&data->ctx, ret);
		} else {
			adc_context_on_sampling_done(&data->ctx, data->dev);
		}
	}
}

static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct nafe13388_data *data = CONTAINER_OF(ctx, struct nafe13388_data, ctx);

	data->channels = ctx->sequence.channels;
	data->repeat_buffer = data->buffer;

	k_sem_give(&data->acq_sem);
}

static void adc_context_update_buffer_pointer(struct adc_context *ctx, bool repeat_sampling)
{
	struct nafe13388_data *data = CONTAINER_OF(ctx, struct nafe13388_data, ctx);

	if (repeat_sampling) {
		data->buffer = data->repeat_buffer;
	}
}

/* The read of the standard ADC API is over, give the device back */
static void adc_context_on_complete(struct adc_context *ctx, int status)
{
	struct nafe13388_data *data = CONTAINER_OF(ctx, struct nafe13388_data, ctx);

	ARG_UNUSED(status);

	k_mutex_lock(&data->lock, K_FOREVER);
	data->owner = NAFE13388_OWNER_IDLE;
	k_mutex_unlock(&data->lock);
}

/* The data sheet requires the same VIEX_CHOP for all channels of a sequence */
static int nafe13388_check_viex_chop(const struct device *dev, uint32_t channels)
{
	const struct nafe13388_data *data = dev->data;
	int viex_chop = -1;

	for (unsigned int ch = 0; ch < NAFE13388_NUM_CHANNELS; ch++) {
		int chop;

		if ((channels & BIT(ch)) == 0U) {
			continue;
		}

		chop = (data->channel_regs[ch][3] & NAFE13388_CH_CONFIG3_VIEX_CHOP) != 0U;
		if (viex_chop >= 0 && viex_chop != chop) {
			LOG_ERR("%s: channels with different excitation chopping", dev->name);
			return -EINVAL;
		}
		viex_chop = chop;
	}

	return 0;
}

static int nafe13388_validate_sequence(const struct device *dev,
				       const struct adc_sequence *sequence)
{
	struct nafe13388_data *data = dev->data;
	const uint32_t channels = sequence->channels;
	size_t needed;

	if (channels == 0U || (channels & ~NAFE13388_ALL_CHANNELS) != 0U) {
		LOG_ERR("%s: invalid channel mask 0x%08x", dev->name, channels);
		return -EINVAL;
	}

	if ((channels & ~data->configured) != 0U) {
		LOG_ERR("%s: channel mask 0x%08x has channels that are not set up", dev->name,
			channels);
		return -EINVAL;
	}

	if (sequence->resolution != 16U && sequence->resolution != 24U) {
		LOG_ERR("%s: unsupported resolution %u", dev->name, sequence->resolution);
		return -EINVAL;
	}

	if (sequence->oversampling != 0U || sequence->calibrate) {
		LOG_ERR("%s: oversampling and calibration are not supported", dev->name);
		return -ENOTSUP;
	}

	needed = POPCOUNT(channels) * nafe13388_sample_size(sequence->resolution);
	if (sequence->options != NULL) {
		needed *= 1U + sequence->options->extra_samplings;
	}

	if (sequence->buffer == NULL || sequence->buffer_size < needed) {
		LOG_ERR("%s: buffer too small, need %zu bytes", dev->name, needed);
		return -ENOMEM;
	}

	return nafe13388_check_viex_chop(dev, channels);
}

static int nafe13388_start_read(const struct device *dev, const struct adc_sequence *sequence)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	ret = nafe13388_validate_sequence(dev, sequence);
	if (ret != 0) {
		return ret;
	}

	/* adc_context_on_complete() gives the device back */
	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->owner != NAFE13388_OWNER_IDLE) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}
	data->owner = NAFE13388_OWNER_ADC_API;
	k_mutex_unlock(&data->lock);

	data->buffer = sequence->buffer;
	adc_context_start_read(&data->ctx, sequence);

	return adc_context_wait_for_completion(&data->ctx);
}

static int nafe13388_read_async(const struct device *dev, const struct adc_sequence *sequence,
				struct k_poll_signal *async)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	adc_context_lock(&data->ctx, async != NULL, async);
	ret = nafe13388_start_read(dev, sequence);
	adc_context_release(&data->ctx, ret);

	return ret;
}

static int nafe13388_read(const struct device *dev, const struct adc_sequence *sequence)
{
	return nafe13388_read_async(dev, sequence, NULL);
}

/* Every logical channel has to be configured once after a reset */
static int nafe13388_init_channels(const struct device *dev)
{
	const struct nafe13388_config *config = dev->config;
	int ret;

	for (unsigned int ch = 0; ch < NAFE13388_NUM_CHANNELS; ch++) {
		if (!config->extra[ch].present) {
			continue;
		}

		ret = nafe13388_channel_setup(dev, &config->dt_cfg[ch]);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int nafe13388_mode_cmd(enum nafe13388_mode mode)
{
	switch (mode) {
	case NAFE13388_MODE_SS:
		return NAFE13388_CMD_SS;
	case NAFE13388_MODE_SC:
		return NAFE13388_CMD_SC;
	case NAFE13388_MODE_MM:
		return NAFE13388_CMD_MM;
	case NAFE13388_MODE_MC:
		return NAFE13388_CMD_MC;
	case NAFE13388_MODE_MS:
		return NAFE13388_CMD_MS;
	default:
		return -EINVAL;
	}
}

/* A pulse on the SYNC pin, the device converts on the rising edge (at least 434 ns wide) */
static int nafe13388_pulse_sync(const struct device *dev)
{
	const struct nafe13388_config *config = dev->config;
	int ret;

	ret = gpio_pin_set_dt(&config->sync, 1);
	if (ret == 0) {
		k_busy_wait(1);
		ret = gpio_pin_set_dt(&config->sync, 0);
	}

	return ret;
}

/*
 * Take the device for a reading of the extension API and send the command of
 * the mode. With the SYNC pin as trigger the command only selects the mode.
 */
static int nafe13388_ext_begin(const struct device *dev, const struct nafe13388_seq *seq,
			       enum nafe13388_owner owner, nafe13388_seq_cb cb, void *user_data)
{
	const struct nafe13388_config *config = dev->config;
	struct nafe13388_data *data = dev->data;
	uint16_t sys_config0;
	unsigned int n;
	int cmd;
	int ret;

	if (seq == NULL || seq->channels == 0U ||
	    (seq->resolution != 16U && seq->resolution != 24U)) {
		return -EINVAL;
	}

	cmd = nafe13388_mode_cmd(seq->mode);
	if (cmd < 0) {
		return cmd;
	}

	n = POPCOUNT(seq->channels);
	if ((seq->mode == NAFE13388_MODE_SS || seq->mode == NAFE13388_MODE_SC) && n != 1U) {
		return -EINVAL;
	}

	if (owner == NAFE13388_OWNER_EXT_CONT &&
	    ((seq->mode != NAFE13388_MODE_SC && seq->mode != NAFE13388_MODE_MC) || cb == NULL ||
	     (seq->drdy_per_channel && n > 1U))) {
		return -EINVAL;
	}

	if (seq->sync_pin && config->sync.port == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->owner != NAFE13388_OWNER_IDLE) {
		ret = -EBUSY;
		goto out;
	}

	if ((seq->channels & ~data->configured) != 0U) {
		LOG_ERR("%s: channel mask 0x%04x has channels that are not set up", dev->name,
			seq->channels);
		ret = -EINVAL;
		goto out;
	}

	ret = nafe13388_check_viex_chop(dev, seq->channels);
	if (ret != 0) {
		goto out;
	}

	sys_config0 = data->sys_config0 &
		      ~(NAFE13388_SYS_CONFIG0_ADC_SYNC | NAFE13388_SYS_CONFIG0_DRDY_PIN_EDGE |
			NAFE13388_SYS_CONFIG0_ADC_DATA_OUT_16BIT);
	sys_config0 |= seq->sync_pin ? NAFE13388_SYS_CONFIG0_ADC_SYNC : 0;
	/*
	 * MS converts one channel per trigger. With DRDY_PIN_EDGE = 1 the device
	 * signals only after the last enabled channel (data sheet SYS_CONFIG0,
	 * Figure 28), so MS always signals every
	 * conversion.
	 */
	if (seq->mode != NAFE13388_MODE_MS && !seq->drdy_per_channel) {
		sys_config0 |= NAFE13388_SYS_CONFIG0_DRDY_PIN_EDGE;
	}
	sys_config0 |= (seq->resolution == 16U) ? NAFE13388_SYS_CONFIG0_ADC_DATA_OUT_16BIT : 0;

	ret = nafe13388_update_sys_config0(dev, sys_config0);
	if (ret != 0) {
		goto out;
	}

	/* The burst readout follows MCH_EN, also in the single channel modes */
	ret = nafe13388_update_mch_en(dev, seq->channels);
	if (ret != 0) {
		goto out;
	}

	/* Assumption: MS starts at the lowest enabled channel when it is selected here */
	if (seq->mode != NAFE13388_MODE_MM && seq->mode != NAFE13388_MODE_MC) {
		ret = nafe13388_frame_cmd(dev, NAFE13388_CMD_CH0 + find_lsb_set(seq->channels) - 1);
		if (ret != 0) {
			goto out;
		}
	}

	nafe13388_drdy_flush(data);

	ret = nafe13388_frame_cmd(dev, cmd);
	if (ret != 0) {
		goto out;
	}

	data->ext_n = n;
	data->ext_mode = seq->mode;
	data->ext_16bit = (seq->resolution == 16U);
	data->ext_sync = seq->sync_pin;
	data->cont_cb = cb;
	data->cont_user_data = user_data;
	data->cont_stop = false;
	k_sem_reset(&data->cont_done);
	data->owner = owner;

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

/* 0 if a reading of the extension API (not continuous) is active, else -EBUSY */
static int nafe13388_ext_check(const struct device *dev)
{
	struct nafe13388_data *data = dev->data;
	enum nafe13388_owner owner;

	k_mutex_lock(&data->lock, K_FOREVER);
	owner = data->owner;
	k_mutex_unlock(&data->lock);

	return (owner == NAFE13388_OWNER_EXT) ? 0 : -EBUSY;
}

int nafe13388_start(const struct device *dev, const struct nafe13388_seq *seq)
{
	return nafe13388_ext_begin(dev, seq, NAFE13388_OWNER_EXT, NULL, NULL);
}

int nafe13388_stop(const struct device *dev, enum nafe13388_stop how)
{
	struct nafe13388_data *data = dev->data;
	int ret = 0;

	if (how != NAFE13388_STOP_ABORT && how != NAFE13388_STOP_END &&
	    how != NAFE13388_STOP_IDLE) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->owner == NAFE13388_OWNER_IDLE) {
		ret = -EALREADY;
	} else if (data->owner != NAFE13388_OWNER_EXT) {
		ret = -EBUSY;
	} else {
		if (how != NAFE13388_STOP_IDLE) {
			ret = nafe13388_frame_cmd(dev, (how == NAFE13388_STOP_ABORT)
							       ? NAFE13388_CMD_ABORT
							       : NAFE13388_CMD_END);
			if (ret == 0 && how == NAFE13388_STOP_END) {
				ret = nafe13388_wait_idle(dev);
			}
		}
		/* The device is given back even if the command failed */
		data->owner = NAFE13388_OWNER_IDLE;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

int nafe13388_trigger(const struct device *dev)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->owner == NAFE13388_OWNER_IDLE) {
		ret = -EINVAL;
	} else if (data->owner != NAFE13388_OWNER_EXT) {
		ret = -EBUSY;
	} else {
		nafe13388_drdy_flush(data);
		if (data->ext_sync) {
			ret = nafe13388_pulse_sync(dev);
		} else {
			ret = nafe13388_frame_cmd(dev, nafe13388_mode_cmd(data->ext_mode));
		}
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

int nafe13388_sync_pulse(const struct device *dev)
{
	const struct nafe13388_config *config = dev->config;
	struct nafe13388_data *data = dev->data;
	int ret;

	if (config->sync.port == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if ((data->owner != NAFE13388_OWNER_EXT && data->owner != NAFE13388_OWNER_EXT_CONT) ||
	    !data->ext_sync) {
		ret = -EINVAL;
	} else {
		ret = nafe13388_pulse_sync(dev);
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

int nafe13388_drdy_arm(const struct device *dev)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	ret = nafe13388_ext_check(dev);
	if (ret == 0) {
		nafe13388_drdy_flush(data);
	}

	return ret;
}

int nafe13388_drdy_wait(const struct device *dev, k_timeout_t timeout)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	ret = nafe13388_ext_check(dev);
	if (ret != 0) {
		return ret;
	}

	return nafe13388_drdy_take(data, timeout);
}

int nafe13388_read_burst(const struct device *dev, int32_t *raw, unsigned int n)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	if (raw == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->owner != NAFE13388_OWNER_EXT) {
		ret = -EBUSY;
	} else if (n != data->ext_n) {
		ret = -EINVAL;
	} else {
		ret = nafe13388_frame_burst(dev, raw, n, data->ext_16bit, true);
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

int nafe13388_read_data(const struct device *dev, uint8_t channel, int32_t *raw)
{
	struct nafe13388_data *data = dev->data;
	uint32_t val;
	int ret;

	if (raw == NULL || channel >= NAFE13388_NUM_CHANNELS) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->owner != NAFE13388_OWNER_EXT) {
		ret = -EBUSY;
	} else if (data->ext_16bit) {
		ret = nafe13388_frame_reg_read24_16(dev, NAFE13388_REG_CH_DATA0 + channel, raw);
	} else {
		ret = nafe13388_frame_reg_read24(dev, NAFE13388_REG_CH_DATA0 + channel, &val);
		if (ret == 0) {
			*raw = sign_extend(val, 23);
		}
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

int nafe13388_continuous_start(const struct device *dev, const struct nafe13388_seq *seq,
			       nafe13388_seq_cb cb, void *user_data)
{
	struct nafe13388_data *data = dev->data;
	int ret;

	ret = nafe13388_ext_begin(dev, seq, NAFE13388_OWNER_EXT_CONT, cb, user_data);
	if (ret == 0) {
		k_sem_give(&data->acq_sem);
	}

	return ret;
}

int nafe13388_continuous_stop(const struct device *dev, enum nafe13388_stop how)
{
	struct nafe13388_data *data = dev->data;
	int ret = 0;

	if (how != NAFE13388_STOP_ABORT && how != NAFE13388_STOP_END) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->owner == NAFE13388_OWNER_IDLE) {
		ret = -EALREADY;
	} else if (data->owner != NAFE13388_OWNER_EXT_CONT) {
		ret = -EBUSY;
	} else if (data->cont_stop) {
		ret = -EALREADY;
	}

	if (ret != 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	data->cont_stop = true;
	ret = nafe13388_frame_cmd(dev, (how == NAFE13388_STOP_ABORT) ? NAFE13388_CMD_ABORT
								     : NAFE13388_CMD_END);
	if (ret == 0 && how == NAFE13388_STOP_END) {
		ret = nafe13388_wait_idle(dev);
	}
	/* Wake the loop, it may be waiting for a DRDY that will not come */
	k_sem_give(&data->drdy_sem);

	k_mutex_unlock(&data->lock);

	/* From the callback itself the loop ends when the callback returns */
	if (k_current_get() != &data->thread) {
		k_sem_take(&data->cont_done, K_FOREVER);
	}

	return ret;
}

/*
 * Extension API: device commands. A reset or a register clear sets the user
 * registers back to their defaults, so the driver brings the registers it
 * knows back to the state after the initialisation.
 */
int nafe13388_command(const struct device *dev, enum nafe13388_command_id id)
{
	struct nafe13388_data *data = dev->data;
	uint16_t code;
	int ret;

	switch (id) {
	case NAFE13388_CLEAR_ALARM:
		return nafe13388_cmd(dev, NAFE13388_CMD_CLEAR_ALARM);
	case NAFE13388_CLEAR_DATA:
		code = NAFE13388_CMD_CLEAR_DATA;
		break;
	case NAFE13388_CLEAR_REG:
		code = NAFE13388_CMD_CLEAR_REG;
		break;
	case NAFE13388_RELOAD:
		code = NAFE13388_CMD_RELOAD;
		break;
	case NAFE13388_RESET:
		code = NAFE13388_CMD_RESET;
		break;
	default:
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->owner != NAFE13388_OWNER_IDLE) {
		ret = -EBUSY;
		goto out;
	}

	ret = nafe13388_frame_cmd(dev, code);
	if (ret != 0 || id == NAFE13388_CLEAR_DATA) {
		goto out;
	}

	if (id == NAFE13388_RESET || id == NAFE13388_CLEAR_REG) {
		/* The copies of the registers are void, whatever happens next */
		data->configured = 0;
		data->mch_en = 0;
		data->sys_config0 = 0;
		memset(data->channel_regs, 0, sizeof(data->channel_regs));
	}

	ret = nafe13388_wait_ready(dev);
	if (ret != 0 || id == NAFE13388_RELOAD) {
		goto out;
	}

	if (id == NAFE13388_CLEAR_REG) {
		/*
		 * CLEAR_REG also clears the gain coefficients to 0x400000 instead of
		 * the factory values, which is a gain error of
		 * 0.4 percent. RELOAD brings the factory coefficients back from the NVM.
		 */
		ret = nafe13388_frame_cmd(dev, NAFE13388_CMD_RELOAD);
		if (ret == 0) {
			ret = nafe13388_wait_ready(dev);
		}
		if (ret != 0) {
			goto out;
		}
	}

	ret = nafe13388_frame_reg_write16(dev, NAFE13388_REG_CH_CONFIG4, 0);
	if (ret == 0) {
		ret = nafe13388_init_sys_config(dev);
	}
	if (ret == 0) {
		ret = nafe13388_init_channels(dev);
	}

out:
	k_mutex_unlock(&data->lock);

	if (ret != 0) {
		LOG_ERR("%s: command %d failed (%d)", dev->name, id, ret);
	}

	return ret;
}

static int nafe13388_init(const struct device *dev)
{
	const struct nafe13388_config *config = dev->config;
	struct nafe13388_data *data = dev->data;
	int ret;

	data->dev = dev;
	k_mutex_init(&data->lock);
	k_sem_init(&data->drdy_sem, 0, 1);
	k_sem_init(&data->acq_sem, 0, 1);
	k_sem_init(&data->cont_done, 0, 1);

	if (!spi_is_ready_dt(&config->spi)) {
		LOG_ERR("%s: SPI bus is not ready", dev->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->drdy) || !gpio_is_ready_dt(&config->reset)) {
		LOG_ERR("%s: GPIO controller is not ready", dev->name);
		return -ENODEV;
	}

	if ((config->sync.port != NULL && !gpio_is_ready_dt(&config->sync)) ||
	    (config->intb.port != NULL && !gpio_is_ready_dt(&config->intb))) {
		LOG_ERR("%s: GPIO controller is not ready", dev->name);
		return -ENODEV;
	}

	ret = nafe13388_init_drdy(dev);
	if (ret != 0) {
		return ret;
	}

	/* The SYNC pin is only used when a reading is started with sync_pin */
	if (config->sync.port != NULL) {
		ret = gpio_pin_configure_dt(&config->sync, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("%s: failed to configure SYNC pin (%d)", dev->name, ret);
			return ret;
		}
	}

	ret = nafe13388_reset(dev);
	if (ret != 0) {
		return ret;
	}

	ret = nafe13388_check_part(dev);
	if (ret != 0) {
		return ret;
	}

	ret = nafe13388_init_sys_config(dev);
	if (ret != 0) {
		return ret;
	}

	ret = nafe13388_init_channels(dev);
	if (ret != 0) {
		return ret;
	}

	k_thread_create(&data->thread, data->stack, K_KERNEL_STACK_SIZEOF(data->stack),
			nafe13388_acquisition_thread, data, NULL, NULL,
			CONFIG_ADC_NAFE13388_ACQUISITION_THREAD_PRIO, 0, K_NO_WAIT);
	/* One thread per instance, the name of the device tells them apart (needs THREAD_NAME) */
	k_thread_name_set(&data->thread, dev->name);

	adc_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static DEVICE_API(adc, nafe13388_api) = {
	.channel_setup = nafe13388_channel_setup,
	.read = nafe13388_read,
#ifdef CONFIG_ADC_ASYNC
	.read_async = nafe13388_read_async,
#endif
	.ref_internal = NAFE13388_REF_INTERNAL_MV,
};

/* Compile time checks of the channel nodes */
#define NAFE13388_CHILD_CHECK(child)                                                               \
	BUILD_ASSERT(DT_REG_ADDR(child) < NAFE13388_NUM_CHANNELS, "channel reg must be 0..15");    \
	BUILD_ASSERT(DT_PROP(child, nxp_channel_delay) <= NAFE13388_CHANNEL_DELAY_CODE_MAX,        \
		     "nxp,channel-delay must be 0..63");                                           \
	BUILD_ASSERT(DT_ENUM_HAS_VALUE(child, nxp_input_select, lv) ||                             \
			     DT_PROP(child, zephyr_differential) ||                                \
			     DT_NODE_HAS_PROP(child, zephyr_input_negative),                       \
		     "HV channels must be differential (zephyr,differential), single ended is"     \
		     " not supported");

/* The devicetree properties that are not part of the standard channel configuration */
#define NAFE13388_CHILD_EXTRA(child)                                                               \
	[DT_REG_ADDR(child)] = {                                                                   \
		.present = true,                                                                   \
		.lv = DT_ENUM_HAS_VALUE(child, nxp_input_select, lv),                              \
		.config0 = FIELD_PREP(NAFE13388_CH_CONFIG0_LVSIG_IN_MASK,                          \
				      DT_PROP(child, nxp_lv_signal)) |                             \
			   FIELD_PREP(NAFE13388_CH_CONFIG0_TCC_OFF, DT_PROP(child, nxp_tcc_off)),  \
		.config1 = FIELD_PREP(NAFE13388_CH_CONFIG1_CAL_MASK,                               \
				      DT_PROP(child, nxp_calibration_pair)) |                      \
			   FIELD_PREP(NAFE13388_CH_CONFIG1_THRS_MASK,                              \
				      DT_PROP(child, nxp_threshold_pair)) |                        \
			   FIELD_PREP(NAFE13388_CH_CONFIG1_SINC_MASK, DT_PROP(child, nxp_sinc)),   \
		.config2 = FIELD_PREP(NAFE13388_CH_CONFIG2_DELAY_MASK,                             \
				      DT_PROP(child, nxp_channel_delay)) |                         \
			   FIELD_PREP(NAFE13388_CH_CONFIG2_NORMAL_SETTLING,                        \
				      DT_PROP(child, nxp_normal_settling)) |                       \
			   FIELD_PREP(NAFE13388_CH_CONFIG2_FILTER_RESET,                           \
				      DT_PROP(child, nxp_filter_reset)) |                          \
			   FIELD_PREP(NAFE13388_CH_CONFIG2_CH_CHOP, DT_PROP(child, nxp_chop)),     \
		.config3 = FIELD_PREP(NAFE13388_CH_CONFIG3_VIEX_VI,                                \
				      DT_PROP(child, nxp_excitation_current)) |                    \
			   FIELD_PREP(NAFE13388_CH_CONFIG3_VIEX_POL,                               \
				      DT_PROP(child, nxp_excitation_negative)) |                   \
			   FIELD_PREP(NAFE13388_CH_CONFIG3_VIEX_MAG_MASK,                          \
				      DT_PROP(child, nxp_excitation_magnitude)) |                  \
			   FIELD_PREP(NAFE13388_CH_CONFIG3_VEXC_EN,                                \
				      DT_PROP(child, nxp_vexc_enable)) |                           \
			   FIELD_PREP(NAFE13388_CH_CONFIG3_ODC,                                    \
				      DT_PROP(child, nxp_open_wire_current)) |                     \
			   FIELD_PREP(NAFE13388_CH_CONFIG3_VIEX_CHOP,                              \
				      DT_PROP(child, nxp_excitation_chop)),                        \
	},

#define NAFE13388_CHILD_CFG(child) [DT_REG_ADDR(child)] = ADC_CHANNEL_CFG_DT(child),

#define NAFE13388_INIT(inst)                                                                       \
	DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, NAFE13388_CHILD_CHECK)                             \
	static const struct nafe13388_config nafe13388_config_##inst = {                           \
		.spi = SPI_DT_SPEC_INST_GET(inst, NAFE13388_SPI_OPERATION),                        \
		.drdy = GPIO_DT_SPEC_INST_GET(inst, drdy_gpios),                                   \
		.reset = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                                 \
		.sync = GPIO_DT_SPEC_INST_GET_OR(inst, sync_gpios, {0}),                           \
		.intb = GPIO_DT_SPEC_INST_GET_OR(inst, intb_gpios, {0}),                           \
		.dev_addr = DT_INST_PROP(inst, nxp_dev_addr),                                      \
		.clock_source = DT_INST_PROP(inst, nxp_clock_source),                              \
		.reference_select = DT_INST_PROP(inst, nxp_reference_select),                      \
		.drdy_wide_pulse = DT_INST_PROP(inst, nxp_drdy_wide_pulse),                        \
		.dt_cfg = {DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, NAFE13388_CHILD_CFG)},          \
		.extra = {DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, NAFE13388_CHILD_EXTRA)},         \
	};                                                                                         \
	static struct nafe13388_data nafe13388_data_##inst = {                                     \
		ADC_CONTEXT_INIT_TIMER(nafe13388_data_##inst, ctx),                                \
		ADC_CONTEXT_INIT_LOCK(nafe13388_data_##inst, ctx),                                 \
		ADC_CONTEXT_INIT_SYNC(nafe13388_data_##inst, ctx),                                 \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, nafe13388_init, NULL, &nafe13388_data_##inst,                  \
			      &nafe13388_config_##inst, POST_KERNEL, CONFIG_ADC_INIT_PRIORITY,     \
			      &nafe13388_api);

DT_INST_FOREACH_STATUS_OKAY(NAFE13388_INIT)
