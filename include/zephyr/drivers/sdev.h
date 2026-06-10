/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief SDIO device interface
 *
 * This file provides the API for SDIO device-side drivers, including
 * register access, configuration, and data transfer operations.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SDEV_H_
#define ZEPHYR_INCLUDE_DRIVERS_SDEV_H_

#include <zephyr/device.h>
#include <zephyr/syscall.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/sd_dev/sdev_pkt.h>

/**
 * @brief SDIO Device Driver APIs
 * @defgroup sdev_interface SDIO Device Driver APIs
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief Supported SD device card types
 *
 * This enum defines the different types of cards supported
 * by the SD device interface.
 */
typedef enum {
	/** Standard SD memory card */
	SD_DEVICE_CARD = 0,
	/** SDIO (I/O) card */
	SDIO_DEVICE_CARD,
	/** MultiMediaCard (MMC) */
	MMC_DEVICE_CARD,
} sdev_card_type_t;

/**
 * @brief SD device state
 *
 * This enum defines the lifecycle states of an SDIO device.
 */
enum sdev_state {
	/** Device is in reset state */
	SDEV_DEVICE_RESET = 0,
	/** Device is initializing */
	SDEV_DEVICE_INIT,
	/** Device initialization is complete */
	SDEV_DEVICE_INIT_DONE,
	/** Device is ready for operation */
	SDEV_DEVICE_READY,
	/** Device is suspended */
	SDEV_DEVICE_SUSPEND,
	/** Device encountered an error */
	SDEV_DEVICE_ERROR,
};

/**
 * @brief SDEV RX callback type
 *
 * This callback is invoked when new data is received from the SDEV
 * transport layer (e.g. SDIO, SPI, etc.).
 *
 * @param dev Device pointer associated with the transport
 * @param pkt Pointer to received packet
 */
typedef void (*sdev_recv_pkt)(const struct device *dev,
			      sdev_pkt_t *pkt);

#ifdef CONFIG_SDIO_DEV
/**
 * @brief SDIO function configuration
 *
 * Configuration for a single SDIO function.
 */
struct sdio_func_cfg {
	/** Function number (1-7) */
	uint8_t fn;
	/** Function Basic Register (FBR) address */
	uint32_t fbr_addr;
	/** Function code (type identifier) */
	uint8_t fn_code;
	/** Block size for block transfers */
	uint16_t block_size;
};



/**
 * @brief SDIO device configuration
 *
 * Complete configuration for an SDIO device including CCCR and functions.
 */
struct sdio_dev_cfg {
	/** Card Common Control Registers (CCCR) address */
	uint32_t cccr_addr;
	/** Bitmap of enabled functions */
	uint8_t func_bitmap;
	/** Array of function configurations */
	struct sdio_func_cfg funcs[CONFIG_SDIO_DEV_MAX_FUNCS];
	/** CCCR version (0-15) */
	int8_t cccr_version;
	/** SDIO specification version (0-15) */
	int8_t sdio_version;
	/** SD specification version (0-15) */
	int8_t spec_version;
	/** Bus width (1, 4, or 8 bits) */
	int8_t bus_width;
	/** CSPI interrupt mode */
	bool cspi_int;
	/** Multi-block support */
	bool multi_block;
	/** Low-speed card */
	bool low_speed;
	/** High-speed support */
	int8_t high_speed;
	/** UHS mode (0-7) */
	int8_t uhs_mode;
	/** Bus speed */
	int8_t bus_speed;
	/** Drive strength (0-7) */
	int8_t strength;
	/** Asynchronous interrupt support */
	bool async_intr_support;
	/** function code */
	int fn_code;
};

#endif /* CONFIG_SDIO_DEV */

/**
 * @brief SD device configuration structure
 *
 * Top-level configuration for SD/SDIO/MMC devices.
 */
struct sdev_cfg {
	/** Card type of the SD device */
	int card_type;
#ifdef CONFIG_SDIO_DEV
	/** SDIO device configuration */
	struct sdio_dev_cfg sdio_cfg;
#endif
#ifdef CONFIG_SDMMC_DEV
	/** SDMMC device configuration */
	struct sdio_dev_cfg mmc_cfg;
#endif
};

