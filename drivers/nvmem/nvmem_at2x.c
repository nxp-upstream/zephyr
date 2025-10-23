/*
 * Copyright (c) 2019 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/nvmem.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#define LOG_LEVEL CONFIG_NVMEM_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nvmem_at2x);

/* AT25 instruction set */
#define EEPROM_AT25_WRSR  0x01U /* Write STATUS register        */
#define EEPROM_AT25_WRITE 0x02U /* Write data to memory array   */
#define EEPROM_AT25_READ  0x03U /* Read data from memory array  */
#define EEPROM_AT25_WRDI  0x04U /* Reset the write enable latch */
#define EEPROM_AT25_RDSR  0x05U /* Read STATUS register         */
#define EEPROM_AT25_WREN  0x06U /* Set the write enable latch   */

/* AT25 status register bits */
#define EEPROM_AT25_STATUS_WIP BIT(0) /* Write-In-Process   (RO) */
#define EEPROM_AT25_STATUS_WEL BIT(1) /* Write Enable Latch (RO) */
#define EEPROM_AT25_STATUS_BP0 BIT(2) /* Block Protection 0 (RW) */
#define EEPROM_AT25_STATUS_BP1 BIT(3) /* Block Protection 1 (RW) */

#define HAS_WP_OR(id) DT_NODE_HAS_PROP(id, wp_gpios) ||
#define ANY_INST_HAS_WP_GPIOS (DT_FOREACH_STATUS_OKAY(atmel_at24, HAS_WP_OR) \
			       DT_FOREACH_STATUS_OKAY(atmel_at25, HAS_WP_OR) 0)

typedef int (*at2x_read_fn_t)(const struct device *dev, off_t offset, void *buf, size_t len);
typedef int (*at2x_write_fn_t)(const struct device *dev, off_t offset, const void *buf, size_t len);

struct nvmem_at2x_config {
	union {
#ifdef CONFIG_NVMEM_AT24
		struct i2c_dt_spec i2c;
#endif /* CONFIG_NVMEM_AT24 */
#ifdef CONFIG_NVMEM_AT25
		struct spi_dt_spec spi;
#endif /* CONFIG_NVMEM_AT25 */
	} bus;
#if ANY_INST_HAS_WP_GPIOS
	struct gpio_dt_spec wp_gpio;
#endif /* ANY_INST_HAS_WP_GPIOS */
	size_t size;
	size_t pagesize;
	uint8_t addr_width;
	bool readonly;
	uint16_t timeout;
	bool (*bus_is_ready)(const struct device *dev);
	at2x_read_fn_t read_fn;
	at2x_write_fn_t write_fn;
};

struct nvmem_at2x_data {
	struct k_mutex lock;
};

#if ANY_INST_HAS_WP_GPIOS
static inline int nvmem_at2x_write_protect(const struct device *dev)
{
	const struct nvmem_at2x_config *config = dev->config;

	if (!config->wp_gpio.port) {
		return 0;
	}

	return gpio_pin_set_dt(&config->wp_gpio, 1);
}

static inline int nvmem_at2x_write_enable(const struct device *dev)
{
	const struct nvmem_at2x_config *config = dev->config;

	if (!config->wp_gpio.port) {
		return 0;
	}

	return gpio_pin_set_dt(&config->wp_gpio, 0);
}
#endif /* ANY_INST_HAS_WP_GPIOS */

