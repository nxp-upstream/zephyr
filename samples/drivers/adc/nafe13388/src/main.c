/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * NAFE13388 sample. It shows three ways to read the chip:
 *
 * 1. One channel with the standard ADC API (adc_read_dt()).
 * 2. Two channels in one sequence with the standard ADC API (adc_read()).
 * 3. A continuous reading that calls a callback for every sequence, with the
 *    extension API of the driver (nafe13388_continuous_start()).
 *
 * The three readings run once at boot. Then the shell commands "nafe read",
 * "nafe seq" and "nafe cont" repeat them.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/nafe13388.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define NUM_CHANNELS    2
#define SEQ_RESOLUTION  24
#define CONT_MAX        32
#define CONT_BOOT_COUNT 10
#define CONT_TIMEOUT    K_SECONDS(5)

/* The two channels of the overlay, io-channels of the zephyr,user node */
static const struct adc_dt_spec adc_channels[NUM_CHANNELS] = {
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
};

/* Print on the shell if there is one, else on the console */
#define SAY(sh, ...)                                                                               \
	do {                                                                                       \
		if ((sh) != NULL) {                                                                \
			shell_print((sh), __VA_ARGS__);                                            \
		} else {                                                                           \
			printk(__VA_ARGS__);                                                       \
			printk("\n");                                                              \
		}                                                                                  \
	} while (0)

/* Convert a raw value to millivolts with the channel settings of the devicetree */
static int32_t to_mv(size_t idx, int32_t raw, bool *ok)
{
	int32_t val = raw;

	*ok = (adc_raw_to_millivolts_dt(&adc_channels[idx], &val) == 0);

	return val;
}

static void say_value(const struct shell *sh, size_t idx, int32_t raw)
{
	bool ok;
	int32_t mv = to_mv(idx, raw, &ok);

	if (ok) {
		SAY(sh, "  channel %u: raw %d = %d mV", (unsigned int)idx, raw, mv);
	} else {
		SAY(sh, "  channel %u: raw %d (value in mV not available)", (unsigned int)idx, raw);
	}
}

/* Part 1: one channel, one call of adc_read_dt() */
static int read_one(const struct shell *sh, size_t idx)
{
	int32_t buf = 0;
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* The buffer size is in bytes, not in samples */
		.buffer_size = sizeof(buf),
	};
	int err;

	(void)adc_sequence_init_dt(&adc_channels[idx], &sequence);

	err = adc_read_dt(&adc_channels[idx], &sequence);
	if (err < 0) {
		SAY(sh, "Single read failed (%d)", err);
		return err;
	}

	SAY(sh, "Single read:");
	say_value(sh, idx, buf);

	return 0;
}

/* Part 2: both channels in one sequence, one call of adc_read() */
static int read_sequence(const struct shell *sh)
{
	int32_t buf[NUM_CHANNELS] = {0};
	struct adc_sequence sequence = {
		.channels = BIT(adc_channels[0].channel_id) | BIT(adc_channels[1].channel_id),
		.buffer = buf,
		.buffer_size = sizeof(buf),
		.resolution = SEQ_RESOLUTION,
	};
	int err;

	err = adc_read(adc_channels[0].dev, &sequence);
	if (err < 0) {
		SAY(sh, "Sequence read failed (%d)", err);
		return err;
	}

	/* The values are in the order of the channel numbers */
	SAY(sh, "Sequence read:");
	for (size_t i = 0; i < NUM_CHANNELS; i++) {
		say_value(sh, i, buf[i]);
	}

	return 0;
}

/* Part 3: continuous reading. The callback runs on the thread of the driver. */
struct cont_ctx {
	struct k_sem done;
	int32_t data[CONT_MAX][NUM_CHANNELS];
	unsigned int want;
	unsigned int count;
	unsigned int late;
	int status;
};

static struct cont_ctx cont;

static int cont_cb(const struct device *dev, const int32_t *raw, unsigned int n, int status,
		   void *user_data)
{
	struct cont_ctx *ctx = user_data;

	ARG_UNUSED(dev);

	if (raw == NULL) {
		/* Fatal error, this is the last call */
		ctx->status = status;
		k_sem_give(&ctx->done);
		return 0;
	}

	if (status == -EOVERFLOW) {
		/* The data is valid, but the processing is late, sequences may be lost */
		ctx->late++;
	}

	if (n == NUM_CHANNELS && ctx->count < ctx->want) {
		for (unsigned int i = 0; i < n; i++) {
			ctx->data[ctx->count][i] = raw[i];
		}
		ctx->count++;
		if (ctx->count == ctx->want) {
			k_sem_give(&ctx->done);
		}
	}

	/* Keep this short and return 0 to go on, non-zero would stop the reading */
	return 0;
}

