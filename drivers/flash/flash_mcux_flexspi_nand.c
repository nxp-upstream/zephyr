/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_flexspi_nand

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include "memc_mcux_flexspi.h"

LOG_MODULE_REGISTER(flash_flexspi_nand, CONFIG_LOG_DEFAULT_LEVEL);

/* SPI-NAND common commands. */
#define SPI_NAND_CMD_READ_REG     0x0FU
#define SPI_NAND_CMD_WRITE_REG    0x1FU
#define SPI_NAND_CMD_READ_ID      0x9FU
#define SPI_NAND_CMD_RESET        0xFFU
#define SPI_NAND_CMD_PAGE_READ    0x13U
#define SPI_NAND_CMD_READ_CACHE   0x03U
#define SPI_NAND_CMD_WRITE_ENABLE 0x06U
#define SPI_NAND_CMD_PROG_LOAD    0x32U
#define SPI_NAND_CMD_PROG_EXEC    0x10U
#define SPI_NAND_CMD_BLOCK_ERASE  0xD8U

/* Register addresses. */
#define SPI_NAND_REG_BLOCK_PROT 0xA0U
#define SPI_NAND_REG_CONFIG     0xB0U
#define SPI_NAND_REG_STATUS     0xC0U

/* STATUS register bits. */
#define SPI_NAND_STATUS_BUSY      BIT(0)
#define SPI_NAND_STATUS_WEL       BIT(1)
#define SPI_NAND_STATUS_E_FAIL    BIT(2)
#define SPI_NAND_STATUS_P_FAIL    BIT(3)
#define SPI_NAND_STATUS_ECC_MASK  (BIT(4) | BIT(5))
#define SPI_NAND_STATUS_ECC_SHIFT 4

/* CONFIG register bits. */
#define SPI_NAND_CFG_ECC_EN BIT(4)
#define SPI_NAND_CFG_OTP_EN BIT(6)

/* Unified timeout for all NAND busy wait operations (us) */
#ifndef FLASH_NAND_WAIT_BUSY_TIMEOUT_US
#define FLASH_NAND_WAIT_BUSY_TIMEOUT_US 0U
#endif

/* ONFI parameter page, treat as a raw 256-byte block.
 * CRC is calculated over the first 254 bytes, with seed 0x4F4E.
 */
struct nand_onfi_parameter_page_raw {
	uint8_t data[256];
};

enum {
	DYNAMIC_SEQ = 0,
	PAGE_READ,
	READ_CACHE,
	PROG_LOAD,
	PROG_EXEC,
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

static const uint32_t flexspi_lut[LUT_END][MEMC_FLEXSPI_CMD_PER_SEQ] = {
	[DYNAMIC_SEQ] = {
		0
	},

	[PAGE_READ] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_PAGE_READ,
				kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18),
	},

	[READ_CACHE] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_READ_CACHE,
				kFLEXSPI_Command_CADDR_SDR, kFLEXSPI_1PAD, 0x10),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_1PAD, 0x08,
				kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x80),
	},

	[PROG_LOAD] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_PROG_LOAD,
				kFLEXSPI_Command_CADDR_SDR, kFLEXSPI_1PAD, 0x10),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_4PAD, 0x40,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),
	},

	[PROG_EXEC] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_PROG_EXEC,
				kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18),
	},
};

static ALWAYS_INLINE bool area_is_subregion(const struct device *dev, off_t offset, size_t size)
{
	struct flash_flexspi_nand_data *nand = dev->data;

	return ((offset >= 0) && (offset < nand->size) &&
		((nand->size - offset) >= size));
}

static int flash_flexspi_nand_xfer(const struct device *dev, uint8_t seq_index, flexspi_command_type_t type,
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

static int flash_flexspi_update_lut(const struct device *dev, const uint32_t *lut_ptr)
{
	struct flash_flexspi_nand_data *nand = dev->data;

	return memc_flexspi_update_lut(nand->controller, nand->port, DYNAMIC_SEQ, lut_ptr, MEMC_FLEXSPI_CMD_PER_SEQ);
}

static int flash_flexspi_nand_read_reg(const struct device *dev, uint8_t reg, uint8_t *value)
{
	const uint32_t lut_read_reg[MEMC_FLEXSPI_CMD_PER_SEQ] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_READ_REG,
				kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, reg),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),
	};
	uint32_t data = 0;
	int ret;

	ret = flash_flexspi_update_lut(dev, lut_read_reg);
	if (ret != 0) {
		return ret;
	}

	ret = flash_flexspi_nand_xfer(dev, DYNAMIC_SEQ, kFLEXSPI_Read, 0, &data, 1);

	*value = (uint8_t)data;

	return ret;
}

