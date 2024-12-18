/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT	nxp_imx_lpspi_flash

#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include "spi_nor.h"
#include "memc_mcux_lpspi.h"
#include "jesd216.h"

#define NOR_WRITE_SIZE	1
#define NOR_ERASE_VALUE	0xff

LOG_MODULE_REGISTER(flash_lpspi_nor, CONFIG_FLASH_LOG_LEVEL);

typedef struct _lpspi_memory_config
{
    uint32_t bytesInPageSize;      /*!< Page size in byte of Serial NOR */
    uint32_t bytesInSectorSize;    /*!< Minimun Sector size in byte supported by Serial NOR */
    uint32_t bytesInMemorySize;    /*!< Memory size in byte of Serial NOR */
} lpspi_memory_config_t;

/* Device variables used in critical sections should be in this structure */
struct flash_lpspi_nor_data {
    const struct device *controller;
    lpspi_memory_config_t config;
    uint32_t flash_baudrate;
    struct flash_parameters flash_parameters;
    uint8_t readIdExpected[JESD216_READ_ID_LEN];
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
    struct flash_pages_layout layout;
#endif
};

enum
{
    kFlashCmd_ReadId          = 0x9F,
    kFlashCmd_ReadStatus      = 0x05,
    kFlashCmd_ReadMemory24Bit = 0x03,
    kFlashCmd_FastRead        = 0x0B,
    kFlashCmd_ReadSFDP        = 0x5A,

    kFlashCmd_WriteEnable     = 0x06,
    kFlashCmd_WriteDisable    = 0x04,
    kFlashCmd_PageProgram     = 0x02,

    kFlashCmd_EraseSector     = 0x20,
    kFlashCmd_EraseBlock      = 0xD8,
    kFlashCmd_EraseChip       = 0x60,
};

static int flash_lpspi_read(const struct device *dev, uint32_t addr, uint8_t *buffer, uint32_t lengthInBytes, bool isFastRead)
{
    struct flash_lpspi_nor_data *data = dev->data;

    uint8_t cmdBuffer[5];
    uint32_t cmdSize = 4u;

    if (isFastRead)
    {
        cmdBuffer[0] = kFlashCmd_FastRead;
        cmdSize      = 5u;
        cmdBuffer[4] = 0x00u; // DUMMY byte for fast read operation.
    }
    else
    {
        cmdBuffer[0] = kFlashCmd_ReadMemory24Bit;
    }

    uint32_t tmpAddr = addr;
    for (uint32_t i = 3u; i > 0u; i--)
    {
        cmdBuffer[i] = (uint8_t)(tmpAddr & 0xFFu);
        tmpAddr >>= 8u;
    }

    spi_mem_xfer_t spiMemXfer;
    spiMemXfer.cmd      = cmdBuffer;
    spiMemXfer.cmdSize  = cmdSize;
    spiMemXfer.data     = buffer;
    spiMemXfer.dataSize = lengthInBytes;
    spiMemXfer.mode     = kSpiMem_Xfer_CommandReadData;

    LOG_DBG("Read %d bytes from 0x%08x", lengthInBytes, addr);

    return memc_lpspi_transfer(data->controller, &spiMemXfer);
}

static int flash_lpspi_wait_busy(const struct device *dev)
{
    bool isBusy     = true;
    status_t status = kStatus_Fail;
    struct flash_lpspi_nor_data *data = dev->data;

    do
    {
        uint8_t cmdBuffer[] = {kFlashCmd_ReadStatus};
        uint8_t flashStatus = 0u;
        spi_mem_xfer_t spiMemXfer;
        spiMemXfer.cmd      = cmdBuffer;
        spiMemXfer.cmdSize  = sizeof(cmdBuffer);
        spiMemXfer.data     = &flashStatus;
        spiMemXfer.dataSize = 1u;
        spiMemXfer.mode     = kSpiMem_Xfer_CommandReadData;

        status = memc_lpspi_transfer(data->controller, &spiMemXfer);
        if (status != kStatus_Success)
        {
            LOG_ERR("Read status error: %d", status);
            return -EIO;
        }

        isBusy = (flashStatus & 1U) != 0U;
    } while (isBusy);

    return 0;
}

