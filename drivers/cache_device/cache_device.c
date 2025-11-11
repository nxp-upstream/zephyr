/*
 * Generic cache device dispatcher
 * Iterates Devicetree instances of "zephyr,cache-device" and calls
 * per-device vtable operations.
 */

#define DT_DRV_COMPAT zephyr_cache_device

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/cache_device.h>
#include <errno.h>

static inline void merge_first_err(int rc, int *handled, int *first_err)
{
	if (rc == 0) {
		*handled = 1;
	} else if (*first_err == 0) {
		*first_err = rc;
	}
}

int cache_device_flush_range(void *addr, size_t size)
{
	if (addr == NULL || size == 0U) {
		return -EINVAL;
	}

	int handled = 0;
	int first_err = 0;

#define TRY_ONE(inst) do { \
		const struct device *dev = DEVICE_DT_INST_GET(inst); \
		if (!device_is_ready(dev)) break; \
		const struct cache_device_driver_api *api = \
			(const struct cache_device_driver_api *)dev->api; \
		if (!api || !api->flush_range) break; \
		int rc = api->flush_range(dev, addr, size); \
		if (rc == -ERANGE) break; \
		merge_first_err(rc, &handled, &first_err); \
	} while (0)

	DT_INST_FOREACH_STATUS_OKAY(TRY_ONE);
#undef TRY_ONE

	return handled ? 0 : (first_err ? first_err : -ENOTSUP);
}

int cache_device_invalidate_range(void *addr, size_t size)
{
	if (addr == NULL || size == 0U) {
		return -EINVAL;
	}

	int handled = 0;
	int first_err = 0;

#define TRY_ONE(inst) do { \
		const struct device *dev = DEVICE_DT_INST_GET(inst); \
		if (!device_is_ready(dev)) break; \
		const struct cache_device_driver_api *api = \
			(const struct cache_device_driver_api *)dev->api; \
		if (!api || !api->invalidate_range) break; \
		int rc = api->invalidate_range(dev, addr, size); \
		if (rc == -ERANGE) break; \
		merge_first_err(rc, &handled, &first_err); \
	} while (0)

	DT_INST_FOREACH_STATUS_OKAY(TRY_ONE);
#undef TRY_ONE

	return handled ? 0 : (first_err ? first_err : -ENOTSUP);
}

int cache_device_flush_and_invalidate_range(void *addr, size_t size)
{
	if (addr == NULL || size == 0U) {
		return -EINVAL;
	}

	int handled = 0;
	int first_err = 0;

#define TRY_ONE(inst) do { \
		const struct device *dev = DEVICE_DT_INST_GET(inst); \
		if (!device_is_ready(dev)) break; \
		const struct cache_device_driver_api *api = \
			(const struct cache_device_driver_api *)dev->api; \
		if (!api || !api->flush_and_invalidate_range) break; \
		int rc = api->flush_and_invalidate_range(dev, addr, size); \
		if (rc == -ERANGE) break; \
		merge_first_err(rc, &handled, &first_err); \
	} while (0)

	DT_INST_FOREACH_STATUS_OKAY(TRY_ONE);
#undef TRY_ONE

	return handled ? 0 : (first_err ? first_err : -ENOTSUP);
}

int cache_device_data_flush_all(void)
{
	int rc = 0;

#define DO_ONE(inst) do { \
		const struct device *dev = DEVICE_DT_INST_GET(inst); \
		if (!device_is_ready(dev)) break; \
		rc |= cache_device_flush_all(dev); \
	} while (0)

	DT_INST_FOREACH_STATUS_OKAY(DO_ONE);
#undef DO_ONE

	return rc;
}

int cache_device_data_invalidate_all(void)
{
	int rc = 0;

#define DO_ONE(inst) do { \
		const struct device *dev = DEVICE_DT_INST_GET(inst); \
		if (!device_is_ready(dev)) break; \
		rc |= cache_device_invalidate_all(dev); \
	} while (0)

	DT_INST_FOREACH_STATUS_OKAY(DO_ONE);
#undef DO_ONE

	return rc;
}

int cache_device_data_flush_and_invalidate_all(void)
{
	int rc = 0;

#define DO_ONE(inst) do { \
		const struct device *dev = DEVICE_DT_INST_GET(inst); \
		if (!device_is_ready(dev)) break; \
		rc |= cache_device_flush_and_invalidate_all(dev); \
	} while (0)

	DT_INST_FOREACH_STATUS_OKAY(DO_ONE);
#undef DO_ONE

	return rc;
}

int cache_device_instr_flush_all(void)
{
	return cache_device_data_flush_all();
}

int cache_device_instr_invalidate_all(void)
{
	return cache_device_data_invalidate_all();
}

int cache_device_instr_flush_and_invalidate_all(void)
{
	return cache_device_data_flush_and_invalidate_all();
}

int cache_device_data_flush_range(void *addr, size_t size)
{
	return cache_device_flush_range(addr, size);
}

int cache_device_data_invalidate_range(void *addr, size_t size)
{
	return cache_device_invalidate_range(addr, size);
}

int cache_device_data_flush_and_invalidate_range(void *addr, size_t size)
{
	return cache_device_flush_and_invalidate_range(addr, size);
}

int cache_device_instr_flush_range(void *addr, size_t size)
{
	return cache_device_flush_range(addr, size);
}

int cache_device_instr_invalidate_range(void *addr, size_t size)
{
	return cache_device_invalidate_range(addr, size);
}

int cache_device_instr_flush_and_invalidate_range(void *addr, size_t size)
{
	return cache_device_flush_and_invalidate_range(addr, size);
}

int cache_device_enable_all(void)
{
	int rc = 0;

#define DO_ONE(inst) do { \
		const struct device *dev = DEVICE_DT_INST_GET(inst); \
		if (!device_is_ready(dev)) break; \
		int drc = cache_device_enable(dev); \
		if (drc != 0) { rc |= drc; } \
	} while (0)

	DT_INST_FOREACH_STATUS_OKAY(DO_ONE);
#undef DO_ONE

	return rc;
}

int cache_device_disable_all(void)
{
	int rc = 0;

#define DO_ONE(inst) do { \
		const struct device *dev = DEVICE_DT_INST_GET(inst); \
		if (!device_is_ready(dev)) break; \
		int drc = cache_device_disable(dev); \
		if (drc != 0) { rc |= drc; } \
	} while (0)

	DT_INST_FOREACH_STATUS_OKAY(DO_ONE);
#undef DO_ONE

	return rc;
}
