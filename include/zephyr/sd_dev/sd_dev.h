/*
 * SPDX-FileCopyrightText: 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief SD Device Interface APIs
 *
 * This file provides the core APIs for SD/SDIO/MMC device-side drivers,
 * including initialization, enumeration, and memory management.
 */

#ifndef ZEPHYR_INCLUDE_SD_DEV_SD_DEV_H_
#define ZEPHYR_INCLUDE_SD_DEV_SD_DEV_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_SDIO_DEV
#include <zephyr/sd_dev/sdio_dev.h>
#endif

#ifdef CONFIG_SDMMC_DEV
#include <zephyr/sd_dev/sd_dev_mmc.h>
#endif

#include <zephyr/sd_dev/sdev_io.h>

/**
 * @brief SD Device APIs
 * @defgroup sd_dev_interface SD Device APIs
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief SD device card context
 *
 * This structure holds runtime state and configuration
 * information for an SD/SDIO/MMC device.
 */
struct sdev_card {
	/** Associated Zephyr device instance */
	const struct device *dev;

	/** Type of card (SD/SDIO/MMC) */
	sdev_card_type_t card_type;

	/** Current device state */
	atomic_t state;

	/** Whether the card has been enumerated */
	bool is_enum;

	/** Number of users referencing this device */
	atomic_t user_cnt;

	/** Signal used to notify state changes */
	struct k_poll_signal state_sig;

	/** Spinlock to protect concurrent access */
	struct k_spinlock lock;

#ifdef CONFIG_SDIO_DEV
	/** SDIO-specific device handle */
	struct sdio_dev *sdio;
#endif

#ifdef CONFIG_SDMMC_DEV
	/** MMC device structure */
	struct mmc_dev mmc;
#endif

	/** Vendor-specific private data */
	void *vendor_priv;
};

/**
 * @name SDEV API
 * @{
 */

/**
 * @brief Initialize SD device
 *
 * This function initializes an SD/SDIO/MMC device and allocates
 * the card context structure.
 *
 * @param dev Pointer to device instance
 * @param scard Pointer to store allocated card context
 *
 * @retval 0 on success
 * @retval -EINVAL if dev or scard is NULL
 * @retval -ENOMEM if memory allocation failed
 * @retval negative errno code on other failures
 */
int sdev_init(const struct device *dev, struct sdev_card **scard);

/**
 * @brief Deinitialize SDEV card device
 *
 * This function deinitializes the SDEV card device by cleaning up all
 * allocated resources, unregistering callbacks, and freeing memory.
 * It handles different card types (SD, SDIO, MMC) appropriately.
 *
 * The function will:
 * - Deinitialize card-type specific structures (SDIO/SD/MMC)
 * - Free all allocated memory
 * - Clear the card pointer
 *
 * @param dev Pointer to the device structure
 * @param card Pointer to the SDEV card structure pointer, will be set to NULL
 *
 * @retval 0 on success
 * @retval -EINVAL if dev or scard pointer is NULL or card device mismatch
 */
int sdev_deinit(const struct device *dev, struct sdev_card *card);

/**
 * @brief Check whether card enumeration is completed
 *
 * @param card Pointer to SD device card instance
 *
 * @retval true Card is enumerated
 * @retval false Card is not enumerated
 */
bool sdev_card_is_enumed(struct sdev_card *card);

/**
 * @brief Notify host that device is ready
 *
 * This function signals to the host controller that the device
 * has completed initialization and is ready for operation.
 *
 * @param card Pointer to SD device card instance
 */
void sdev_notify_host_ready(struct sdev_card *card);

/**
 * @}
 */

/**
 * @name SD Device Heap Management
 * @{
 */

/**
 * @brief Size of SD device heap buffer in bytes
 */
#define SDEV_HEAP_SIZE CONFIG_SDEV_HEAP_SIZE

/**
 * @brief Allocate memory from SD device heap
 *
 * This function allocates memory from a dedicated heap for SD device
 * operations. The heap is typically used for temporary buffers and
 * packet data.
 *
 * @param size Size in bytes to allocate
 *
 * @return Pointer to allocated memory, or NULL if allocation failed
 */
void *sdev_heap_alloc(size_t size);

/**
 * @brief Free memory back to SD device heap
 *
 * This function returns previously allocated memory back to the
 * SD device heap.
 *
 * @param ptr Pointer to memory to be freed
 */
void sdev_heap_free(void *ptr);

/** Data available for reading */
#define SDEV_POLLIN  BIT(0)
/** Ready for writing */
#define SDEV_POLLOUT BIT(1)
/** Error condition */
#define SDEV_POLLERR BIT(2)
/** Hang up */
#define SDEV_POLLHUP BIT(3)

/**
 * @}
 */

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_SD_DEV_SD_DEV_H_ */
