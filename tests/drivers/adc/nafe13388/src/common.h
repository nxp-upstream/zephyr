/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_TESTS_DRIVERS_ADC_NAFE13388_COMMON_H_
#define ZEPHYR_TESTS_DRIVERS_ADC_NAFE13388_COMMON_H_

#include <zephyr/device.h>
#include <zephyr/dt-bindings/adc/nafe13388.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/nafe13388.h>
#include <zephyr/ztest.h>

#include "nafe13388_emul.h"

#define NAFE1 DEVICE_DT_GET(DT_NODELABEL(nafe1))
#define NAFE5 DEVICE_DT_GET(DT_NODELABEL(nafe5)) /* has no sync-gpios */
#define E1    NAFE_EMUL(nafe1)
#define D1    ((struct nafe_emul_data *)E1->data)

/* Frame number idx of the log must hold exactly these transmitted bytes */
#define ZASSERT_FRAME(e, idx, ...)                                                                 \
	do {                                                                                       \
		static const uint8_t exp_[] = {__VA_ARGS__};                                       \
		zassert_true(nafe_emul_frame_is(e, idx, exp_, sizeof(exp_)), "frame %d", (idx));   \
	} while (0)

/*
 * Bring the emulator and the driver of nafe1 into the state after the init
 * (devicetree channels only): defaults, CMD_RESET through the driver, empty log.
 */
static inline void nafe_before(void *unused)
{
	ARG_UNUSED(unused);

	/* A failed test may have left the device taken, give it back first */
	(void)nafe13388_continuous_stop(NAFE1, NAFE13388_STOP_ABORT);
	(void)nafe13388_stop(NAFE1, NAFE13388_STOP_IDLE);

	nafe_emul_defaults(E1);
	zassert_ok(nafe13388_command(NAFE1, NAFE13388_RESET));
	nafe_emul_clear_log(E1);
}

/* adc_read() of the channels, 24 or 16 bit, into buf */
static inline int nafe_read(uint32_t channels, uint8_t resolution, void *buf, size_t size,
			    const struct adc_sequence_options *opts)
{
	const struct adc_sequence seq = {
		.options = opts,
		.channels = channels,
		.buffer = buf,
		.buffer_size = size,
		.resolution = resolution,
	};

	return adc_read(NAFE1, &seq);
}

static inline struct nafe13388_seq nafe_seq(uint16_t channels, enum nafe13388_mode mode,
					    uint8_t resolution)
{
	struct nafe13388_seq seq = {
		.channels = channels,
		.mode = mode,
		.resolution = resolution,
	};

	return seq;
}

/* The device is free: stop() has nothing to stop and sends no frame */
#define ZASSERT_IDLE()                                                                             \
	zassert_equal(nafe13388_stop(NAFE1, NAFE13388_STOP_IDLE), -EALREADY, "device is taken")

/*
 * Let the acquisition thread of the driver run until it waits for DRDY. The test
 * thread is cooperative, so the driver thread runs only while the test thread sleeps
 * or waits. A DRDY pulse sent before the thread waits would count as a late one.
 * One sleep lasts up to one tick (10 ms on native_sim), see the DRDY timeout in
 * prj.conf.
 */
#define NAFE_SETTLE() k_msleep(1)

/* Wait until a continuous reading has ended and the device is free again */
static inline void nafe_wait_idle(void)
{
	for (int i = 0; i < 100; i++) {
		if (nafe13388_stop(NAFE1, NAFE13388_STOP_IDLE) == -EALREADY) {
			return;
		}
		k_msleep(1);
	}

	zassert_unreachable("the device is still taken");
}

/* No protocol error of the driver may have been seen by the emulator */
static inline void nafe_after(void *unused)
{
	ARG_UNUSED(unused);

	zassert_equal(D1->errors, 0, "the emulator saw %u protocol errors", D1->errors);
}

#endif /* ZEPHYR_TESTS_DRIVERS_ADC_NAFE13388_COMMON_H_ */
