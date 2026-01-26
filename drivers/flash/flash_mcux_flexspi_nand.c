/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_flexspi_nand

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include "memc_mcux_flexspi.h"

LOG_MODULE_REGISTER(flash_flexspi_nand, CONFIG_LOG_DEFAULT_LEVEL);

/* NAND (SPI-NAND command set) common commands */
#define SPI_NAND_CMD_GET_FEATURE  0x0FU
#define SPI_NAND_CMD_SET_FEATURE  0x1FU
#define SPI_NAND_CMD_READ_ID      0x9FU
#define SPI_NAND_CMD_RESET        0xFFU
#define SPI_NAND_CMD_PAGE_READ    0x13U
#define SPI_NAND_CMD_READ_CACHE   0x03U
#define SPI_NAND_CMD_WRITE_ENABLE 0x06U
#define SPI_NAND_CMD_PROG_LOAD    0x02U
#define SPI_NAND_CMD_PROG_EXEC    0x10U
#define SPI_NAND_CMD_BLOCK_ERASE  0xD8U

/* Feature addresses (common across multiple vendors) */
#define SPI_NAND_FEATURE_ADDR_BLOCK_PROT 0xA0U
#define SPI_NAND_FEATURE_ADDR_CONFIG     0xB0U
#define SPI_NAND_FEATURE_ADDR_STATUS     0xC0U

/* STATUS register */
#define SPI_NAND_STATUS_OIP BIT(0)
#define SPI_NAND_STATUS_WEL BIT(1)
#define SPI_NAND_STATUS_E_FAIL BIT(2)
#define SPI_NAND_STATUS_P_FAIL BIT(3)

/* CONFIG register bits */
#define SPI_NAND_CFG_ECC_EN BIT(4)
#define SPI_NAND_CFG_OTP_EN BIT(6)

/* ONFI parameter page: treat as a raw 256-byte block.
 * CRC is calculated over the first 254 bytes, with seed 0x4F4E.
 */
struct nand_onfi_parameter_page_raw {
	uint8_t data[256];
};

enum {
	LUT_RESET = 0,
	LUT_GET_STATUS,
	LUT_GET_CFG,
	LUT_SET_CFG,
	LUT_SET_BP,
	LUT_READ_ID,
	LUT_PAGE_READ,
	LUT_READ_CACHE,
	LUT_WREN,
	LUT_PROG_LOAD,
	LUT_PROG_EXEC,
	LUT_BLOCK_ERASE,
	LUT_END,
};

struct flash_flexspi_nand_config {
	const struct device *controller;
};

struct flash_flexspi_nand_data {
	const struct device *controller;
	flexspi_device_config_t config;
	flexspi_port_t port;
	uint64_t size;
	uint32_t page_size;
	uint16_t oob_size;
	uint32_t pages_per_block;
	uint32_t blocks_per_unit;
	uint8_t units;
	uint32_t block_size;
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	struct flash_pages_layout layout;
#endif
	struct flash_parameters flash_parameters;
};

/* LUT_READ_CACHE read step is configured for 128B. Keep transfers within that. */
#define NAND_CACHE_READ_MAX 128U

/* LUT entries are 4 bytes each, and sequences are 4 entries.
 * All probe traffic is 1-1-1 per milestone constraints.
 */
static uint32_t flexspi_lut[LUT_END][MEMC_FLEXSPI_CMD_PER_SEQ];