static int nvmem_at2x_read(const struct device *dev, off_t offset, void *buf, size_t len)
{
	const struct nvmem_at2x_config *config = dev->config;
	struct nvmem_at2x_data *data = dev->data;
	uint8_t *pbuf = buf;
	int ret;

	if (!len) {
		return 0;
	}

	if ((offset + len) > config->size) {
		LOG_WRN("attempt to read past device boundary");
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	while (len) {
		ret = config->read_fn(dev, offset, pbuf, len);
		if (ret < 0) {
			LOG_ERR("failed to read EEPROM (err %d)", ret);
			k_mutex_unlock(&data->lock);
			return ret;
		}

		pbuf += ret;
		offset += ret;
		len -= ret;
	}

	k_mutex_unlock(&data->lock);

	return 0;
}

static size_t nvmem_at2x_limit_write_count(const struct device *dev, off_t offset, size_t len)
{
	const struct nvmem_at2x_config *config = dev->config;
	size_t count = len;
	off_t page_boundary;

	/* We can at most write one page at a time */
	if (count > config->pagesize) {
		count = config->pagesize;
	}

	/* Writes can not cross a page boundary */
	page_boundary = ROUND_UP(offset + 1, config->pagesize);
	if (offset + count > page_boundary) {
		count = page_boundary - offset;
	}

	return count;
}

static int nvmem_at2x_write(const struct device *dev, off_t offset, const void *buf, size_t len)
{
	const struct nvmem_at2x_config *config = dev->config;
	struct nvmem_at2x_data *data = dev->data;
	const uint8_t *pbuf = buf;
	int ret;

	if (config->readonly) {
		LOG_WRN("attempt to write to read-only device");
		return -EACCES;
	}

	if (!len) {
		return 0;
	}

	if ((offset + len) > config->size) {
		LOG_WRN("attempt to write past device boundary");
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

#if ANY_INST_HAS_WP_GPIOS
	ret = nvmem_at2x_write_enable(dev);
	if (ret) {
		LOG_ERR("failed to write-enable EEPROM (err %d)", ret);
		k_mutex_unlock(&data->lock);
		return ret;
	}
#endif /* ANY_INST_HAS_WP_GPIOS */

	while (len) {
		ret = config->write_fn(dev, offset, pbuf, len);
		if (ret < 0) {
			LOG_ERR("failed to write to EEPROM (err %d)", ret);
#if ANY_INST_HAS_WP_GPIOS
			nvmem_at2x_write_protect(dev);
#endif /* ANY_INST_HAS_WP_GPIOS */
			k_mutex_unlock(&data->lock);
			return ret;
		}

		pbuf += ret;
		offset += ret;
		len -= ret;
	}

#if ANY_INST_HAS_WP_GPIOS
	ret = nvmem_at2x_write_protect(dev);
	if (ret) {
		LOG_ERR("failed to write-protect EEPROM (err %d)", ret);
	}
#else
	ret = 0;
#endif /* ANY_INST_HAS_WP_GPIOS */

	k_mutex_unlock(&data->lock);

	return ret;
}

static size_t nvmem_at2x_size(const struct device *dev)
{
	const struct nvmem_at2x_config *config = dev->config;

	return config->size;
}

#ifdef CONFIG_NVMEM_AT24
static bool nvmem_at24_bus_is_ready(const struct device *dev)
{
	const struct nvmem_at2x_config *config = dev->config;

	return device_is_ready(config->bus.i2c.bus);
}

static uint16_t nvmem_at24_translate_offset(const struct device *dev, off_t *offset)
{
	const struct nvmem_at2x_config *config = dev->config;

	const uint16_t addr_incr = *offset >> config->addr_width;
	*offset &= BIT_MASK(config->addr_width);

	return config->bus.i2c.addr + addr_incr;
}

static size_t nvmem_at24_adjust_read_count(const struct device *dev, off_t offset, size_t len)
{
	const struct nvmem_at2x_config *config = dev->config;
	const size_t remainder = BIT(config->addr_width) - offset;

	if (len > remainder) {
		len = remainder;
	}

	return len;
}

static int nvmem_at24_read(const struct device *dev, off_t offset, void *buf, size_t len)
{
	const struct nvmem_at2x_config *config = dev->config;
	int64_t timeout;
	uint8_t addr[2];
	uint16_t bus_addr;
	int err;

	bus_addr = nvmem_at24_translate_offset(dev, &offset);

	if (config->addr_width == 16) {
		sys_put_be16(offset, addr);
	} else {
		addr[0] = offset & BIT_MASK(8);
	}

	len = nvmem_at24_adjust_read_count(dev, offset, len);

	/* Retry until write cycle should be completed */
	timeout = k_uptime_get() + config->timeout;
	while (1) {
		int64_t now = k_uptime_get();
		err = i2c_write_read(config->bus.i2c.bus, bus_addr,
				     addr, config->addr_width / 8,
				     buf, len);
		if (!err || now > timeout) {
			break;
		}
		k_sleep(K_MSEC(1));
	}

	if (err < 0) {
		return err;
	}

	return (int)len;
}

static int nvmem_at24_write(const struct device *dev, off_t offset, const void *buf, size_t len)
{
	const struct nvmem_at2x_config *config = dev->config;
	int count = nvmem_at2x_limit_write_count(dev, offset, len);
	uint8_t block[2 + 256]; /* max header + max page size (safe upper bound) */
	int64_t timeout;
	uint16_t bus_addr;
	int i = 0;
	int err;

	bus_addr = nvmem_at24_translate_offset(dev, &offset);

	if (config->addr_width == 16) {
		block[i++] = offset >> 8;
	}
	block[i++] = offset;
	memcpy(&block[i], buf, count);

	/* Retry until previous write cycle should be completed */
	timeout = k_uptime_get() + config->timeout;
	while (1) {
		int64_t now = k_uptime_get();
		err = i2c_write(config->bus.i2c.bus, block, i + count, bus_addr);
		if (!err || now > timeout) {
			break;
		}
		k_sleep(K_MSEC(1));
	}

	if (err < 0) {
		return err;
	}

	return count;
}
#endif /* CONFIG_NVMEM_AT24 */

#ifdef CONFIG_NVMEM_AT25
static bool nvmem_at25_bus_is_ready(const struct device *dev)
{
	const struct nvmem_at2x_config *config = dev->config;

	return spi_is_ready_dt(&config->bus.spi);
}

static int nvmem_at25_rdsr(const struct device *dev, uint8_t *status)
{
	const struct nvmem_at2x_config *config = dev->config;
	uint8_t rdsr[2] = { EEPROM_AT25_RDSR, 0 };
	uint8_t sr[2];
	int err;
	const struct spi_buf tx_buf = {
		.buf = rdsr,
		.len = sizeof(rdsr),
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1,
	};
	const struct spi_buf rx_buf = {
		.buf = sr,
		.len = sizeof(sr),
	};
	const struct spi_buf_set rx = {
		.buffers = &rx_buf,
		.count = 1,
	};

	err = spi_transceive_dt(&config->bus.spi, &tx, &rx);
	if (!err) {
		*status = sr[1];
	}

	return err;
}

static int nvmem_at25_wait_for_idle(const struct device *dev)
{
	const struct nvmem_at2x_config *config = dev->config;
	int64_t timeout;
	uint8_t status;
	int err;

	timeout = k_uptime_get() + config->timeout;
	while (1) {
		int64_t now = k_uptime_get();
		err = nvmem_at25_rdsr(dev, &status);
		if (err) {
			LOG_ERR("Could not read status register (err %d)", err);
			return err;
		}

		if (!(status & EEPROM_AT25_STATUS_WIP)) {
			return 0;
		}
		if (now > timeout) {
			break;
		}
		k_sleep(K_MSEC(1));
	}

	return -EBUSY;
}

static int nvmem_at25_read(const struct device *dev, off_t offset, void *buf, size_t len)
{
	const struct nvmem_at2x_config *config = dev->config;
	size_t cmd_len = 1 + config->addr_width / 8;
	uint8_t cmd[4] = { EEPROM_AT25_READ, 0, 0, 0 };
	uint8_t *paddr;
	int err;
	const struct spi_buf tx_buf = {
		.buf = cmd,
		.len = cmd_len,
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1,
	};
	const struct spi_buf rx_bufs[2] = {
		{ .buf = NULL, .len = cmd_len },
		{ .buf = buf, .len = len },
	};
	const struct spi_buf_set rx = {
		.buffers = rx_bufs,
		.count = ARRAY_SIZE(rx_bufs),
	};

	if (!len) {
		return 0;
	}

	if ((offset + len) > config->size) {
		LOG_WRN("attempt to read past device boundary");
		return -EINVAL;
	}

	paddr = &cmd[1];
	switch (config->addr_width) {
	case 24:
		*paddr++ = offset >> 16;
		__fallthrough;
	case 16:
		*paddr++ = offset >> 8;
		__fallthrough;
	case 8:
		*paddr++ = offset;
		break;
	default:
		__ASSERT(0, "invalid address width");
	}

	err = nvmem_at25_wait_for_idle(dev);
	if (err) {
		LOG_ERR("EEPROM idle wait failed (err %d)", err);
		return err;
	}

	err = spi_transceive_dt(&config->bus.spi, &tx, &rx);
	if (err < 0) {
		return err;
	}

	return (int)len;
}

static int nvmem_at25_wren(const struct device *dev)
{
	const struct nvmem_at2x_config *config = dev->config;
	uint8_t cmd = EEPROM_AT25_WREN;
	const struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&config->bus.spi, &tx);
}

static int nvmem_at25_write(const struct device *dev, off_t offset, const void *buf, size_t len)
{
	const struct nvmem_at2x_config *config = dev->config;
	int count = nvmem_at2x_limit_write_count(dev, offset, len);
	uint8_t cmd[4] = { EEPROM_AT25_WRITE, 0, 0, 0 };
	size_t cmd_len = 1 + config->addr_width / 8;
	uint8_t *paddr;
	int err;
	const struct spi_buf tx_bufs[2] = {
		{ .buf = cmd, .len = cmd_len },
		{ .buf = (void *)buf, .len = count },
	};
	const struct spi_buf_set tx = { .buffers = tx_bufs, .count = ARRAY_SIZE(tx_bufs) };

	paddr = &cmd[1];
	switch (config->addr_width) {
	case 24:
		*paddr++ = offset >> 16;
		__fallthrough;
	case 16:
		*paddr++ = offset >> 8;
		__fallthrough;
	case 8:
		*paddr++ = offset;
		break;
	default:
		__ASSERT(0, "invalid address width");
	}

	err = nvmem_at25_wait_for_idle(dev);
	if (err) {
		LOG_ERR("EEPROM idle wait failed (err %d)", err);
		return err;
	}

	err = nvmem_at25_wren(dev);
	if (err) {
		LOG_ERR("failed to disable write protection (err %d)", err);
		return err;
	}

	err = spi_transceive_dt(&config->bus.spi, &tx, NULL);
	if (err) {
		return err;
	}

	return count;
}
#endif /* CONFIG_NVMEM_AT25 */

static int nvmem_at2x_init(const struct device *dev)
{
	const struct nvmem_at2x_config *config = dev->config;
	struct nvmem_at2x_data *data = dev->data;

	k_mutex_init(&data->lock);

	if (!config->bus_is_ready(dev)) {
		LOG_ERR("parent bus device not ready");
		return -EINVAL;
	}

#if ANY_INST_HAS_WP_GPIOS
	if (config->wp_gpio.port) {
		int err;
		if (!gpio_is_ready_dt(&config->wp_gpio)) {
			LOG_ERR("wp gpio device not ready");
			return -EINVAL;
		}

		err = gpio_pin_configure_dt(&config->wp_gpio, GPIO_OUTPUT_ACTIVE);
		if (err) {
			LOG_ERR("failed to configure WP GPIO pin (err %d)", err);
			return err;
		}
	}
#endif /* ANY_INST_HAS_WP_GPIOS */

	return 0;
}

static DEVICE_API(nvmem, nvmem_at2x_api) = {
	.read = nvmem_at2x_read,
	.write = nvmem_at2x_write,
	.size = nvmem_at2x_size,
};

#define ASSERT_AT24_ADDR_W_VALID(w) \
	BUILD_ASSERT(w == 8U || w == 16U, "Unsupported address width")

#define ASSERT_AT25_ADDR_W_VALID(w) \
	BUILD_ASSERT(w == 8U || w == 16U || w == 24U, "Unsupported address width")

#define ASSERT_PAGESIZE_IS_POWER_OF_2(page) \
	BUILD_ASSERT((page != 0U) && ((page & (page - 1)) == 0U), "Page size is not a power of two")

#define ASSERT_SIZE_PAGESIZE_VALID(size, page) \
	BUILD_ASSERT(size % page == 0U, "Size is not an integer multiple of page size")

#define INST_DT_AT2X(inst, t) DT_INST(inst, atmel_at##t)

#define NVMEM_AT24_BUS(n, t) \
	{ .i2c = I2C_DT_SPEC_GET(INST_DT_AT2X(n, t)) }

#define NVMEM_AT25_BUS(n, t) \
	{ .spi = SPI_DT_SPEC_GET(INST_DT_AT2X(n, t), \
				 SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8)) }

