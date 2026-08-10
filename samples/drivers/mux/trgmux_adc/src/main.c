/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC hardware trigger via signal router (TRGMUX / INPUTMUX / XBAR).
 *
 * Platform-agnostic flow:
 *   1. Apply the DT-defined signal route (mux-states) so the timer output
 *      is connected to the ADC trigger input.
 *   2. Configure the ADC channel.
 *   3. Register a counter top callback and start the periodic timer.
 *   4. The callback fires in ISR context on every timer overflow; it submits
 *      a k_work item to the system work queue.
 *   5. The work handler calls adc_read_dt() in thread context (required
 *      because adc_read_dt blocks on a semaphore), then prints the result.
 *   6. main() returns after setup; all sampling is driven by the hardware
 *      timer with no software polling loop.
 *
 * All per-platform wiring (which mux controller, which ADC, which timer,
 * and the concrete routing cell values) lives in the board overlay.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/mux.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* ---- Devicetree nodes --------------------------------------------------- */

#define ROUTE_NODE DT_NODELABEL(adc_hw_trigger_route)
#define USER_NODE  DT_PATH(zephyr_user)

BUILD_ASSERT(DT_NODE_EXISTS(ROUTE_NODE),
	     "board overlay must define an 'adc-hw-trigger-route' node");
BUILD_ASSERT(DT_NODE_HAS_PROP(USER_NODE, io_channels),
	     "board overlay must add 'io-channels' to /zephyr,user");
BUILD_ASSERT(DT_NODE_HAS_PROP(USER_NODE, trigger_timer),
	     "board overlay must add 'trigger-timer' to /zephyr,user");

/* DT-defined signal route (mux-states entry 0). */
MUX_STATE_DT_SPEC_DEFINE(ROUTE_NODE);

/* ADC channel from /zephyr,user io-channels[0]. */
static const struct adc_dt_spec adc_chan = ADC_DT_SPEC_GET(USER_NODE);

/* Timer that generates the periodic hardware trigger. */
static const struct device *const timer_dev =
	DEVICE_DT_GET(DT_PHANDLE(USER_NODE, trigger_timer));

/* MUX controller resolved from the route node. */
static const struct device *const mux_dev = MUX_STATE_DT_DEV_GET(ROUTE_NODE);

/* ---- Configuration ------------------------------------------------------ */

/* ADC sampling rate in Hz. The timer top value is derived from the counter
 * frequency at runtime so no board-specific Kconfig is needed.
 */
#define SAMPLE_RATE_HZ 100U

/* ---- ADC sampling work -------------------------------------------------- */

static uint32_t adc_buf;
static struct adc_sequence seq = {
	.buffer      = &adc_buf,
	.buffer_size = sizeof(adc_buf),
};
static uint32_t sample_count;

/*
 * Work item executed in thread context on every timer tick.
 *
 * adc_read_dt() arms the ADC channel and blocks until the conversion is
 * complete.  In hardware-trigger mode the ADC waits for the external pulse
 * (already routed by the mux) rather than issuing a software trigger, so
 * the actual sampling instant is controlled by the hardware timer, not by
 * when this work item happens to be scheduled.
 */
static void adc_sample_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int ret = adc_read_dt(&adc_chan, &seq);

	if (ret < 0) {
		printk("ADC read error: %d\n", ret);
		return;
	}

	int32_t val_mv = (int32_t)adc_buf;

	ret = adc_raw_to_millivolts_dt(&adc_chan, &val_mv);
	if (ret < 0) {
		printk("ADC reading[%u]: raw=0x%04x "
		       "(conversion to mV not supported)\n",
		       sample_count, adc_buf);
	} else {
		printk("ADC reading[%u]: %d mV\n", sample_count, val_mv);
	}

	sample_count++;
}

static K_WORK_DEFINE(adc_sample_work, adc_sample_work_handler);

/*
 * Counter top callback — runs in ISR context on every timer overflow.
 *
 * Only submits the work item; must not call adc_read_dt() directly because
 * that function blocks on a semaphore.
 */
static void timer_top_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	k_work_submit(&adc_sample_work);
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
	int ret;

	/* 1. Verify all devices are ready. */
	if (!device_is_ready(mux_dev)) {
		printk("MUX controller not ready\n");
		return -ENODEV;
	}
	if (!device_is_ready(timer_dev)) {
		printk("Timer device not ready\n");
		return -ENODEV;
	}
	if (!adc_is_ready_dt(&adc_chan)) {
		printk("ADC controller not ready\n");
		return -ENODEV;
	}

	/* 2. Apply the DT-defined routing: timer output -> ADC trigger input. */
	ret = mux_state_apply(mux_dev, MUX_STATE_DT_GET(ROUTE_NODE));
	if (ret < 0) {
		printk("Failed to apply mux route: %d\n", ret);
		return ret;
	}
	printk("Signal route applied\n");

	/* 3. Configure the ADC channel and initialise the sequence once. */
	ret = adc_channel_setup_dt(&adc_chan);
	if (ret < 0) {
		printk("Failed to set up ADC channel: %d\n", ret);
		return ret;
	}

	ret = adc_sequence_init_dt(&adc_chan, &seq);
	if (ret < 0) {
		printk("Failed to init ADC sequence: %d\n", ret);
		return ret;
	}

	/* 4. Configure the timer: derive the top value from the counter
	 *    frequency so the same source works on any supported board.
	 */
	uint32_t timer_freq = counter_get_frequency(timer_dev);

	if (timer_freq == 0U) {
		printk("Counter frequency unknown\n");
		return -EINVAL;
	}

	struct counter_top_cfg top_cfg = {
		.ticks     = timer_freq / SAMPLE_RATE_HZ,
		.callback  = timer_top_cb,
		.user_data = NULL,
		.flags     = 0,
	};

	ret = counter_set_top_value(timer_dev, &top_cfg);
	if (ret < 0) {
		printk("Failed to set timer period: %d\n", ret);
		return ret;
	}

	/* 5. Start the timer. From this point every overflow fires timer_top_cb
	 *    which submits adc_sample_work. main() is no longer needed.
	 */
	ret = counter_start(timer_dev);
	if (ret < 0) {
		printk("Failed to start timer: %d\n", ret);
		return ret;
	}

	printk("Timer started at %u Hz (top=%u ticks) — sampling driven by hardware\n",
	       SAMPLE_RATE_HZ, top_cfg.ticks);

	/* main() exits; the system work queue services adc_sample_work
	 * indefinitely. The Zephyr idle thread runs when no work is pending,
	 * allowing the SoC to enter sleep states between samples.
	 */
	return 0;
}