static void flexspi_lut_init(void)
{
	static bool initialized;

	if (initialized) {
		return;
	}

	/* RESET (0xFF) */
	flexspi_lut[LUT_RESET][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_RESET,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0);

	/* GET_FEATURE STATUS (0x0F, 0xC0) -> read 1 */
	flexspi_lut[LUT_GET_STATUS][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_GET_FEATURE,
				kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_FEATURE_ADDR_STATUS);
	flexspi_lut[LUT_GET_STATUS][1] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0);

	/* GET_FEATURE CFG (0x0F, 0xB0) -> read 1 */
	flexspi_lut[LUT_GET_CFG][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_GET_FEATURE,
				kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_FEATURE_ADDR_CONFIG);
	flexspi_lut[LUT_GET_CFG][1] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0);

	/* SET_FEATURE CFG (0x1F, 0xB0) <- write 1 */
	flexspi_lut[LUT_SET_CFG][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_SET_FEATURE,
				kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_FEATURE_ADDR_CONFIG);
	flexspi_lut[LUT_SET_CFG][1] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0);

	/* SET_FEATURE BLOCK PROTECT (0x1F, 0xA0) <- write 1 */
	flexspi_lut[LUT_SET_BP][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_SET_FEATURE,
				kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_FEATURE_ADDR_BLOCK_PROT);
	flexspi_lut[LUT_SET_BP][1] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0);

	/* READ_ID (0x9F) + 1 dummy byte -> read 3 */
	flexspi_lut[LUT_READ_ID][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_READ_ID,
				kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_1PAD, 0x08);
	flexspi_lut[LUT_READ_ID][1] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x03,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0);

	/* PAGE_READ (0x13) + 24-bit row address */
	flexspi_lut[LUT_PAGE_READ][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_PAGE_READ,
				kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18);

	/* READ_CACHE (0x03) + 16-bit column address + dummy -> read N */
	flexspi_lut[LUT_READ_CACHE][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_READ_CACHE,
				kFLEXSPI_Command_CADDR_SDR, kFLEXSPI_1PAD, 0x10);
	flexspi_lut[LUT_READ_CACHE][1] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_1PAD, 0x08,
				kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x80);

	/* WRITE_ENABLE (0x06) */
	flexspi_lut[LUT_WREN][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_WRITE_ENABLE,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0);

	/* PROG_LOAD (0x02) + 16-bit column address <- write N */
	flexspi_lut[LUT_PROG_LOAD][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_PROG_LOAD,
				kFLEXSPI_Command_CADDR_SDR, kFLEXSPI_1PAD, 0x10);
	flexspi_lut[LUT_PROG_LOAD][1] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 0x80,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0);

	/* PROG_EXEC (0x10) + 24-bit row address */
	flexspi_lut[LUT_PROG_EXEC][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_PROG_EXEC,
				kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18);

	/* BLOCK_ERASE (0xD8) + 24-bit row address */
	flexspi_lut[LUT_BLOCK_ERASE][0] =
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_BLOCK_ERASE,
				kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18);

	initialized = true;
}

static int nand_xfer(const struct device *dev, uint8_t seq_index, flexspi_command_type_t type,
		     uint32_t addr, void *data, size_t data_len)
{
	struct flash_flexspi_nand_data *nand = dev->data;
	flexspi_transfer_t transfer = {
		.deviceAddress = addr,
		.port = nand->port,
		.cmdType = type,
		.SeqNumber = 1,
		.seqIndex = seq_index,
		.data = data,
		.dataSize = data_len,
	};

	return memc_flexspi_transfer(nand->controller, &transfer);
}

static int nand_get_feature(const struct device *dev, uint8_t reg, uint8_t *val)
{
	uint32_t tmp = 0;
	int ret;

	if (reg == SPI_NAND_FEATURE_ADDR_STATUS) {
		ret = nand_xfer(dev, LUT_GET_STATUS, kFLEXSPI_Read, 0, &tmp, 1);
	} else if (reg == SPI_NAND_FEATURE_ADDR_CONFIG) {
		ret = nand_xfer(dev, LUT_GET_CFG, kFLEXSPI_Read, 0, &tmp, 1);
	} else {
		return -ENOTSUP;
	}

	*val = (uint8_t)tmp;
	return ret;
}

static int nand_set_cfg(const struct device *dev, uint8_t cfg)
{
	uint32_t tmp = cfg;

	return nand_xfer(dev, LUT_SET_CFG, kFLEXSPI_Write, 0, &tmp, 1);
}

static int nand_set_block_protect(const struct device *dev, uint8_t bp)
{
	uint32_t tmp = bp;

	return nand_xfer(dev, LUT_SET_BP, kFLEXSPI_Write, 0, &tmp, 1);
}

static int nand_write_enable(const struct device *dev)
{
	uint8_t status;
	int ret;

	ret = nand_xfer(dev, LUT_WREN, kFLEXSPI_Command, 0, NULL, 0);
	if (ret != 0) {
		return ret;
	}

	ret = nand_get_feature(dev, SPI_NAND_FEATURE_ADDR_STATUS, &status);
	if (ret != 0) {
		return ret;
	}

	return ((status & SPI_NAND_STATUS_WEL) != 0U) ? 0 : -EACCES;
}

