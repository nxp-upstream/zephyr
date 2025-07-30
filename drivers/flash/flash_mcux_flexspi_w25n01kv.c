/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT	nxp_imx_flexspi_w25n01kv

#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include "spi_nand.h"
#include "memc_mcux_flexspi.h"

#ifdef CONFIG_HAS_MCUX_CACHE
#include <fsl_cache.h>
#endif

#define NAND_ERASE_VALUE	0xff

#ifdef CONFIG_FLASH_MCUX_FLEXSPI_NAND_PAGE_BUFFER
static uint8_t nand_page_buf[SPI_NAND_PAGE_SIZE];
#endif

#define NAND_FLASH_ENABLE_ECC_CMD 0x18

LOG_MODULE_REGISTER(flash_flexspi_nand, CONFIG_FLASH_LOG_LEVEL);

enum {
	/* Instructions */
	WRITE_ENABLE=1,
	READ_STATUS,
	READ_PAGE,
	READ_CACHE,
	PAGE_PROGRAM_LOAD,
	PAGE_PROGRAM_EXEC,
	ERASE_BLOCK,
	READ_ID,
	SET_FEATURE_PROT,
    SET_FEATURE_CFG,
};

/* Device variables used in critical sections should be in this structure */
struct flash_flexspi_nand_data {
	const struct device *controller;
	flexspi_device_config_t config;
	flexspi_port_t port;
	uint64_t size;
	struct flash_pages_layout layout;
	struct flash_parameters flash_parameters;
};

static const uint32_t flash_flexspi_nand_lut[][4] = {
	[WRITE_ENABLE] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x06,
				kFLEXSPI_Command_STOP,		kFLEXSPI_1PAD, 0),
	},

	[READ_STATUS] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x0F,
				kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0xC0),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR,	kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP,	        kFLEXSPI_1PAD, 0),
	},

	[READ_PAGE] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x13,
				kFLEXSPI_Command_RADDR_SDR,	kFLEXSPI_1PAD, 0x18),
	},

        // X4
	[READ_CACHE] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x6B,
				kFLEXSPI_Command_CADDR_SDR,	kFLEXSPI_1PAD, 0x10),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_DUMMY_SDR,	kFLEXSPI_4PAD, 0x08,
				kFLEXSPI_Command_READ_SDR,	kFLEXSPI_4PAD, 0x80),
	},

        // X4
	[PAGE_PROGRAM_LOAD] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x32,
				kFLEXSPI_Command_CADDR_SDR,	kFLEXSPI_1PAD, 0x10),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR,	kFLEXSPI_4PAD, 0x40,
				kFLEXSPI_Command_STOP,	        kFLEXSPI_1PAD, 0),
	},

	[PAGE_PROGRAM_EXEC] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x10,
				kFLEXSPI_Command_RADDR_SDR,	kFLEXSPI_1PAD, 0x18),
	},

	[ERASE_BLOCK] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0xD8,
				kFLEXSPI_Command_RADDR_SDR,	kFLEXSPI_1PAD, 0x18),
	},

	[READ_ID] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x9F,
				kFLEXSPI_Command_DUMMY_SDR,	kFLEXSPI_1PAD, 0x08),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_READ_SDR,	kFLEXSPI_1PAD, 0x02,
				kFLEXSPI_Command_STOP,	        kFLEXSPI_1PAD, 0),
	},

	[SET_FEATURE_PROT] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x1F,
				kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0xA0),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR,	kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP,	        kFLEXSPI_1PAD, 0),
	},

	[SET_FEATURE_CFG] = {
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0x1F,
				kFLEXSPI_Command_SDR,		kFLEXSPI_1PAD, 0xB0),
		FLEXSPI_LUT_SEQ(kFLEXSPI_Command_WRITE_SDR,	kFLEXSPI_1PAD, 0x01,
				kFLEXSPI_Command_STOP,	        kFLEXSPI_1PAD, 0),
	},

};