static int flash_flexspi_nand_write_reg(const struct device *dev, uint8_t reg, uint8_t value)
{
	const uint32_t lut_write_reg[MEMC_FLEXSPI_CMD_PER_SEQ] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_WRITE_REG,
				kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, reg),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR, kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),
	};
	int ret;

	ret = flash_flexspi_update_lut(dev, lut_write_reg);
	if (ret != 0) {
		return ret;
	}

	return flash_flexspi_nand_xfer(dev, DYNAMIC_SEQ, kFLEXSPI_Write, 0, &value, 1);
}

static int flash_flexspi_nand_set_block_protect(const struct device *dev, uint8_t value)
{
	return flash_flexspi_nand_write_reg(dev, SPI_NAND_REG_BLOCK_PROT, value);
}

static int flash_flexspi_nand_write_enable(const struct device *dev)
{
	const uint32_t lut_we[MEMC_FLEXSPI_CMD_PER_SEQ] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_WRITE_ENABLE,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),
	};
	int ret;

	ret = flash_flexspi_update_lut(dev, lut_we);
	if (ret != 0) {
		return ret;
	}

	return flash_flexspi_nand_xfer(dev, DYNAMIC_SEQ, kFLEXSPI_Command, 0, NULL, 0);
}

static int flash_flexspi_nand_wait_busy(const struct device *dev, uint32_t timeout_us)
{
	int infinite = (timeout_us == 0);
	int64_t end_ms = 0;
	uint8_t status;
	int ret;

	if (!infinite) {
		end_ms = k_uptime_get() + MAX(1, (int64_t)DIV_ROUND_UP(timeout_us, 1000));
	}
	while (infinite || k_uptime_get() < end_ms) {
		ret = flash_flexspi_nand_read_reg(dev, SPI_NAND_REG_STATUS, &status);
		if (ret != 0) {
			return ret;
		}
		if ((status & SPI_NAND_STATUS_BUSY) == 0U) {
			return 0;
		}
		k_busy_wait(5);
	}
	return infinite ? 0 : -ETIMEDOUT;
}

static int flash_flexspi_nand_reset(const struct device *dev)
{
	const uint32_t lut_reset[MEMC_FLEXSPI_CMD_PER_SEQ] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_RESET,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),
	};
	int ret;

	/* Check BUSY=0 before issuing RESET to avoid data corruption. */
	ret = flash_flexspi_nand_wait_busy(dev, FLASH_NAND_WAIT_BUSY_TIMEOUT_US);
	if (ret != 0) {
		LOG_ERR("Device still busy before RESET, aborting");
		return ret;
	}

	ret = flash_flexspi_update_lut(dev, lut_reset);
	if (ret != 0) {
		return ret;
	}

	ret = flash_flexspi_nand_xfer(dev, DYNAMIC_SEQ, kFLEXSPI_Command, 0, NULL, 0);
	if (ret != 0) {
		return ret;
	}

	/* W25N01GV tRST. */
	k_busy_wait(500);

	return ret;
}

static int flash_flexspi_nand_read_id(const struct device *dev, uint8_t id[3])
{
	const uint32_t lut_read_id[MEMC_FLEXSPI_CMD_PER_SEQ] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_READ_ID,
				kFLEXSPI_Command_DUMMY_SDR, kFLEXSPI_1PAD, 0x08),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR, kFLEXSPI_1PAD, 0x03,
				kFLEXSPI_Command_STOP, kFLEXSPI_1PAD, 0),
	};
	uint32_t value = 0;
	int ret;

	ret = flash_flexspi_update_lut(dev, lut_read_id);
	if (ret != 0) {
		return ret;
	}

	ret = flash_flexspi_nand_xfer(dev, DYNAMIC_SEQ, kFLEXSPI_Read, 0, &value, 3);
	if (ret != 0) {
		return ret;
	}

	id[0] = (uint8_t)(value & 0xFF);
	id[1] = (uint8_t)((value >> 8) & 0xFF);
	id[2] = (uint8_t)((value >> 16) & 0xFF);

	return 0;
}

