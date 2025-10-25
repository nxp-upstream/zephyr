/*
 * Copied from eeprom sample and modified to use nvmem API
 *
 * Copyright (c) 2021 Thomas Stranger
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/nvmem.h>
#include <zephyr/device.h>

#define NVMEM_SAMPLE_OFFSET 0
#define NVMEM_SAMPLE_MAGIC  0xEE9703

struct perisistant_values {
	uint32_t magic;
	uint32_t boot_count;
};

/*
 * Get a device structure from a devicetree node with alias nvmem-0
 */
static const struct device *get_nvmem_device(void)
{
	const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(nvmem_0));

	if (!device_is_ready(dev)) {
		printk("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       dev->name);
		return NULL;
	}

	printk("Found NVMEM device \"%s\"\n", dev->name);
	return dev;
}

int main(void)
{
	const struct device *nvmem = get_nvmem_device();
	size_t nvmem_size;
	struct perisistant_values values;
	int rc;

	if (nvmem == NULL) {
		return 0;
	}

	nvmem_size = nvmem_get_size(nvmem);
	printk("Using nvmem with size of: %zu.\n", nvmem_size);

	rc = nvmem_read(nvmem, NVMEM_SAMPLE_OFFSET, &values, sizeof(values));
	if (rc < 0) {
		printk("Error: Couldn't read nvmem: err: %d.\n", rc);
		return 0;
	}

	if (values.magic != NVMEM_SAMPLE_MAGIC) {
		values.magic = NVMEM_SAMPLE_MAGIC;
		values.boot_count = 0;
	}

	values.boot_count++;
	printk("Device booted %d times.\n", values.boot_count);

	rc = nvmem_write(nvmem, NVMEM_SAMPLE_OFFSET, &values, sizeof(values));
	if (rc < 0) {
		printk("Error: Couldn't write nvmem: err:%d.\n", rc);
		return 0;
	}

	printk("Reset the MCU to see the increasing boot counter.\n\n");
	return 0;
}