static int flash_flexspi_nand_get_vendor_id(const struct device *dev,
		uint16_t *vendor_id)
{
	struct flash_flexspi_nand_data *data = dev->data;
	uint32_t buffer = 0;
	int ret;

	flexspi_transfer_t transfer = {
		.deviceAddress = 0,
		.port = data->port,
		.cmdType = kFLEXSPI_Read,
		.SeqNumber = 1,
		.seqIndex = READ_ID,
		.data = &buffer,
		.dataSize = 2,
	};

	LOG_DBG("Reading id");

	ret = memc_flexspi_transfer(data->controller, &transfer);
	*vendor_id = buffer;

	return ret;
}

static int flash_flexspi_nand_set_prot(const struct device *dev,
		uint8_t prot)
{
	struct flash_flexspi_nand_data *data = dev->data;
    uint32_t buffer = prot;

	flexspi_transfer_t transfer = {
		.deviceAddress = 0,
		.port = data->port,
		.cmdType = kFLEXSPI_Write,
		.SeqNumber = 1,
		.seqIndex = SET_FEATURE_PROT,
		.data = &buffer,
		.dataSize = 1,
	};

	LOG_DBG("Setting protection register to 0x%x", prot);

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_set_cfg(const struct device *dev,
		uint8_t cfg)
{
	struct flash_flexspi_nand_data *data = dev->data;
    uint32_t buffer = cfg;

	flexspi_transfer_t transfer = {
		.deviceAddress = 0,
		.port = data->port,
		.cmdType = kFLEXSPI_Write,
		.SeqNumber = 1,
		.seqIndex = SET_FEATURE_CFG,
		.data = &buffer,
		.dataSize = 1,
	};

	LOG_DBG("Setting configuration register to 0x%x", cfg);

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_read_status(const struct device *dev,
		uint32_t *status)
{
	struct flash_flexspi_nand_data *data = dev->data;

	flexspi_transfer_t transfer = {
		.deviceAddress = 0,
		.port = data->port,
		.cmdType = kFLEXSPI_Read,
		.SeqNumber = 1,
		.seqIndex = READ_STATUS,
		.data = status,
		.dataSize = 1,
	};

	LOG_DBG("Reading status register");

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_write_enable(const struct device *dev)
{
	struct flash_flexspi_nand_data *data = dev->data;
	flexspi_transfer_t transfer;

	transfer.deviceAddress = 0;
	transfer.port = data->port;
	transfer.cmdType = kFLEXSPI_Command;
	transfer.SeqNumber = 1;
	transfer.seqIndex = WRITE_ENABLE;
	transfer.data = NULL;
	transfer.dataSize = 0;

	LOG_DBG("Enabling write");

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_erase_block(const struct device *dev,
		off_t offset)
{
	struct flash_flexspi_nand_data *data = dev->data;

	flexspi_transfer_t transfer = {
		.deviceAddress = offset,
		.port = data->port,
		.cmdType = kFLEXSPI_Command,
		.SeqNumber = 1,
		.seqIndex = ERASE_BLOCK,
		.data = NULL,
		.dataSize = 0,
	};

	LOG_DBG("Erasing block at 0x%08zx", (ssize_t) offset);

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_page_program_load(const struct device *dev,
		off_t offset, const void *buffer, size_t len)
{
	struct flash_flexspi_nand_data *data = dev->data;

	flexspi_transfer_t transfer = {
		.deviceAddress = offset,
		.port = data->port,
		.cmdType = kFLEXSPI_Write,
		.SeqNumber = 1,
		.seqIndex = PAGE_PROGRAM_LOAD,
		.data = (uint32_t *) buffer,
		.dataSize = len,
	};

	LOG_DBG("Programming page data %d bytes to 0x%08zx", len, (ssize_t) offset);

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_page_program_exec(const struct device *dev, off_t offset)
{
	struct flash_flexspi_nand_data *data = dev->data;
	flexspi_transfer_t transfer;

	transfer.deviceAddress = offset;
	transfer.port = data->port;
	transfer.cmdType = kFLEXSPI_Command;
	transfer.SeqNumber = 1;
	transfer.seqIndex = PAGE_PROGRAM_EXEC;
	transfer.data = NULL;
	transfer.dataSize = 0;

	LOG_DBG("Executing page program");

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_read_page(const struct device *dev,
		off_t offset)
{
	struct flash_flexspi_nand_data *data = dev->data;

	flexspi_transfer_t transfer = {
		.deviceAddress = offset,
		.port = data->port,
		.cmdType = kFLEXSPI_Command,
		.SeqNumber = 1,
		.seqIndex = READ_PAGE,
		.data = NULL,
		.dataSize = 0,
	};

	LOG_DBG("Executing read page");

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_read_cache(const struct device *dev,
		off_t offset, void *buffer, size_t len)
{
	struct flash_flexspi_nand_data *data = dev->data;

	flexspi_transfer_t transfer = {
		.deviceAddress = offset,
		.port = data->port,
		.cmdType = kFLEXSPI_Read,
		.SeqNumber = 1,
		.seqIndex = READ_CACHE,
		.data = (uint32_t *) buffer,
		.dataSize = len,
	};

	LOG_DBG("Reading page data %d bytes from 0x%08zx", len, (ssize_t) offset);

	return memc_flexspi_transfer(data->controller, &transfer);
}

static int flash_flexspi_nand_wait_bus_busy(const struct device *dev)
{
	uint32_t status = 0;
	int ret;

	do {
		ret = flash_flexspi_nand_read_status(dev, &status);
		LOG_DBG("status: 0x%x", status);
		if (ret) {
			LOG_ERR("Could not read status");
			return ret;
		}
	} while (status & BIT(kSerialNandStatus_BusyOffset));

	return 0;
}

static int flash_flexspi_nand_check_error(const struct device *dev)
{
	uint32_t status = 0;
	int ret;

    ret = flash_flexspi_nand_read_status(dev, &status);
    LOG_DBG("status: 0x%x", status);
    if (ret) {
        LOG_ERR("Could not read status");
        return ret;
    }
                
	if (status & BIT(kSerialNandStatus_EraseFailureOffset))
    {
        LOG_ERR("Meet erase failure");
        return EPERM;
    }
	if (status & BIT(kSerialNandStatus_ProgramFailureOffset))
    {
        LOG_ERR("Meet program failure");
        return EPERM;
    }

	return 0;
}

static int flash_flexspi_nand_check_ecc(const struct device *dev)
{
	uint32_t status = 0;
	int ret;

    ret = flash_flexspi_nand_read_status(dev, &status);
    LOG_DBG("status: 0x%x", status);
    if (ret) {
        LOG_ERR("Could not read status");
        return ret;
    }
                
	if ((status & kSerialNand_EccCheckMask) == kSerialNand_EccFailureMask)
    {
        LOG_ERR("Meet ECC error - Multiple bit flips");
        return EPERM;
    }

	return 0;
}

static int flash_flexspi_nand_enable_ecc(const struct device *dev)
{
	struct flash_flexspi_nand_data *data = dev->data;

	uint8_t value = 0;
	flash_flexspi_nand_write_enable(dev);
	flash_flexspi_nand_set_prot(dev, value);
	flash_flexspi_nand_wait_bus_busy(dev);

    value = NAND_FLASH_ENABLE_ECC_CMD;
	flash_flexspi_nand_write_enable(dev);
	flash_flexspi_nand_set_cfg(dev, value);
	flash_flexspi_nand_wait_bus_busy(dev);

	memc_flexspi_reset(data->controller);

	return 0;
}

static int flash_flexspi_nand_read(const struct device *dev, off_t offset,
		void *buffer, size_t len)
{
	struct flash_flexspi_nand_data *data = dev->data;
	uint8_t *src = (uint8_t *) buffer;
	int i;
    int ret;

	while (len) {
		/* If the offset isn't a multiple of the NAND page size, we first need
		 * to write the remaining part that fits, otherwise the read could
		 * be wrapped around within the same page
		 */
		i = MIN(SPI_NAND_PAGE_SIZE - (offset % SPI_NAND_PAGE_SIZE), len);
		ret = flash_flexspi_nand_read_page(dev, offset);
        if (ret) {
			LOG_ERR("Could not execute read");
			return ret;
		}
        ret = flash_flexspi_nand_wait_bus_busy(dev);
		if (ret) {
			return ret;
		}
		memc_flexspi_reset(data->controller);
        ret = flash_flexspi_nand_check_ecc(dev);
		if (ret) {
			return ret;
		}
#ifdef CONFIG_FLASH_MCUX_FLEXSPI_NAND_PAGE_BUFFER
		ret = flash_flexspi_nand_read_cache(dev, offset, nand_page_buf, i);
                memcpy(src, nand_page_buf, i);
#else
		ret = flash_flexspi_nand_read_cache(dev, offset, src, i);
#endif
        if (ret) {
			LOG_ERR("Could not read page data");
			return ret;
		}
		src += i;
		offset += i;
		len -= i;
	}

	return 0;
}

static int flash_flexspi_nand_write(const struct device *dev, off_t offset,
		const void *buffer, size_t len)
{
	struct flash_flexspi_nand_data *data = dev->data;
	uint8_t *src = (uint8_t *) buffer;
	int i;
    int ret;

	while (len) {
		/* If the offset isn't a multiple of the NAND page size, we first need
		 * to write the remaining part that fits, otherwise the write could
		 * be wrapped around within the same page
		 */
		i = MIN(SPI_NAND_PAGE_SIZE - (offset % SPI_NAND_PAGE_SIZE), len);
#ifdef CONFIG_FLASH_MCUX_FLEXSPI_NAND_PAGE_BUFFER
		memcpy(nand_page_buf, src, i);
#endif
		flash_flexspi_nand_write_enable(dev);
#ifdef CONFIG_FLASH_MCUX_FLEXSPI_NAND_PAGE_BUFFER
		ret = flash_flexspi_nand_page_program_load(dev, offset, nand_page_buf, i);
#else
		ret = flash_flexspi_nand_page_program_load(dev, offset, src, i);
#endif
        if (ret) {
			LOG_ERR("Could not program page data");
			return ret;
		}
		ret = flash_flexspi_nand_page_program_exec(dev, offset);
        if (ret) {
			LOG_ERR("Could not execute program");
			return ret;
		}
        ret = flash_flexspi_nand_wait_bus_busy(dev);
		if (ret) {
			return ret;
		}
        ret = flash_flexspi_nand_check_error(dev);
		if (ret) {
			return ret;
		}
        ret = flash_flexspi_nand_check_ecc(dev);
		if (ret) {
			return ret;
		}
		memc_flexspi_reset(data->controller);
		src += i;
		offset += i;
		len -= i;
	}

	return 0;
}

static int flash_flexspi_nand_erase(const struct device *dev, off_t offset,
		size_t size)
{
	struct flash_flexspi_nand_data *data = dev->data;
	int num_blocks = size / SPI_NAND_BLOCK_SIZE;
	int i;
    int ret;

	if (offset % SPI_NAND_BLOCK_SIZE) {
		LOG_ERR("Invalid offset");
		return -EINVAL;
	}

	if (size % SPI_NAND_BLOCK_SIZE) {
		LOG_ERR("Invalid size");
		return -EINVAL;
	}

	for (i = 0; i < num_blocks; i++) {
        flash_flexspi_nand_write_enable(dev);
        ret = flash_flexspi_nand_erase_block(dev, offset);
		if (ret) {
			LOG_ERR("Could not erase block");
			return ret;
		}
        ret = flash_flexspi_nand_wait_bus_busy(dev);
		if (ret) {
			return ret;
		}
        ret = flash_flexspi_nand_check_error(dev);
		if (ret) {
			return ret;
		}
        ret = flash_flexspi_nand_check_ecc(dev);
		if (ret) {
			return ret;
		}
        memc_flexspi_reset(data->controller);
        offset += SPI_NAND_BLOCK_SIZE;
    }

	return 0;
}

static const struct flash_parameters *flash_flexspi_nand_get_parameters(
		const struct device *dev)
{
	struct flash_flexspi_nand_data *data = dev->data;

	return &data->flash_parameters;
}

static int flash_flexspi_nand_get_size(const struct device *dev, uint64_t *size)
{
	const struct flash_flexspi_nand_data *data = dev->data;

	*size = data->size;

	return 0;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void flash_flexspi_nand_pages_layout(const struct device *dev,
		const struct flash_pages_layout **layout, size_t *layout_size)
{
	struct flash_flexspi_nand_data *data = dev->data;

	*layout = &data->layout;
	*layout_size = 1;
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

static int flash_flexspi_nand_init(const struct device *dev)
{
	struct flash_flexspi_nand_data *data = dev->data;
	uint16_t vendor_id;

	if (!device_is_ready(data->controller)) {
		LOG_ERR("Controller device not ready");
		return -ENODEV;
	}

	if (memc_flexspi_set_device_config(data->controller, &data->config,
	    (const uint32_t *)flash_flexspi_nand_lut,
	    sizeof(flash_flexspi_nand_lut) / MEMC_FLEXSPI_CMD_SIZE,
	    data->port)) {
		LOG_ERR("Could not set device configuration");
		return -EINVAL;
	}

	memc_flexspi_reset(data->controller);
        
    flash_flexspi_nand_enable_ecc(dev);

	if (flash_flexspi_nand_get_vendor_id(dev, &vendor_id)) {
		LOG_ERR("Could not read vendor id");
		return -EIO;
	}
	LOG_DBG("Vendor id: 0x%0x", vendor_id);

	return 0;
}

static DEVICE_API(flash, flash_flexspi_nand_api) = {
	.erase = flash_flexspi_nand_erase,
	.write = flash_flexspi_nand_write,
	.read = flash_flexspi_nand_read,
	.get_parameters = flash_flexspi_nand_get_parameters,
	.get_size = flash_flexspi_nand_get_size,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = flash_flexspi_nand_pages_layout,
#endif
};

#define CONCAT3(x, y, z) x ## y ## z

#define CS_INTERVAL_UNIT(unit)						\
	CONCAT3(kFLEXSPI_CsIntervalUnit, unit, SckCycle)

#define AHB_WRITE_WAIT_UNIT(unit)					\
	CONCAT3(kFLEXSPI_AhbWriteWaitUnit, unit, AhbCycle)

#define FLASH_FLEXSPI_DEVICE_CONFIG(n)					\
	{								\
		.flexspiRootClk = DT_INST_PROP(n, spi_max_frequency),	\
		.flashSize = DT_INST_PROP(n, size) / 8 / KB(1),		\
		.CSIntervalUnit =					\
			CS_INTERVAL_UNIT(				\
				DT_INST_PROP(n, cs_interval_unit)),	\
		.CSInterval = DT_INST_PROP(n, cs_interval),		\
		.CSHoldTime = DT_INST_PROP(n, cs_hold_time),		\
		.CSSetupTime = DT_INST_PROP(n, cs_setup_time),		\
		.dataValidTime = DT_INST_PROP(n, data_valid_time),	\
		.columnspace = DT_INST_PROP(n, column_space),		\
		.enableWordAddress = DT_INST_PROP(n, word_addressable),	\
		.AWRSeqIndex = 0,					\
		.AWRSeqNumber = 0,					\
		.ARDSeqIndex = 0,					\
		.ARDSeqNumber = 1,					\
		.AHBWriteWaitUnit =					\
			AHB_WRITE_WAIT_UNIT(				\
				DT_INST_PROP(n, ahb_write_wait_unit)),	\
		.AHBWriteWaitInterval =					\
			DT_INST_PROP(n, ahb_write_wait_interval),	\
	}								\

#define FLASH_FLEXSPI_NAND(n)						\
	static struct flash_flexspi_nand_data				\
		flash_flexspi_nand_data_##n = {				\
		.controller = DEVICE_DT_GET(DT_INST_BUS(n)),		\
		.config = FLASH_FLEXSPI_DEVICE_CONFIG(n),		\
		.port = DT_INST_REG_ADDR(n),				\
		.size = DT_INST_PROP(n, size) / 8,			\
		.layout = {						\
			.pages_count = DT_INST_PROP(n, size) / 8	\
				/ SPI_NAND_BLOCK_SIZE,			\
			.pages_size = SPI_NAND_BLOCK_SIZE,		\
		},							\
		.flash_parameters = {					\
			.write_block_size = SPI_NAND_PAGE_SIZE,		\
			.erase_value = NAND_ERASE_VALUE,			\
		},							\
	};								\
									\
	DEVICE_DT_INST_DEFINE(n,					\
			      flash_flexspi_nand_init,			\
			      NULL,					\
			      &flash_flexspi_nand_data_##n,		\
			      NULL,					\
			      POST_KERNEL,				\
			      CONFIG_FLASH_INIT_PRIORITY,		\
			      &flash_flexspi_nand_api);

DT_INST_FOREACH_STATUS_OKAY(FLASH_FLEXSPI_NAND)