/**
 * @brief SDIO device driver API
 *
 * This structure defines the interface that must be implemented
 * by SDIO device drivers.
 */
__subsystem struct sdev_driver_api {
	/**
	 * @brief Get SDIO device configuration
	 *
	 * @param dev Pointer to the device structure
	 *
	 * @return Pointer to constant configuration structure, or NULL on error
	 */
	const struct sdev_cfg *(*get_config)(const struct device *dev);

	/**
	 * @brief Read 8-bit register
	 *
	 * @param dev Device instance
	 * @param addr Register address
	 * @param val Pointer to store value
	 *
	 * @retval 0 on success
	 * @retval -EINVAL if val is NULL
	 * @retval -ENOTSUP if operation is not supported
	 * @retval negative errno code on failure
	 */
	int (*read_reg8)(const struct device *dev, uint32_t addr, uint8_t *val);

	/**
	 * @brief Read 32-bit register
	 *
	 * @param dev Device instance
	 * @param addr Register address (must be 4-byte aligned)
	 * @param val Pointer to store value
	 *
	 * @retval 0 on success
	 * @retval -EINVAL if val is NULL or addr is not aligned
	 * @retval -ENOTSUP if operation is not supported
	 * @retval negative errno code on failure
	 */
	int (*read_reg32)(const struct device *dev, uint32_t addr, uint32_t *val);

	/**
	 * @brief Write 8-bit register
	 *
	 * @param dev Device instance
	 * @param addr Register address
	 * @param val Value to write
	 *
	 * @retval 0 on success
	 * @retval -ENOTSUP if operation is not supported
	 * @retval negative errno code on failure
	 */
	int (*write_reg8)(const struct device *dev, uint32_t addr, uint8_t val);

	/**
	 * @brief Write 32-bit register
	 *
	 * @param dev Device instance
	 * @param addr Register address (must be 4-byte aligned)
	 * @param val Value to write
	 *
	 * @retval 0 on success
	 * @retval -EINVAL if addr is not aligned
	 * @retval -ENOTSUP if operation is not supported
	 * @retval negative errno code on failure
	 */
	int (*write_reg32)(const struct device *dev, uint32_t addr, uint32_t val);

	/**
	 * @brief Configure CIS tuple
	 *
	 * @param dev Device instance
	 *
	 * @retval 0 on success
	 * @retval -ENOTSUP if operation is not supported
	 * @retval negative errno code on failure
	 */
	int (*cis_tuple_configurate)(const struct device *dev);

	/**
	 * @brief Set device ready status
	 *
	 * @param dev Device instance
	 *
	 * @retval 0 on success
	 * @retval -ENOTSUP if operation is not supported
	 * @retval negative errno code on failure
	 */
	int (*set_dev_ready)(const struct device *dev);

	/**
	 * @brief Send data through SDIO
	 *
	 * @param dev Device instance
	 * @param pkt Pointer to packet
	 *
	 * @retval 0 on success
	 * @retval -EINVAL if pkt is NULL
	 * @retval -ENOTSUP if operation is not supported
	 * @retval negative errno code on failure
	 */
	int (*send_data)(const struct device *dev, sdev_pkt_t *pkt);

	/**
	 * @brief Set device state
	 *
	 * @param dev Device instance
	 * @param state State value to set
	 */
	void (*set_state)(const struct device *dev, uint32_t state);

	/**
	 * @brief Get device state
	 *
	 * @param dev Device instance
	 *
	 * @retval Positive or zero state value on success
	 * @retval -ENOTSUP if operation is not supported
	 * @retval negative errno code on failure
	 */
	int (*get_state)(const struct device *dev);

	/**
	 * @brief Register RX callback
	 *
	 * Called by upper layers to register a data reception handler.
	 *
	 * @param dev Device instance
	 * @param cb Callback function
	 *
	 * @retval 0 on success
	 * @retval -EINVAL if input is invalid
	 */
	int (*register_rx_cb)(const struct device *dev,
			      sdev_recv_pkt cb);
};