static int flash_flexspi_nand_onfi_read(const struct device *dev)
{
	struct nand_onfi_parameter_page_raw onfi;
	uint16_t computed_crc;
	uint8_t cfg;
	uint8_t cfg_restore;
	struct flash_flexspi_nand_data *nand = dev->data;
	int ret;

	ret = flash_flexspi_nand_read_reg(dev, SPI_NAND_REG_CONFIG, &cfg);
	if (ret != 0) {
		return ret;
	}

	cfg_restore = cfg;

	/* Ensure on-die ECC is enabled, and enable OTP/parameter access. */
	cfg |= (SPI_NAND_CFG_ECC_EN | SPI_NAND_CFG_OTP_EN);
	ret = flash_flexspi_nand_write_reg(dev, SPI_NAND_REG_CONFIG, cfg);
	if (ret != 0) {
		return ret;
	}

	ret = flash_flexspi_nand_wait_busy(dev, FLASH_NAND_WAIT_BUSY_TIMEOUT_US);
	if (ret != 0) {
		return ret;
	}

	/* Load parameter page into cache.
	 * Many SPI-NAND devices expose the ONFI parameter page when OTP is enabled.
	 */
	ret = flash_flexspi_nand_xfer(dev, PAGE_READ, kFLEXSPI_Command, 2048, NULL, 0);
	if (ret != 0) {
		goto restore;
	}

	ret = flash_flexspi_nand_wait_busy(dev, FLASH_NAND_WAIT_BUSY_TIMEOUT_US);
	if (ret != 0) {
		goto restore;
	}

	memset(&onfi, 0, sizeof(onfi));
	ret = flash_flexspi_nand_xfer(dev, READ_CACHE, kFLEXSPI_Read, 2048, &onfi, sizeof(onfi));
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
	(void)flash_flexspi_nand_write_reg(dev, SPI_NAND_REG_CONFIG, (cfg_restore | (cfg & SPI_NAND_CFG_ECC_EN)) & ~SPI_NAND_CFG_OTP_EN);
	(void)flash_flexspi_nand_wait_busy(dev, FLASH_NAND_WAIT_BUSY_TIMEOUT_US);
	return ret;
}

static int flash_flexspi_nand_enable_ecc(const struct device *dev)
{
	struct flash_flexspi_nand_data *nand = dev->data;
	uint8_t cfg;
	int ret;

	ret = flash_flexspi_nand_read_reg(dev, SPI_NAND_REG_CONFIG, &cfg);
	if (ret != 0) {
		return ret;
	}

	if ((cfg & SPI_NAND_CFG_ECC_EN) != 0U) {
		return 0;
	}

	cfg |= SPI_NAND_CFG_ECC_EN;

	ret = flash_flexspi_nand_write_enable(dev);
	if (ret != 0) {
		return ret;
	}

	ret = flash_flexspi_nand_write_reg(dev, SPI_NAND_REG_CONFIG, cfg);
	if (ret != 0) {
		return ret;
	}

	ret = flash_flexspi_nand_wait_busy(dev, FLASH_NAND_WAIT_BUSY_TIMEOUT_US);
	if (ret != 0) {
		return ret;
	}

	memc_flexspi_reset(nand->controller);

	return 0;
}

static int flash_flexspi_nand_check_ecc(const struct device *dev)
{
	uint8_t status;
	int ret;

	ret = flash_flexspi_nand_read_reg(dev, SPI_NAND_REG_STATUS, &status);
	if (ret != 0) {
		return ret;
	}

	uint8_t ecc = (status & SPI_NAND_STATUS_ECC_MASK) >> SPI_NAND_STATUS_ECC_SHIFT;
	if (ecc == 0x3U) {
		LOG_ERR("Uncorrectable ECC error (STATUS=0x%02x)", status);
		return -EIO;
	}
	if (ecc != 0U) {
		LOG_WRN("ECC corrected bitflips (STATUS=0x%02x)", status);
	}

	return 0;
}

static int flash_flexspi_nand_read(const struct device *dev, off_t offset, void *data,
			      size_t len)
{
	struct flash_flexspi_nand_data *nand = dev->data;
	uint32_t off = (uint32_t)offset;
	uint8_t *buffer = data;
	size_t chunk;
	int ret;

	if ((data == NULL) || (!area_is_subregion(dev, offset, len))) {
		return -EINVAL;
	}

	while (len > 0U) {
		chunk = MIN(len, (size_t)(nand->page_size - (off % nand->page_size)));

		ret = flash_flexspi_nand_xfer(dev, PAGE_READ, kFLEXSPI_Command, off, NULL, 0);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_wait_busy(dev, FLASH_NAND_WAIT_BUSY_TIMEOUT_US);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_check_ecc(dev);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_xfer(dev, READ_CACHE, kFLEXSPI_Read, off, buffer, chunk);
		if (ret != 0) {
			return ret;
		}

		buffer += chunk;
		off += chunk;
		len -= chunk;
	}

	return 0;
}