static int nand_wait_until_ready(const struct device *dev, uint32_t timeout_us)
{
	int64_t end_ms = k_uptime_get() + MAX(1, (int64_t)DIV_ROUND_UP(timeout_us, 1000));
	uint8_t status;
	int ret;

	do {
		ret = nand_get_feature(dev, SPI_NAND_FEATURE_ADDR_STATUS, &status);
		if (ret != 0) {
			return ret;
		}
		if ((status & SPI_NAND_STATUS_OIP) == 0U) {
			return 0;
		}
		k_busy_wait(5);
	} while (k_uptime_get() < end_ms);

	return -ETIMEDOUT;
}

static int nand_reset(const struct device *dev)
{
	int ret = nand_xfer(dev, LUT_RESET, kFLEXSPI_Command, 0, NULL, 0);

	if (ret != 0) {
		return ret;
	}

	/* Reset completion is typically fast; keep conservative timeout. */
	return nand_wait_until_ready(dev, 50000);
}

static int nand_read_id(const struct device *dev, uint8_t id[3])
{
	uint32_t tmp = 0;
	int ret = nand_xfer(dev, LUT_READ_ID, kFLEXSPI_Read, 0, &tmp, 3);

	if (ret != 0) {
		return ret;
	}

	id[0] = (uint8_t)(tmp & 0xFF);
	id[1] = (uint8_t)((tmp >> 8) & 0xFF);
	id[2] = (uint8_t)((tmp >> 16) & 0xFF);
	return 0;
}

static int nand_onfi_read_and_log(const struct device *dev)
{
	struct nand_onfi_parameter_page_raw onfi;
	uint16_t computed_crc;
	uint8_t cfg;
	uint8_t cfg_restore;
	struct flash_flexspi_nand_data *nand = dev->data;
	int ret;

	ret = nand_get_feature(dev, SPI_NAND_FEATURE_ADDR_CONFIG, &cfg);
	if (ret != 0) {
		return ret;
	}

	cfg_restore = cfg;

	/* Ensure on-die ECC is enabled, and enable OTP/parameter access.
	 * This matches the generic approach being proposed in Zephyr PR #100845.
	 */
	cfg |= (SPI_NAND_CFG_ECC_EN | SPI_NAND_CFG_OTP_EN);
	ret = nand_set_cfg(dev, cfg);
	if (ret != 0) {
		return ret;
	}

	ret = nand_wait_until_ready(dev, 50000);
	if (ret != 0) {
		return ret;
	}

	/* Load parameter page into cache.
	 * Many SPI-NAND devices expose the ONFI parameter page when OTP is enabled.
	 */
	ret = nand_xfer(dev, LUT_PAGE_READ, kFLEXSPI_Command, 1, NULL, 0);
	if (ret != 0) {
		goto restore;
	}

	ret = nand_wait_until_ready(dev, 50000);
	if (ret != 0) {
		goto restore;
	}

	memset(&onfi, 0, sizeof(onfi));
	ret = nand_xfer(dev, LUT_READ_CACHE, kFLEXSPI_Read, 0, &onfi, sizeof(onfi));
	if (ret != 0) {
		goto restore;
	}

	if (memcmp(&onfi.data[0], "ONFI", 4) != 0) {
		ret = -EINVAL;
		goto restore;
	}

	computed_crc = crc16(CRC16_POLY, 0x4F4E, (void *)&onfi, sizeof(onfi) - sizeof(uint16_t));
	if (computed_crc != sys_get_le16(&onfi.data[254])) {
		ret = -EIO;
		goto restore;
	}

	/* ONFI strings are ASCII, space padded.
	 * Manufacturer is 12 bytes @ offset 32, Model is 20 bytes @ offset 44.
	 */
	char manufacturer[13];
	char model[21];
	memcpy(manufacturer, &onfi.data[32], 12);
	manufacturer[12] = '\0';
	memcpy(model, &onfi.data[44], 20);
	model[20] = '\0';

	LOG_INF("NAND probe OK (ONFI CRC %04X)", computed_crc);
	LOG_INF("Manufacturer: %s", manufacturer);
	LOG_INF("Model: %s", model);

	nand->page_size = sys_get_le32(&onfi.data[80]);
	nand->oob_size = sys_get_le16(&onfi.data[84]);
	nand->pages_per_block = sys_get_le32(&onfi.data[92]);
	nand->blocks_per_unit = sys_get_le32(&onfi.data[96]);
	nand->units = onfi.data[100];
	nand->block_size = nand->page_size * nand->pages_per_block;

	LOG_INF("Page size (data): %u", nand->page_size);
	LOG_INF("Page size (spare/OOB): %u", nand->oob_size);
	LOG_INF("Pages per block: %u", nand->pages_per_block);
	LOG_INF("Blocks per unit: %u", nand->blocks_per_unit);
	LOG_INF("Units: %u", nand->units);

	/* Keep DT size as the authoritative limit, but log if it disagrees with ONFI. */
	if ((nand->page_size != 0U) && (nand->pages_per_block != 0U) && (nand->blocks_per_unit != 0U) &&
	    (nand->units != 0U)) {
		uint64_t onfi_size = (uint64_t)nand->page_size * (uint64_t)nand->pages_per_block *
				  (uint64_t)nand->blocks_per_unit * (uint64_t)nand->units;
		if ((onfi_size != 0U) && (nand->size != onfi_size)) {
			LOG_WRN("DT size (%llu) != ONFI size (%llu)", nand->size, onfi_size);
		}
	}

restore:
	/* Restore config register to its previous state (clear OTP_EN if we set it).
	 * Keep ECC enabled if it was enabled.
	 */
	(void)nand_set_cfg(dev, (cfg_restore | (cfg & SPI_NAND_CFG_ECC_EN)) & ~SPI_NAND_CFG_OTP_EN);
	(void)nand_wait_until_ready(dev, 50000);
	return ret;
}