/**
 * @brief Get SDIO device configuration
 *
 * This function retrieves the SDIO device configuration including
 * CCCR and FBR information. The returned pointer is valid for the
 * lifetime of the device.
 *
 * @param dev Pointer to the device structure
 *
 * @return Pointer to constant configuration structure, or NULL on error
 */
__syscall const struct sdev_cfg *sdev_get_config(const struct device *dev);

/**
 * @brief Read 8-bit register value from device
 *
 * @param dev Pointer to device instance
 * @param addr Register address
 * @param val Output pointer for value
 *
 * @retval 0 on success
 * @retval -EINVAL if val is NULL
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
__syscall int sdev_read_reg8(const struct device *dev, uint32_t addr, uint8_t *val);

/**
 * @brief Read 32-bit register value from device
 *
 * @param dev Pointer to device instance
 * @param addr Register address (must be 4-byte aligned)
 * @param val Output pointer for value
 *
 * @retval 0 on success
 * @retval -EINVAL if val is NULL or addr is not aligned
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
__syscall int sdev_read_reg32(const struct device *dev, uint32_t addr, uint32_t *val);

/**
 * @brief Write 8-bit value to device register
 *
 * @param dev Pointer to device instance
 * @param addr Register address
 * @param val Value to write
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
__syscall int sdev_write_reg8(const struct device *dev, uint32_t addr, uint8_t val);

/**
 * @brief Write 32-bit value to device register
 *
 * @param dev Pointer to device instance
 * @param addr Register address (must be 4-byte aligned)
 * @param val Value to write
 *
 * @retval 0 on success
 * @retval -EINVAL if addr is not aligned
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
__syscall int sdev_write_reg32(const struct device *dev, uint32_t addr, uint32_t val);

/**
 * @brief Configure CIS table for the device
 *
 * @param dev Pointer to device instance
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
__syscall int sdev_cis_table_configurate(const struct device *dev);

/**
 * @brief Set device to ready state
 *
 * @param dev Pointer to device instance
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
__syscall int sdev_set_dev_ready(const struct device *dev);

/**
 * @brief Send data via SDIO interface
 *
 * @param dev Pointer to device instance
 * @param pkt Pointer to packet data
 *
 * @retval 0 on success
 * @retval -EINVAL if pkt is NULL
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
__syscall int sdio_send_data(const struct device *dev, sdev_pkt_t *pkt);

/**
 * @brief Set device state
 *
 * This function sets the operational state of the device.
 *
 * @param dev Pointer to device instance
 * @param state State value to set (see enum sdev_state)
 */
__syscall void sdev_set_state(const struct device *dev, uint32_t state);

/**
 * @brief Get device state
 *
 * This function retrieves the current operational state of the device.
 *
 * @param dev Pointer to device instance
 *
 * @retval Positive or zero state value on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
__syscall int sdev_get_state(const struct device *dev);

/**
 * @brief Register RX callback for SDEV device
 *
 * This API allows upper layer subsystems to register a callback
 * that will be invoked when data is received from the SDEV driver.
 *
 * @note This API is not thread-safe. Caller must ensure proper synchronization.
 *
 * @param dev SDEV device instance
 * @param cb Callback function
 *
 * @retval 0 on success
 * @retval -EINVAL if parameters are invalid
 * @retval -ENOSYS if driver does not implement this API
 */
__syscall int sdev_register_rx_cb(const struct device *dev,
				  sdev_recv_pkt cb);

/**
 * @brief Implementation of sdev_get_config
 *
 * @param dev Pointer to device instance
 *
 * @return Pointer to configuration structure, or NULL on error
 */
static inline const struct sdev_cfg *z_impl_sdev_get_config(const struct device *dev)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api == NULL || api->get_config == NULL) {
		return NULL;
	}

	return api->get_config(dev);
}