static int flash_lpspi_write_enable(const struct device *dev)
{
    struct flash_lpspi_nor_data *data = dev->data;

    uint8_t cmdBuffer[5];
    uint32_t cmdSize = 4u;

    cmdBuffer[0] = kFlashCmd_WriteEnable;
    cmdSize      = 1u;

    spi_mem_xfer_t spiMemXfer;
    spiMemXfer.cmd      = cmdBuffer;
    spiMemXfer.cmdSize  = cmdSize;
    spiMemXfer.data     = NULL;
    spiMemXfer.dataSize = 0U;
    spiMemXfer.mode     = kSpiMem_Xfer_CommandOnly;

    LOG_DBG("Enabling write");

    return memc_lpspi_transfer(data->controller, &spiMemXfer);
}

static int flash_lpspi_write_page(const struct device *dev, uint32_t addr, uint8_t *buffer, uint32_t lengthInBytes, bool blocking)
{
    int status = 0;
    struct flash_lpspi_nor_data *data = dev->data;

    LOG_DBG("Page programming %d bytes to 0x%08x", lengthInBytes, addr);

    do
    {
        if (lengthInBytes == 0u)
        {
            status = kStatus_Success;
            break;
        }

        uint8_t cmdBuffer[5];
        uint32_t cmdSize = 4u;

        if (flash_lpspi_write_enable(dev))
        {
            break;
        }

        cmdBuffer[0]     = kFlashCmd_PageProgram;
        uint32_t tmpAddr = addr;

        for (uint32_t i = 3u; i > 0u; i--)
        {
            cmdBuffer[i] = (uint8_t)(tmpAddr & 0xFFu);
            tmpAddr >>= 8u;
        }

        spi_mem_xfer_t spiMemXfer;
        spiMemXfer.cmd      = cmdBuffer;
        spiMemXfer.cmdSize  = cmdSize;
        spiMemXfer.data     = buffer;
        spiMemXfer.dataSize = lengthInBytes;
        spiMemXfer.mode     = kSpiMem_Xfer_CommandWriteData;

        status = memc_lpspi_transfer(data->controller, &spiMemXfer);
        if (status != kStatus_Success)
        {
            break;
        }

        if (true == blocking)
        {
            status = flash_lpspi_wait_busy(dev);
        }
    } while (false);

    return status;
}

static int flash_lpspi_erase(const struct device *dev, uint32_t addr, eraseOptions_t option, bool blocking)
{
    int status = 0;
    struct flash_lpspi_nor_data *data = dev->data;

    LOG_DBG("Erase flash");

    do
    {
        uint8_t cmdBuffer[5];
        uint32_t cmdSize = 4u;

        if (flash_lpspi_write_enable(dev))
        {
            break;
        }

        if (option == kSize_EraseChip)
        {
            cmdBuffer[0] = kFlashCmd_EraseChip;
            cmdSize      = 1u;
        }
        else
        {
            switch (option)
            {
                case kSize_EraseSector:
                    cmdBuffer[0] = kFlashCmd_EraseSector;
                    break;

                case kSize_EraseBlock:
                    cmdBuffer[0] = kFlashCmd_EraseBlock;
                    break;

                default:
                    status = kStatus_Fail;
                    break;
            }
            if (status != kStatus_Success)
            {
                break;
            }
            uint32_t tmpAddr = addr;
            for (uint32_t i = 3u; i > 0u; i--)
            {
                cmdBuffer[i] = (uint8_t)(tmpAddr & 0xFFu);
                tmpAddr >>= 8u;
            }
        }

        spi_mem_xfer_t spiMemXfer;
        spiMemXfer.cmd      = cmdBuffer;
        spiMemXfer.cmdSize  = cmdSize;
        spiMemXfer.data     = NULL;
        spiMemXfer.dataSize = 0U;
        spiMemXfer.mode     = kSpiMem_Xfer_CommandOnly;

        status = memc_lpspi_transfer(data->controller, &spiMemXfer);
        if (status != kStatus_Success)
        {
            break;
        }

        if (true == blocking)
        {
            status = flash_lpspi_wait_busy(dev);
        }
    } while (false);

    return status;
}