static int flash_flexspi_nand_read(const struct device *dev, off_t offset, void *data,
			      size_t len)
{
	struct flash_flexspi_nand_data *nand = dev->data;
	uint8_t *out = data;
	uint64_t off = (uint64_t)offset;
	int ret;

	if (data == NULL) {
		return -EINVAL;
	}
	if (offset < 0) {
		return -EINVAL;
	}
	if (len == 0) {
		return 0;
	}
	if ((nand->page_size == 0U) || (nand->page_size > 0xFFFFU)) {
		/* Requires successful ONFI probe (page_size) and a column address that fits 16-bit. */
		return -EIO;
	}
	if ((off + (uint64_t)len) > nand->size) {
		return -EINVAL;
	}

	while (len > 0U) {
		uint32_t page = (uint32_t)(off / nand->page_size);
		uint32_t col = (uint32_t)(off % nand->page_size);
		size_t chunk = MIN(len, (size_t)(nand->page_size - col));

		/* LUT_READ_CACHE is set up for 128-byte reads; chunk accordingly. */
		chunk = MIN(chunk, (size_t)NAND_CACHE_READ_MAX);

		ret = nand_xfer(dev, LUT_PAGE_READ, kFLEXSPI_Command, page, NULL, 0);
		if (ret != 0) {
			return ret;
		}
		ret = nand_wait_until_ready(dev, 50000);
		if (ret != 0) {
			return ret;
		}

		ret = nand_xfer(dev, LUT_READ_CACHE, kFLEXSPI_Read, col, out, chunk);
		if (ret != 0) {
			return ret;
		}

		off += chunk;
		out += chunk;
		len -= chunk;
	}

	return 0;
}

static int flash_flexspi_nand_write(const struct device *dev, off_t offset, const void *data,
			       size_t len)
{
	struct flash_flexspi_nand_data *nand = dev->data;
	const uint8_t *in = data;
	uint64_t off = (uint64_t)offset;
	int ret;

	if (data == NULL) {
		return -EINVAL;
	}
	if (offset < 0) {
		return -EINVAL;
	}
	if (len == 0) {
		return 0;
	}
	if ((nand->page_size == 0U) || (nand->page_size > 0xFFFFU)) {
		return -EIO;
	}
	if ((off + (uint64_t)len) > nand->size) {
		return -EINVAL;
	}

	while (len > 0U) {
		uint32_t page = (uint32_t)(off / nand->page_size);
		uint32_t col = (uint32_t)(off % nand->page_size);
		size_t chunk = MIN(len, (size_t)(nand->page_size - col));

		chunk = MIN(chunk, (size_t)NAND_CACHE_READ_MAX);

		ret = nand_write_enable(dev);
		if (ret != 0) {
			return ret;
		}

		ret = nand_xfer(dev, LUT_PROG_LOAD, kFLEXSPI_Write, col, (void *)in, chunk);
		if (ret != 0) {
			return ret;
		}

		ret = nand_xfer(dev, LUT_PROG_EXEC, kFLEXSPI_Command, page, NULL, 0);
		if (ret != 0) {
			return ret;
		}

		ret = nand_wait_until_ready(dev, 500000);
		if (ret != 0) {
			return ret;
		}

		uint8_t status;
		ret = nand_get_feature(dev, SPI_NAND_FEATURE_ADDR_STATUS, &status);
		if (ret != 0) {
			return ret;
		}
		if ((status & SPI_NAND_STATUS_P_FAIL) != 0U) {
			return -EIO;
		}

		off += chunk;
		in += chunk;
		len -= chunk;
	}

	return 0;
}

