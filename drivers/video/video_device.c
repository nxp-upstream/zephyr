/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/video.h>

#include "video_device.h"

struct video_device *video_find_vdev(const struct device *dev)
{
	if (!dev) {
		return NULL;
	}

	STRUCT_SECTION_FOREACH(video_device, vdev) {
		if (vdev->dev == dev) {
			return vdev;
		}
	}

	return NULL;
}

const struct device *video_get_vdev(uint8_t ind)
{
	uint8_t i = 0;

	STRUCT_SECTION_FOREACH(video_device, vdev) {
		if (vdev->is_mdev && i++ == ind) {
			return vdev->dev;
		}
	}

	return NULL;
}

uint8_t video_get_vdevs_num(void)
{
	uint8_t count = 0;

	STRUCT_SECTION_FOREACH(video_device, vdev) {
		if (vdev->is_mdev) {
			count++;
		}
	}

	return count;
}