static int flash_lpspi_nor_read_id(const struct device *dev, uint8_t *id)
{
    status_t status;
    struct flash_lpspi_nor_data *data = dev->data;

    do
    {
        if (id == NULL)
        {
            status = kStatus_InvalidArgument;
            break;
        }

        uint8_t cmdBuffer[1] = {kFlashCmd_ReadId};
        spi_mem_xfer_t spiMemXfer;
        spiMemXfer.cmd      = cmdBuffer;
        spiMemXfer.cmdSize  = sizeof(cmdBuffer);
        spiMemXfer.data     = id;
        spiMemXfer.dataSize = JESD216_READ_ID_LEN;
        spiMemXfer.mode     = kSpiMem_Xfer_CommandReadData;
        status              = memc_lpspi_transfer(data->controller, &spiMemXfer);

        if (status != kStatus_Success)
        {
            break;
        }

        status = kStatus_Success;

    } while (false);

	if (status != kStatus_Success) {
		LOG_ERR("Read ID error: %d", status);
		return -ENODEV;
	}

    return 0;
}

static int flash_lpspi_nor_read(const struct device *dev, off_t offset,
		void *buffer, size_t len)
{
    int ret;

    ret = flash_lpspi_read(dev, (uint32_t) offset, (uint8_t *)buffer, (uint32_t) len, true);

    return ret;
}

static int flash_lpspi_nor_write(const struct device *dev, off_t offset,
		const void *buffer, size_t len)
{
    uint8_t *src = (uint8_t *) buffer;
    int i;

    while (len) {
        /* If the offset isn't a multiple of the NOR page size, we first need
         * to write the remaining part that fits, otherwise the write could
         * be wrapped around within the same page
         */
        i = MIN(SPI_NOR_PAGE_SIZE - (offset % SPI_NOR_PAGE_SIZE), len);

        flash_lpspi_write_page(dev, (uint32_t) offset, src, (uint32_t) i, true);

        src += i;
        offset += i;
        len -= i;
    }
    return 0;
}

static int flash_lpspi_nor_erase(const struct device *dev, off_t offset,
		size_t size)
{
    struct flash_lpspi_nor_data *data = dev->data;
    int num_sectors = size / SPI_NOR_SECTOR_SIZE;
    int num_blocks = size / SPI_NOR_BLOCK_SIZE;
    int i;

    if (offset % SPI_NOR_SECTOR_SIZE) {
        LOG_ERR("Invalid offset");
        return -EINVAL;
    }

    if (size % SPI_NOR_SECTOR_SIZE) {
        LOG_ERR("Invalid size");
        return -EINVAL;
    }

    if ((offset == 0) && (size == data->config.bytesInMemorySize)) {
        flash_lpspi_erase(dev, (uint32_t) offset, kSize_EraseChip, true);
    } else if ((0 == (offset % SPI_NOR_BLOCK_SIZE)) && (0 == (size % SPI_NOR_BLOCK_SIZE))) { 
        for (i = 0; i < num_blocks; i++) {
            flash_lpspi_erase(dev, (uint32_t) offset, kSize_EraseBlock, true);
            offset += SPI_NOR_BLOCK_SIZE;
        }
    } else {
        for (i = 0; i < num_sectors; i++) {
            flash_lpspi_erase(dev, (uint32_t) offset, kSize_EraseSector, true);
            offset += SPI_NOR_SECTOR_SIZE;
        }
    }

    return 0;
}

static int flash_lpspi_nor_init(const struct device *dev)
{
    struct flash_lpspi_nor_data *data = dev->data;
    int ret = 0;

    if (!device_is_ready(data->controller)) {
        LOG_ERR("Controller device not ready");
        return -ENODEV;
    }

    uint32_t baudRate  = data->flash_baudrate;
    if (memc_lpspi_config(data->controller, baudRate))
    {
        LOG_ERR("Flash init fail");
        return -EIO;
    }

    /* Verify connectivity by reading the device ID */
    LOG_DBG("Reading JEDEC ID");
    uint8_t jedec_id[JESD216_READ_ID_LEN];

    ret = flash_lpspi_nor_read_id(dev, jedec_id);
    if (ret != 0) {
        LOG_ERR("JEDEC ID read failed (%d)", ret);
        return -ENODEV;
    }

    /*
     * Check the memory device ID against the one configured from devicetree
     * to verify we are talking to the correct device.
     */
    if (memcmp(jedec_id, data->readIdExpected, sizeof(jedec_id)) != 0) {
        LOG_ERR("Device id %02x %02x %02x does not match config %02x %02x %02x",
            jedec_id[0], jedec_id[1], jedec_id[2], data->readIdExpected[0], data->readIdExpected[1], data->readIdExpected[2]);
        return -EINVAL;
    }

    return 0;
}

