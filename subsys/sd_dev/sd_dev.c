/*
 * SPDX-FileCopyrightText: 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief SD Device Core Implementation
 *
 * This file implements the core SD device subsystem including heap
 * management, device initialization, and enumeration detection.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/sd_dev/sd_dev.h>
#include <zephyr/drivers/sdev.h>
#include <zephyr/sys/time_units.h>

LOG_MODULE_REGISTER(sd_dev, LOG_LEVEL_INF);

/**
 * @name SD Device Heap Management
 * @{
 */

/** SD device heap buffer */
static uint8_t sdev_heap_buf[SDEV_HEAP_SIZE];

/** SD device heap instance */
static struct k_heap sdev_heap;

/**
 * @brief Initialize SD device heap
 *
 * This function initializes the heap used for SD device allocations.
 */
static inline void sdev_heap_init(void)
{
	k_heap_init(&sdev_heap, sdev_heap_buf, sizeof(sdev_heap_buf));
}

void *sdev_heap_alloc(size_t size)
{
	return k_heap_alloc(&sdev_heap, size, K_NO_WAIT);
}

void sdev_heap_free(void *ptr)
{
	k_heap_free(&sdev_heap, ptr);
}

/**
 * @}
 */

/**
 * @name SD Device Notification
 * @{
 */

void sdev_notify_host_ready(struct sdev_card *card)
{
	const struct device *dev = card->dev;

	sdev_set_dev_ready(dev);
}

/**
 * @}
 */

/**
 * @name SD Device Enumeration
 * @{
 */

bool sdev_card_is_enumed(struct sdev_card *card)
{
#ifdef CONFIG_SDIO_DEV
	if (card->card_type == SDIO_DEVICE_CARD) {
		return sdio_dev_is_enumed(card->sdio);
	}
#endif
	return 0;
}

/**
 * @}
 */

/**
 * @name SD Device Initialization
 * @{
 */

int sdev_init(const struct device *dev, struct sdev_card **scard)
{
	int ret = 0;
	struct sdev_card *card = 0;
	const struct sdev_cfg *cfg = 0;

	if (!dev) {
		LOG_ERR("Invalid param: card=%p cfg=%p", card, cfg);
		return -EINVAL;
	}

	cfg = sdev_get_config(dev);

	sdev_heap_init();

	card = sdev_heap_alloc(sizeof(struct sdev_card));
	if (!card) {
		LOG_WRN("sdev card alloc memory fail");
		return -ENOMEM;
	}

	card->is_enum = false;
	card->dev = dev;
	card->card_type = cfg->card_type;

	switch (cfg->card_type) {
	case SD_DEVICE_CARD:
		LOG_INF("SD card init not implemented");
		break;
#ifdef CONFIG_SDIO_DEV
	case SDIO_DEVICE_CARD: {
		struct sdio_dev *sdio = 0;
		const struct sdio_dev_cfg *sdio_cfg = &cfg->sdio_cfg;

		if (!sdio_cfg) {
			LOG_ERR("sdio_cfg is NULL");
			return -EINVAL;
		}

		sdio = sdev_heap_alloc(sizeof(struct sdio_dev));
		if (!sdio) {
			LOG_ERR("sdio alloc failed");
			return -ENOMEM;
		}

		memset(sdio, 0, sizeof(*sdio));

		card->sdio = sdio;
		sdio->card = card;

		ret = sdio_dev_init(sdio, sdio_cfg);
		if (ret) {
			LOG_ERR("sdio_dev_init failed (%d)", ret);
			sdev_heap_free(sdio);
			card->sdio = NULL;
			return ret;
		}

		if (card->sdio == NULL) {
			LOG_ERR("%s: sdio is NULL after init", dev->name);
			return -EFAULT;
		}
		break;
	}
#endif /* CONFIG_SDIO_DEV */
	case MMC_DEVICE_CARD:
		LOG_ERR("MMC stack not enabled");
		return -ENOTSUP;

	default:
		LOG_ERR("Unknown SD card type: %d", cfg->card_type);
		return -EINVAL;
	}

	sdev_register_rx_cb(dev, sdev_rx_dispatch);

	sdev_notify_host_ready(card);

	*scard = card;

	return ret;
}

/**
 * @brief Deinitialize SDEV card device
 *
 * This function deinitializes the SDEV card device by cleaning up all
 * allocated resources, unregistering callbacks, and freeing memory based
 * on the card type (SD, SDIO, or MMC).
 *
 * @param dev Pointer to the device structure
 * @param card Pointer to the SDEV card structure pointer
 *
 * @retval 0 on success
 * @retval -EINVAL if dev or scard pointer is NULL
 */
int sdev_deinit(const struct device *dev, struct sdev_card *card)
{
	int ret = 0;

	if (!dev || !card) {
		LOG_ERR("Invalid param: dev=%p card=%p", dev, card);
		return -EINVAL;
	}

	/* Verify card belongs to this device */
	if (card->dev != dev) {
		LOG_ERR("Card device mismatch");
		return -EINVAL;
	}

	/* Deinitialize based on card type */
	switch (card->card_type) {
	case SD_DEVICE_CARD:
		LOG_INF("SD card deinit not implemented");
		break;

#ifdef CONFIG_SDIO_DEV
	case SDIO_DEVICE_CARD: {
		struct sdio_dev *sdio = card->sdio;

		if (!sdio) {
			LOG_WRN("SDIO device already NULL");
			break;
		}

		/* Deinitialize SDIO device */
		ret = sdio_dev_deinit(sdio);
		if (ret) {
			LOG_ERR("sdio_dev_deinit failed (%d)", ret);
			/* Continue cleanup despite error */
		}

		/* Free SDIO structure */
		sdev_heap_free(sdio);
		card->sdio = NULL;
		break;
	}
#endif /* CONFIG_SDIO_DEV */

	case MMC_DEVICE_CARD:
		LOG_INF("MMC card deinit not implemented");
		break;

	default:
		LOG_WRN("Unknown SD card type: %d", card->card_type);
		break;
	}

	/* Clear card state */
	card->is_enum = false;
	card->dev = NULL;

	/* Free card structure */
	sdev_heap_free(card);

	LOG_INF("%s: SDIO software deinit done", dev->name);

	return ret;
}

/**
 * @}
 */
