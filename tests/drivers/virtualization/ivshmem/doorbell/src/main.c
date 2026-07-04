/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/drivers/virtualization/ivshmem.h>

/*
 * These tests exercise the ivshmem-doorbell device, which needs an
 * ivshmem-server running on the host. Twister provides it via the
 * "ivshmem-server" sidecar; run by hand as:
 *
 *   ivshmem-server -F -S /tmp/ivshmem_socket -M ivshmem -l 4096 -n 2
 */

ZTEST(ivshmem_doorbell, test_doorbell_device_ready)
{
	const struct device *ivshmem;
	uintptr_t mem;
	size_t size;
	uint16_t vectors;

	ivshmem = DEVICE_DT_GET_ONE(qemu_ivshmem);
	zassert_true(device_is_ready(ivshmem), "ivshmem device is not ready");

	/* A mapped region and interrupt vectors both require the server to have
	 * accepted the connection during driver init.
	 */
	size = ivshmem_get_mem(ivshmem, &mem);
	zassert_not_equal(size, 0, "Shared memory size cannot be 0");
	zassert_not_equal(mem, 0, "Shared memory cannot be NULL");

	vectors = ivshmem_get_vectors(ivshmem);
	zassert_not_equal(vectors, 0, "ivshmem-doorbell must have vectors");
}

ZTEST(ivshmem_doorbell, test_doorbell_self_notify)
{
	const struct device *ivshmem;
	struct k_poll_signal signal;
	unsigned int signaled;
	uint32_t id;
	int result;
	int ret;

	ivshmem = DEVICE_DT_GET_ONE(qemu_ivshmem);
	zassert_true(device_is_ready(ivshmem), "ivshmem device is not ready");

	id = ivshmem_get_id(ivshmem);

	k_poll_signal_init(&signal);

	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &signal),
	};

	ret = ivshmem_register_handler(ivshmem, &signal, 0);
	zassert_ok(ret, "Could not register the vector 0 handler (%d)", ret);

	/* Ring our own doorbell: the interrupt travels to the server and back,
	 * so a delivered signal proves the full doorbell path is up.
	 */
	ret = ivshmem_int_peer(ivshmem, id, 0);
	zassert_ok(ret, "Could not notify peer %u vector 0 (%d)", id, ret);

	ret = k_poll(events, ARRAY_SIZE(events), K_MSEC(1000));
	zassert_ok(ret, "Timed out waiting for the doorbell interrupt (%d)", ret);

	k_poll_signal_check(&signal, &signaled, &result);
	zassert_true(signaled, "Doorbell signal was not raised");
	zassert_equal(result, 0, "Expected vector 0, got %d", result);
}

ZTEST_SUITE(ivshmem_doorbell, NULL, NULL, NULL, NULL, NULL);