#if defined(CONFIG_FLASH_JESD216_API)
static int flash_lpspi_nor_sfdp_read(const struct device *dev, off_t offset,
				   void *data, size_t len)
{
    struct flash_lpspi_nor_data *flash_data = dev->data;

    uint8_t cmdBuffer[5];
    uint32_t cmdSize = 5u;
    cmdBuffer[0] = kFlashCmd_ReadSFDP;
    cmdBuffer[4] = 0x00u; // DUMMY byte.

    uint32_t tmpAddr = (uint32_t)offset;
    for (uint32_t i = 3u; i > 0u; i--)
    {
        cmdBuffer[i] = (uint8_t)(tmpAddr & 0xFFu);
        tmpAddr >>= 8u;
    }

    spi_mem_xfer_t spiMemXfer;
    spiMemXfer.cmd      = cmdBuffer;
    spiMemXfer.cmdSize  = cmdSize;
    spiMemXfer.data     = (uint8_t *)data;
    spiMemXfer.dataSize = len;
    spiMemXfer.mode     = kSpiMem_Xfer_CommandReadData;

    LOG_DBG("Read SFDP");

    return memc_lpspi_transfer(flash_data->controller, &spiMemXfer);
}
#endif

static const struct flash_parameters *flash_lpspi_nor_get_parameters(
		const struct device *dev)
{
    struct flash_lpspi_nor_data *data = dev->data;

    return &data->flash_parameters;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void flash_lpspi_nor_pages_layout(const struct device *dev,
		const struct flash_pages_layout **layout, size_t *layout_size)
{
    struct flash_lpspi_nor_data *data = dev->data;

    *layout = &data->layout;
    *layout_size = 1;
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

static const struct flash_driver_api flash_lpspi_nor_api = {
	.erase = flash_lpspi_nor_erase,
	.write = flash_lpspi_nor_write,
	.read = flash_lpspi_nor_read,
	.get_parameters = flash_lpspi_nor_get_parameters,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = flash_lpspi_nor_pages_layout,
#endif
#if defined(CONFIG_FLASH_JESD216_API)
	.sfdp_read = flash_lpspi_nor_sfdp_read,
	.read_jedec_id = flash_lpspi_nor_read_id,
#endif
};

#define FLASH_LPSPI_DEVICE_CONFIG(n)					\
    {								\
        .bytesInMemorySize = DT_INST_PROP(n, size) / 8,		\
        .bytesInPageSize = SPI_NOR_PAGE_SIZE,		\
        .bytesInSectorSize = SPI_NOR_SECTOR_SIZE,    \
    }								\

#define FLASH_LPSPI_NOR(n)						\
    BUILD_ASSERT(DT_INST_PROP_LEN(n, jedec_id) == JESD216_READ_ID_LEN,\
            "jedec-id must be of size JESD216_READ_ID_LEN bytes");  \
                                                                    \
    static struct flash_lpspi_nor_data				\
        flash_lpspi_nor_data_##n = {				\
        .controller = DEVICE_DT_GET(DT_INST_BUS(n)),		\
        .config = FLASH_LPSPI_DEVICE_CONFIG(n),		\
        .flash_baudrate = DT_INST_PROP(n, spi_max_frequency),  \
        .flash_parameters = {					\
            .write_block_size = NOR_WRITE_SIZE, \
            .erase_value = NOR_ERASE_VALUE,		\
        },                             \
        .readIdExpected = DT_INST_PROP(n, jedec_id),  \
        .layout = {						\
            .pages_count = DT_INST_PROP(n, size) / 8	\
                / SPI_NOR_SECTOR_SIZE,			\
            .pages_size = SPI_NOR_SECTOR_SIZE,		\
        },							\
    };								\
									\
    DEVICE_DT_INST_DEFINE(n,					\
                          flash_lpspi_nor_init,			\
                          NULL,					\
                          &flash_lpspi_nor_data_##n,		\
                          NULL,					\
                          POST_KERNEL,				\
                          CONFIG_FLASH_INIT_PRIORITY,		\
                          &flash_lpspi_nor_api);

DT_INST_FOREACH_STATUS_OKAY(FLASH_LPSPI_NOR)