/**
 * @brief Implementation of sdev_read_reg8
 *
 * @param dev Pointer to device instance
 * @param addr Register address
 * @param val Output pointer for value
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
static inline int z_impl_sdev_read_reg8(const struct device *dev, uint32_t addr, uint8_t *val)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->read_reg8 == NULL) {
		return -ENOTSUP;
	}

	return api->read_reg8(dev, addr, val);
}

/**
 * @brief Implementation of sdev_read_reg32
 *
 * @param dev Pointer to device instance
 * @param addr Register address
 * @param val Output pointer for value
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
static inline int z_impl_sdev_read_reg32(const struct device *dev, uint32_t addr, uint32_t *val)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->read_reg32 == NULL) {
		return -ENOTSUP;
	}

	return api->read_reg32(dev, addr, val);
}

/**
 * @brief Implementation of sdev_write_reg8
 *
 * @param dev Pointer to device instance
 * @param addr Register address
 * @param val Value to write
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
static inline int z_impl_sdev_write_reg8(const struct device *dev, uint32_t addr, uint8_t val)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->write_reg8 == NULL) {
		return -ENOTSUP;
	}

	return api->write_reg8(dev, addr, val);
}

/**
 * @brief Implementation of sdev_write_reg32
 *
 * @param dev Pointer to device instance
 * @param addr Register address
 * @param val Value to write
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
static inline int z_impl_sdev_write_reg32(const struct device *dev, uint32_t addr, uint32_t val)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->write_reg32 == NULL) {
		return -ENOTSUP;
	}

	return api->write_reg32(dev, addr, val);
}

/**
 * @brief Implementation of sdev_cis_table_configurate
 *
 * @param dev Pointer to device instance
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
static inline int z_impl_sdev_cis_table_configurate(const struct device *dev)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->cis_tuple_configurate == NULL) {
		return -ENOTSUP;
	}

	return api->cis_tuple_configurate(dev);
}

/**
 * @brief Implementation of sdev_set_dev_ready
 *
 * @param dev Pointer to device instance
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
static inline int z_impl_sdev_set_dev_ready(const struct device *dev)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->set_dev_ready == NULL) {
		return -ENOTSUP;
	}

	return api->set_dev_ready(dev);
}

/**
 * @brief Implementation of sdio_send_data
 *
 * @param dev Pointer to device instance
 * @param pkt Pointer to packet data
 *
 * @retval 0 on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
static inline int z_impl_sdio_send_data(const struct device *dev, sdev_pkt_t *pkt)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->send_data == NULL) {
		return -ENOTSUP;
	}

	return api->send_data(dev, pkt);
}

/**
 * @brief Implementation of sdev_set_state
 *
 * @param dev Pointer to device instance
 * @param state State value to set
 */
static inline void z_impl_sdev_set_state(const struct device *dev, uint32_t state)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->set_state == NULL) {
		return;
	}

	api->set_state(dev, state);
}

/**
 * @brief Implementation of sdev_get_state
 *
 * @param dev Pointer to device instance
 *
 * @retval Device state value on success
 * @retval -ENOTSUP if operation is not supported
 * @retval negative errno code on failure
 */
static inline int z_impl_sdev_get_state(const struct device *dev)
{
	const struct sdev_driver_api *api = (const struct sdev_driver_api *)dev->api;

	if (api->get_state == NULL) {
		return -ENOTSUP;
	}

	return api->get_state(dev);
}

/**
 * @brief Inline implementation of sdev_register_rx_cb()
 *
 * This function dispatches the call to the underlying driver API.
 *
 * @param dev SDEV device instance
 * @param cb Callback function
 *
 * @retval 0 on success
 * @retval -EINVAL if parameters are invalid
 * @retval -ENOSYS if driver API is not implemented
 */
static inline int z_impl_sdev_register_rx_cb(const struct device *dev,
					     sdev_recv_pkt cb)
{
	const struct sdev_driver_api *api =
		(const struct sdev_driver_api *)dev->api;

	if ((dev == NULL) || (cb == NULL)) {
		return -EINVAL;
	}

	if ((api == NULL) || (api->register_rx_cb == NULL)) {
		return -ENOSYS;
	}

	return api->register_rx_cb(dev, cb);
}

/**
 * @}
 */

#include <zephyr/syscalls/sdev.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_SDEV_H_ */
