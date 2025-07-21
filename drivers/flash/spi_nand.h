/*
 * Copyright (c) 2018 Savoir-Faire Linux.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SPI_NAND_H__
#define __SPI_NAND_H__

#include <zephyr/sys/util.h>

 /* Page and block size are standard, not configurable. */
 #define SPI_NAND_PAGE_SIZE    0x0800U
 #define SPI_NAND_BLOCK_SIZE   0x20000U

enum
{
    kSerialNand_EccCheckMask = 0x30,   // ECC Mask in status register
    kSerialNand_EccFailureMask = 0x20, // ECC failure Mask in status register

    kSerialNandStatus_BusyOffset           = 0,
    kSerialNandStatus_EraseFailureOffset   = 2,
    kSerialNandStatus_ProgramFailureOffset = 3,
};


#endif /*__SPI_NAND_H__*/