static int flash_flexspi_nand_write(const struct device *dev, off_t offset, const void *data,
			       size_t len)
{
	struct flash_flexspi_nand_data *nand = dev->data;
	uint32_t off = (uint32_t)offset;
	const uint8_t *buffer = data;
	uint8_t status;
	size_t chunk;
	int ret;

	if ((data == NULL) || (!area_is_subregion(dev, offset, len))) {
		return -EINVAL;
	}

	while (len > 0U) {
		chunk = MIN(len, (size_t)(nand->page_size - (off % nand->page_size)));

		ret = flash_flexspi_nand_write_enable(dev);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_xfer(dev, PROG_LOAD, kFLEXSPI_Write, off, (void *)buffer, chunk);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_xfer(dev, PROG_EXEC, kFLEXSPI_Command, off, NULL, 0);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_wait_busy(dev, FLASH_NAND_WAIT_BUSY_TIMEOUT_US);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_read_reg(dev, SPI_NAND_REG_STATUS, &status);
		if (ret != 0) {
			return ret;
		}
		if ((status & SPI_NAND_STATUS_P_FAIL) != 0U) {
			return -EIO;
		}

		off += chunk;
		buffer += chunk;
		len -= chunk;
	}

	return 0;
}

static int flash_flexspi_nand_erase(const struct device *dev, off_t offset, size_t size)
{
	const uint32_t lut_block_erase[MEMC_FLEXSPI_CMD_PER_SEQ] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR, kFLEXSPI_1PAD, SPI_NAND_CMD_BLOCK_ERASE,
				kFLEXSPI_Command_RADDR_SDR, kFLEXSPI_1PAD, 0x18),
	};
	struct flash_flexspi_nand_data *nand = dev->data;
	uint32_t block_size = nand->block_size;
	uint32_t off = (uint32_t)offset;
	uint8_t status;
	int ret;

	if (!area_is_subregion(dev, offset, size)) {
		return -EINVAL;
	}

	while (size > 0U) {
		ret = flash_flexspi_nand_write_enable(dev);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_update_lut(dev, lut_block_erase);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_xfer(dev, DYNAMIC_SEQ, kFLEXSPI_Command, off, NULL, 0);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_wait_busy(dev, FLASH_NAND_WAIT_BUSY_TIMEOUT_US);
		if (ret != 0) {
			return ret;
		}

		ret = flash_flexspi_nand_read_reg(dev, SPI_NAND_REG_STATUS, &status);
		if (ret != 0) {
			return ret;
		}
		if ((status & SPI_NAND_STATUS_E_FAIL) != 0U) {
			return -EIO;
		}

		off += block_size;
		size -= block_size;
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

	/* Apply FlexSPI pinmux for the NAND port. */
	ret = memc_flexspi_apply_pinctrl(nand->controller, PINCTRL_STATE_DEFAULT);
	if (ret != 0 && ret != -ENOENT) {
		LOG_ERR("Failed to apply FlexSPI pinctrl (%d)", ret);
		return ret;
	}

	memc_flexspi_reset(nand->controller);

	ret = flash_flexspi_nand_reset(dev);
	if (ret != 0) {
		LOG_ERR("NAND reset failed (%d)", ret);
		return ret;
	}

	/* Clear all block protection bits for bring-up and flash_shell convenience. */
	ret = flash_flexspi_nand_set_block_protect(dev, 0x00);
	if (ret != 0) {
		LOG_WRN("Failed to clear block protect (%d)", ret);
	}

	ret = flash_flexspi_nand_read_id(dev, id);
	if (ret != 0) {
		LOG_ERR("NAND read-id failed (%d)", ret);
		return ret;
	}
	LOG_INF("JEDEC ID (raw): %02X %02X %02X", id[0], id[1], id[2]);

	ret = flash_flexspi_nand_enable_ecc(dev);
	if (ret != 0) {
		LOG_ERR("Failed to enable on-die ECC (%d)", ret);
		return ret;
	}

	ret = flash_flexspi_nand_onfi_read(dev);
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

#define CS_INTERVAL_UNIT(unit) UTIL_CAT(UTIL_CAT(kFLEXSPI_CsIntervalUnit, unit), SckCycle)
#define AHB_WRITE_WAIT_UNIT(unit) UTIL_CAT(UTIL_CAT(kFLEXSPI_AhbWriteWaitUnit, unit), AhbCycle)

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
			.write_block_size = 2048,                                             \
			.erase_value = 0xff,                                                             \
		},                                                                                       \
	};                                                                                          \
	DEVICE_DT_INST_DEFINE(n, flash_flexspi_nand_init, NULL, &flash_flexspi_nand_data_##n,        \
			      &flash_flexspi_nand_config_##n, POST_KERNEL, CONFIG_FLASH_MCUX_FLEXSPI_NAND_INIT_PRIORITY, \
			      &flash_flexspi_nand_api);

DT_INST_FOREACH_STATUS_OKAY(FLASH_FLEXSPI_NAND)