static int flash_flexspi_nand_erase(const struct device *dev, off_t offset, size_t size)
{
	struct flash_flexspi_nand_data *nand = dev->data;
	uint64_t off = (uint64_t)offset;
	uint32_t blk_sz;
	int ret;

	if (offset < 0) {
		return -EINVAL;
	}
	if (size == 0U) {
		return 0;
	}
	if (nand->block_size == 0U) {
		return -EIO;
	}
	if ((off + (uint64_t)size) > nand->size) {
		return -EINVAL;
	}

	blk_sz = nand->block_size;
	if (((off % blk_sz) != 0U) || ((size % blk_sz) != 0U)) {
		return -EINVAL;
	}

	while (size > 0U) {
		uint32_t row = (uint32_t)(off / nand->page_size);

		ret = nand_write_enable(dev);
		if (ret != 0) {
			return ret;
		}

		ret = nand_xfer(dev, LUT_BLOCK_ERASE, kFLEXSPI_Command, row, NULL, 0);
		if (ret != 0) {
			return ret;
		}

		ret = nand_wait_until_ready(dev, 3000000);
		if (ret != 0) {
			return ret;
		}

		uint8_t status;
		ret = nand_get_feature(dev, SPI_NAND_FEATURE_ADDR_STATUS, &status);
		if (ret != 0) {
			return ret;
		}
		if ((status & SPI_NAND_STATUS_E_FAIL) != 0U) {
			return -EIO;
		}

		off += blk_sz;
		size -= blk_sz;
	}

	return 0;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void flash_flexspi_nand_page_layout(const struct device *dev,
					     const struct flash_pages_layout **layout,
					     size_t *layout_size)
{
	struct flash_flexspi_nand_data *nand = dev->data;

	*layout = &nand->layout;
	*layout_size = 1;
}
#endif

static const struct flash_parameters *flash_flexspi_nand_get_parameters(const struct device *dev)
{
	struct flash_flexspi_nand_data *nand = dev->data;

	return &nand->flash_parameters;
}

static int flash_flexspi_nand_get_size(const struct device *dev, uint64_t *size)
{
	struct flash_flexspi_nand_data *nand = dev->data;

	*size = nand->size;
	return 0;
}

static int flash_flexspi_nand_init(const struct device *dev)
{
	struct flash_flexspi_nand_data *nand = dev->data;
	uint8_t id[3];
	int ret;

	flexspi_lut_init();

	if (!device_is_ready(nand->controller)) {
		LOG_ERR("FlexSPI controller not ready");
		return -ENODEV;
	}

	ret = memc_flexspi_set_device_config(nand->controller, &nand->config,
					  (const uint32_t *)flexspi_lut,
					  sizeof(flexspi_lut) / MEMC_FLEXSPI_CMD_SIZE,
					  nand->port);
	if (ret != 0) {
		LOG_ERR("Failed to configure FlexSPI NAND (%d)", ret);
		return ret;
	}

	memc_flexspi_reset(nand->controller);

	ret = nand_reset(dev);
	if (ret != 0) {
		LOG_ERR("NAND reset failed (%d)", ret);
		return ret;
	}

	/* Clear all block protection bits for bring-up and flash_shell convenience. */
	ret = nand_set_block_protect(dev, 0x00);
	if (ret != 0) {
		LOG_WRN("Failed to clear block protect (%d)", ret);
	}

	ret = nand_read_id(dev, id);
	if (ret != 0) {
		LOG_ERR("NAND read-id failed (%d)", ret);
		return ret;
	}
	LOG_INF("JEDEC ID (raw): %02X %02X %02X", id[0], id[1], id[2]);

	ret = nand_onfi_read_and_log(dev);
	if (ret != 0) {
		LOG_ERR("ONFI parameter read/CRC failed (%d)", ret);
		return ret;
	}

	if (nand->page_size == 0U) {
		LOG_ERR("ONFI did not provide a valid page size");
		return -EIO;
	}
	if ((nand->pages_per_block == 0U) || (nand->block_size == 0U)) {
		LOG_ERR("ONFI did not provide a valid block geometry");
		return -EIO;
	}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	nand->layout.pages_size = nand->block_size;
	nand->layout.pages_count = (size_t)(nand->size / nand->block_size);
#endif

	return 0;
}

static DEVICE_API(flash, flash_flexspi_nand_api) = {
	.erase = flash_flexspi_nand_erase,
	.write = flash_flexspi_nand_write,
	.read = flash_flexspi_nand_read,
	.get_parameters = flash_flexspi_nand_get_parameters,
	.get_size = flash_flexspi_nand_get_size,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = flash_flexspi_nand_page_layout,
#endif
};

#define CONCAT3(x, y, z) x ## y ## z
#define CS_INTERVAL_UNIT(unit) CONCAT3(kFLEXSPI_CsIntervalUnit, unit, SckCycle)
#define AHB_WRITE_WAIT_UNIT(unit) CONCAT3(kFLEXSPI_AhbWriteWaitUnit, unit, AhbCycle)

#define FLASH_FLEXSPI_DEVICE_CONFIG(n)                                                             \
	{                                                                                           \
		.flexspiRootClk = DT_INST_PROP(n, spi_max_frequency),                                 \
		.flashSize = DT_INST_PROP(n, size) / 8 / KB(1),                                       \
		.CSIntervalUnit = CS_INTERVAL_UNIT(DT_INST_PROP(n, cs_interval_unit)),                \
		.CSInterval = DT_INST_PROP(n, cs_interval),                                           \
		.CSHoldTime = DT_INST_PROP(n, cs_hold_time),                                          \
		.CSSetupTime = DT_INST_PROP(n, cs_setup_time),                                        \
		.dataValidTime = DT_INST_PROP(n, data_valid_time),                                    \
		.columnspace = DT_INST_PROP(n, column_space),                                         \
		.enableWordAddress = DT_INST_PROP(n, word_addressable),                               \
		.AWRSeqIndex = 0,                                                                     \
		.AWRSeqNumber = 0,                                                                    \
		.ARDSeqIndex = 0,                                                                     \
		.ARDSeqNumber = 1,                                                                    \
		.AHBWriteWaitUnit = AHB_WRITE_WAIT_UNIT(DT_INST_PROP(n, ahb_write_wait_unit)),        \
		.AHBWriteWaitInterval = DT_INST_PROP(n, ahb_write_wait_interval),                     \
	}

#define FLASH_FLEXSPI_NAND(n)                                                                       \
	static const struct flash_flexspi_nand_config flash_flexspi_nand_config_##n = {              \
		.controller = DEVICE_DT_GET(DT_INST_BUS(n)),                                            \
	};                                                                                          \
	static struct flash_flexspi_nand_data flash_flexspi_nand_data_##n = {                        \
		.controller = DEVICE_DT_GET(DT_INST_BUS(n)),                                            \
		.config = FLASH_FLEXSPI_DEVICE_CONFIG(n),                                                \
		.port = DT_INST_REG_ADDR(n),                                                            \
		.size = DT_INST_PROP(n, size) / 8,                                                      \
		.page_size = 0,                                                                         \
		.oob_size = 0,                                                                          \
		.pages_per_block = 0,                                                                   \
		.blocks_per_unit = 0,                                                                   \
		.units = 0,                                                                             \
		.flash_parameters = {                                                                   \
			.write_block_size = 1,                                                            \
			.erase_value = 0xff,                                                             \
		},                                                                                       \
	};                                                                                          \
	DEVICE_DT_INST_DEFINE(n, flash_flexspi_nand_init, NULL, &flash_flexspi_nand_data_##n,        \
			      &flash_flexspi_nand_config_##n, POST_KERNEL, CONFIG_FLASH_INIT_PRIORITY,      \
			      &flash_flexspi_nand_api);

DT_INST_FOREACH_STATUS_OKAY(FLASH_FLEXSPI_NAND)