static int read_continuous(const struct shell *sh, unsigned int count)
{
	const struct nafe13388_seq seq = {
		.channels = BIT(adc_channels[0].channel_id) | BIT(adc_channels[1].channel_id),
		.mode = NAFE13388_MODE_MC,
		.resolution = SEQ_RESOLUTION,
	};
	const struct device *dev = adc_channels[0].dev;
	int err;

	count = CLAMP(count, 1, CONT_MAX);

	k_sem_init(&cont.done, 0, 1);
	cont.want = count;
	cont.count = 0;
	cont.late = 0;
	cont.status = 0;

	err = nafe13388_continuous_start(dev, &seq, cont_cb, &cont);
	if (err < 0) {
		/* -EBUSY if another reading is running: the device has one reading at a time */
		SAY(sh, "Continuous start failed (%d)", err);
		return err;
	}

	if (k_sem_take(&cont.done, CONT_TIMEOUT) != 0) {
		SAY(sh, "Continuous read timed out");
	}

	/* Stop at once. -EALREADY means that the driver ended the reading itself. */
	err = nafe13388_continuous_stop(dev, NAFE13388_STOP_ABORT);
	if (err < 0 && err != -EALREADY) {
		SAY(sh, "Continuous stop failed (%d)", err);
	}

	if (cont.status != 0) {
		SAY(sh, "Continuous read ended by an error (%d)", cont.status);
	}

	SAY(sh, "Continuous read: %u sequences, %u late", cont.count, cont.late);
	for (unsigned int i = 0; i < cont.count; i++) {
		bool ok0, ok1;
		int32_t mv0 = to_mv(0, cont.data[i][0], &ok0);
		int32_t mv1 = to_mv(1, cont.data[i][1], &ok1);

		SAY(sh, "  [%u] channel 0: %d mV, channel 1: %d mV", i, ok0 ? mv0 : cont.data[i][0],
		    ok1 ? mv1 : cont.data[i][1]);
	}

	return 0;
}

static int cmd_read(const struct shell *sh, size_t argc, char **argv)
{
	int err = 0;
	unsigned long idx = shell_strtoul(argv[1], 10, &err);

	if (err != 0 || idx >= NUM_CHANNELS) {
		shell_error(sh, "channel must be 0 or 1");
		return -EINVAL;
	}

	return read_one(sh, idx);
}

static int cmd_seq(const struct shell *sh, size_t argc, char **argv)
{
	return read_sequence(sh);
}

static int cmd_cont(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int count = CONT_BOOT_COUNT;

	if (argc > 1) {
		int err = 0;

		count = shell_strtoul(argv[1], 10, &err);
		if (err != 0 || count == 0) {
			shell_error(sh, "count must be 1 to %d", CONT_MAX);
			return -EINVAL;
		}
	}

	return read_continuous(sh, count);
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_nafe,
	SHELL_CMD_ARG(read, NULL, "Read one channel with adc_read_dt()\nUsage: nafe read <0|1>",
		      cmd_read, 2, 0),
	SHELL_CMD(seq, NULL, "Read both channels in one sequence with adc_read()", cmd_seq),
	SHELL_CMD_ARG(cont, NULL,
		      "Continuous reading with a callback\nUsage: nafe cont [count 1..32]",
		      cmd_cont, 1, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(nafe, &sub_nafe, "NAFE13388 sample commands", NULL);

/*
 * The shell takes over the UART a little after boot. Printing with printk() at the same
 * time mixes the text with the prompt, so the boot output goes through the shell once it
 * is ready. It falls back to printk() if the shell does not come up.
 */
static const struct shell *boot_shell(void)
{
	const struct shell *sh = shell_backend_uart_get_ptr();

	for (int i = 0; i < 100 && !shell_ready(sh); i++) {
		k_msleep(10);
	}

	return shell_ready(sh) ? sh : NULL;
}

int main(void)
{
	const struct shell *sh = boot_shell();

	SAY(sh, "NAFE13388 sample");

	for (size_t i = 0; i < NUM_CHANNELS; i++) {
		int err;

		if (!adc_is_ready_dt(&adc_channels[i])) {
			printk("ADC device %s is not ready\n", adc_channels[i].dev->name);
			return 0;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);
		if (err < 0) {
			printk("Could not set up channel %u (%d)\n", (unsigned int)i, err);
			return 0;
		}
	}

	(void)read_one(sh, 0);
	(void)read_sequence(sh);
	(void)read_continuous(sh, CONT_BOOT_COUNT);

	SAY(sh, "Try the shell commands: nafe read <0|1>, nafe seq, nafe cont [count]");

	return 0;
}