#define NVMEM_AT2X_WP_GPIOS(id) \
	IF_ENABLED(DT_NODE_HAS_PROP(id, wp_gpios), (.wp_gpio = GPIO_DT_SPEC_GET(id, wp_gpios),))

#define NVMEM_AT2X_DEVICE(n, t) \
	ASSERT_PAGESIZE_IS_POWER_OF_2(DT_PROP(INST_DT_AT2X(n, t), pagesize)); \
	ASSERT_SIZE_PAGESIZE_VALID(DT_PROP(INST_DT_AT2X(n, t), size), \
				   DT_PROP(INST_DT_AT2X(n, t), pagesize)); \
	ASSERT_AT##t##_ADDR_W_VALID(DT_PROP(INST_DT_AT2X(n, t), address_width)); \
	static const struct nvmem_at2x_config nvmem_at##t##_config_##n = { \
		.bus = NVMEM_AT##t##_BUS(n, t), \
		NVMEM_AT2X_WP_GPIOS(INST_DT_AT2X(n, t)) \
		.size = DT_PROP(INST_DT_AT2X(n, t), size), \
		.pagesize = DT_PROP(INST_DT_AT2X(n, t), pagesize), \
		.addr_width = DT_PROP(INST_DT_AT2X(n, t), address_width), \
		.readonly = DT_PROP(INST_DT_AT2X(n, t), read_only), \
		.timeout = DT_PROP(INST_DT_AT2X(n, t), timeout), \
		.bus_is_ready = nvmem_at##t##_bus_is_ready, \
		.read_fn = nvmem_at##t##_read, \
		.write_fn = nvmem_at##t##_write, \
	}; \
	static struct nvmem_at2x_data nvmem_at##t##_data_##n; \
	DEVICE_DT_DEFINE(INST_DT_AT2X(n, t), nvmem_at2x_init, NULL, \
			&nvmem_at##t##_data_##n, &nvmem_at##t##_config_##n, \
			POST_KERNEL, CONFIG_NVMEM_INIT_PRIORITY, &nvmem_at2x_api)

#define NVMEM_AT24_DEVICE(n) NVMEM_AT2X_DEVICE(n, 24)
#define NVMEM_AT25_DEVICE(n) NVMEM_AT2X_DEVICE(n, 25)

#define CALL_WITH_ARG(arg, expr) expr(arg);

#define INST_DT_AT2X_FOREACH(t, inst_expr) \
	LISTIFY(DT_NUM_INST_STATUS_OKAY(atmel_at##t), CALL_WITH_ARG, (), inst_expr)

#ifdef CONFIG_NVMEM_AT24
INST_DT_AT2X_FOREACH(24, NVMEM_AT24_DEVICE);
#endif

#ifdef CONFIG_NVMEM_AT25
INST_DT_AT2X_FOREACH(25, NVMEM_AT25_DEVICE);
#endif
