/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <sys/types.h>
#include <fsl_lpspi.h>

//!@brief SPI Memory transfer mode defintions
typedef enum _spi_mem_xfer_mode
{
    kSpiMem_Xfer_CommandOnly,      //!< Command Only
    kSpiMem_Xfer_CommandWriteData, //!< Command then Write Data
    kSpiMem_Xfer_CommandReadData,  //!< Comamdn then Read Data
} spi_mem_xfer_mode_t;

//!@brief SPI Memory Transfer Context
typedef struct __spi_mem_xfer
{
    uint8_t *cmd;             //!< Command buffer
    uint8_t *data;            //!< Data Buffer
    size_t cmdSize;           //!< Command buffer size
    size_t dataSize;          //!< Data buffer size
    spi_mem_xfer_mode_t mode; //!< Transfer mode
} spi_mem_xfer_t;

//!@brief Flash ID definition
typedef struct _flash_id
{
    uint8_t mid;    //!< Manufacturer Identifier
    uint8_t did[2]; //!< Device Identifier
    uint8_t reserved[17];
} flash_id_t;

typedef enum
{
    kSize_EraseSector = 0x1,
    kSize_EraseBlock  = 0x2,
    kSize_EraseChip   = 0x3,
} eraseOptions_t;


/**
 * @brief configure new LPSPI device
 *
 * Configures new device on the LPSPI bus.
 * @param dev: LPSPI device
 * @param baudrate: Baudrate of LPSPI device
 * @return 0 on success, negative value on failure
 */
int memc_lpspi_config(const struct device *dev, uint32_t baudrate);

/**
 * @brief Send blocking IP transfer
 *
 * Send blocking IP transfer using LPSPI.
 * @param dev: LPSPI device
 * @param xfer: LPSPI transfer context
 * @return 0 on success, negative value on failure
 */
int memc_lpspi_transfer(const struct device *dev, spi_mem_xfer_t *xfer);
