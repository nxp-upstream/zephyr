/*
 * Copyright (c) 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/audio/spdif.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spdif_sample);

#define SPDIF_NODE DT_NODELABEL(spdif)

#define SAMPLE_RATE_HZ 48000U
#define FRAME_COUNT 64U
#define BLOCK_SIZE (SPDIF_FRAME_SIZE_BYTES * FRAME_COUNT)
#define BLOCK_COUNT 3U
#define TX_WAIT_MS 1000

K_MEM_SLAB_DEFINE_STATIC(spdif_tx_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *const spdif_dev = DEVICE_DT_GET(SPDIF_NODE);

static void fill_silence(void *buffer, size_t size)
{
	memset(buffer, 0, size);
}

static int wait_for_tx_reclaim(void)
{
	int waited_ms = 0;

	while (waited_ms < TX_WAIT_MS) {
		if (k_mem_slab_num_free_get(&spdif_tx_slab) == BLOCK_COUNT) {
			return 0;
		}

		k_msleep(20);
		waited_ms += 20;
	}

	return -ETIMEDOUT;
}

int main(void)
{
	struct spdif_config cfg = {
		.format = SPDIF_FRAME_FORMAT_PCM_24_STEREO,
		.sample_rate = SAMPLE_RATE_HZ,
		.mem_slab = &spdif_tx_slab,
		.block_size = BLOCK_SIZE,
		.timeout = 100,
		.options = SPDIF_OPT_TX_AUTOSYNC,
		.dpll_clk_source = 1,
		.tx_clk_source = 1,
		.rx_watermark = SPDIF_RX_WATERMARK_8,
		.tx_watermark = SPDIF_TX_WATERMARK_8,
		.gain = SPDIF_GAIN_8,
	};
	struct spdif_status status;
	void *block;
	int ret;

	LOG_INF("SPDIF sample");

	if (!device_is_ready(spdif_dev)) {
		LOG_ERR("SPDIF device %s is not ready", spdif_dev->name);
		return 0;
	}

	ret = spdif_channel_status_set(spdif_dev, 0U, 0U);
	if (ret != 0) {
		LOG_ERR("spdif_channel_status_set failed: %d", ret);
		return 0;
	}

	ret = spdif_configure(spdif_dev, SPDIF_DIR_TX, &cfg);
	if (ret != 0) {
		LOG_ERR("spdif_configure failed: %d", ret);
		return 0;
	}

	for (int index = 0; index < BLOCK_COUNT; index++) {
		ret = k_mem_slab_alloc(&spdif_tx_slab, &block, K_MSEC(100));
		if (ret != 0) {
			LOG_ERR("failed to allocate TX block %d: %d", index, ret);
			return 0;
		}

		fill_silence(block, BLOCK_SIZE);

		ret = spdif_write(spdif_dev, block, BLOCK_SIZE);
		if (ret != 0) {
			LOG_ERR("spdif_write failed for block %d: %d", index, ret);
			k_mem_slab_free(&spdif_tx_slab, block);
			return 0;
		}
	}

	ret = spdif_trigger(spdif_dev, SPDIF_DIR_TX, SPDIF_TRIGGER_START);
	if (ret != 0) {
		LOG_ERR("START trigger failed: %d", ret);
		return 0;
	}

	ret = wait_for_tx_reclaim();
	if (ret != 0) {
		LOG_WRN("timed out waiting for TX completion, forcing DROP");
		(void)spdif_trigger(spdif_dev, SPDIF_DIR_TX, SPDIF_TRIGGER_DROP);
	} else {
		LOG_INF("TX buffers reclaimed by SPDIF driver");
	}

	ret = spdif_status_get(spdif_dev, &status);
	if (ret != 0) {
		LOG_ERR("spdif_status_get failed: %d", ret);
		return 0;
	}

	LOG_INF("Status flags=0x%08x tx_cs=[0x%06x 0x%06x] rx_rate=%u",
		status.flags,
		status.tx_channel_status_low,
		status.tx_channel_status_high,
		status.rx_sample_rate);
	LOG_INF("Exiting");

	return 0;
}