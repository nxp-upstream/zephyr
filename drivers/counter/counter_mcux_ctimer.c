/*
 * Copyright (c) 2021, Toby Firth.
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT nxp_lpc_ctimer

#include <zephyr/drivers/counter.h>
#include <fsl_ctimer.h>
#ifdef CONFIG_COUNTER_CAPTURE
#include <zephyr/drivers/mux.h>
#include <zephyr/drivers/pinctrl.h>
#endif /* CONFIG_COUNTER_CAPTURE */
#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/mcux_lpc_syscon_clock.h>
#include <zephyr/irq.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(mcux_ctimer, CONFIG_COUNTER_LOG_LEVEL);

#ifdef CONFIG_COUNTER_MCUX_CTIMER_RESERVE_CHANNEL_FOR_SETTOP
/* One of the CTimer channels is reserved to implement set_top_value API */
#define NUM_CHANNELS 3
#else
#define NUM_CHANNELS 4
#endif

#ifdef CONFIG_COUNTER_CAPTURE
#define CTIMER_CAPTURE_INT_MASK(chan)    (CTIMER_CCR_CAP0I_MASK << ((uint32_t)(chan) * 3U))
#define CTIMER_CAPTURE_STATUS_MASK(chan) (CTIMER_IR_CR0INT_MASK << (uint32_t)(chan))
#define CTIMER_CAPTURE_VALID_FLAGS       (COUNTER_CAPTURE_BOTH_EDGES | COUNTER_CAPTURE_SINGLE_SHOT)
#endif /* CONFIG_COUNTER_CAPTURE */

struct mcux_lpc_ctimer_channel_data {
	counter_alarm_callback_t alarm_callback;
	void *alarm_user_data;
#ifdef CONFIG_COUNTER_CAPTURE
	counter_capture_cb_t capture_callback;
	void *capture_user_data;
	ctimer_capture_edge_t capture_edge;
	counter_capture_flags_t capture_flags;
	bool capture_single_shot;
#endif /* CONFIG_COUNTER_CAPTURE */
};

#ifdef CONFIG_PM_DEVICE
/*
 * Everything a suspended CTIMER cannot keep for itself. Suspend gates the
 * block's function clock, so the registers stop answering and have to be read
 * out before it goes and written back after. MR and MCR are the run-time part:
 * an armed alarm lives there and nowhere else, so the devicetree configuration
 * alone does not describe the block.
 */
struct mcux_lpc_ctimer_context {
	uint32_t tcr;
	uint32_t tc;
	uint32_t pr;
	uint32_t mcr;
	uint32_t mr[CTIMER_MR_COUNT];
	uint32_t ccr;
	uint32_t emr;
	uint32_t ctcr;
	uint32_t pwmc;
#if defined(CTIMER_MSR_COUNT)
	uint32_t msr[CTIMER_MSR_COUNT];
#endif
	bool valid;
};
#endif /* CONFIG_PM_DEVICE */

struct mcux_lpc_ctimer_data {
	struct mcux_lpc_ctimer_channel_data channels[NUM_CHANNELS];
	counter_top_callback_t top_callback;
	void *top_user_data;
#ifdef CONFIG_PM_DEVICE
	struct mcux_lpc_ctimer_context context;
#endif
};

#ifdef CONFIG_COUNTER_CAPTURE
/* One capture input route decoded from the mux-states phandle-array. The
 * trailing state cell is applied through the mux subsystem, so the routing
 * hardware is whatever devicetree wires up.
 */
struct mcux_lpc_ctimer_mux_entry {
	const struct device *dev;
	const struct mux_state *state;
};
#endif /* CONFIG_COUNTER_CAPTURE */

struct mcux_lpc_ctimer_config {
	struct counter_config_info info;
	CTIMER_Type *base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	ctimer_timer_mode_t mode;
	ctimer_capture_channel_t input;
	uint32_t prescale;
	void (*irq_config_func)(const struct device *dev);
#ifdef CONFIG_COUNTER_CAPTURE
	const struct pinctrl_dev_config *pincfg;
	const struct mcux_lpc_ctimer_mux_entry *mux_entries;
	uint8_t mux_entries_count;
#endif /* CONFIG_COUNTER_CAPTURE */
};

static int mcux_lpc_ctimer_start(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;

	CTIMER_StartTimer(config->base);

	return 0;
}

static int mcux_lpc_ctimer_stop(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;

	CTIMER_StopTimer(config->base);

	return 0;
}

static uint32_t mcux_lpc_ctimer_read(CTIMER_Type *base)
{
	return CTIMER_GetTimerCountValue(base);
}

static int mcux_lpc_ctimer_get_value(const struct device *dev, uint32_t *ticks)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	*ticks = mcux_lpc_ctimer_read(config->base);
	return 0;
}

static uint32_t mcux_lpc_ctimer_get_top_value(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;

#ifdef CONFIG_COUNTER_MCUX_CTIMER_RESERVE_CHANNEL_FOR_SETTOP
	CTIMER_Type *base = config->base;

	/* Return the top value if it has been set, else return the max top value */
	if (base->MR[NUM_CHANNELS] != 0) {
		return base->MR[NUM_CHANNELS];
	} else {
		return config->info.max_top_value;
	}
#else
	return config->info.max_top_value;
#endif
}

static int mcux_lpc_ctimer_set_alarm(const struct device *dev, uint8_t chan_id,
				     const struct counter_alarm_cfg *alarm_cfg)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;
	uint32_t ticks = alarm_cfg->ticks;
	uint32_t current = mcux_lpc_ctimer_read(config->base);
	uint32_t top = mcux_lpc_ctimer_get_top_value(dev);

	if (alarm_cfg->ticks > top) {
		return -EINVAL;
	}

	if (data->channels[chan_id].alarm_callback != NULL) {
		LOG_ERR("channel already in use");
		return -EBUSY;
	}

#ifdef CONFIG_COUNTER_CAPTURE
	if (data->channels[chan_id].capture_callback != NULL) {
		LOG_ERR("channel already configured for capture");
		return -EBUSY;
	}
#endif /* CONFIG_COUNTER_CAPTURE */

	if ((alarm_cfg->flags & COUNTER_ALARM_CFG_ABSOLUTE) == 0) {
		ticks += current;
		if (ticks > top) {
			ticks %= top;
		}
	}

	ctimer_match_config_t match_config = { .matchValue = ticks,
					       .enableCounterReset = false,
					       .enableCounterStop = false,
					       .outControl = kCTIMER_Output_NoAction,
					       .outPinInitState = false,
					       .enableInterrupt = true };

	/*
	 * A previously cancelled alarm can leave its match value programmed; on
	 * this SoC the counter reaching that value latches the channel's status
	 * flag even while the match interrupt is disabled. Mask this channel's
	 * interrupt and drop any latched flag before registering the callback so
	 * a stale match cannot be delivered as a spurious expiry. Only the match
	 * armed by CTIMER_SetupMatch() below, which re-enables the interrupt, can
	 * then reach the callback.
	 */
	CTIMER_DisableInterrupts(config->base, (1U << chan_id));
	CTIMER_ClearStatusFlags(config->base, (1U << chan_id));

	data->channels[chan_id].alarm_callback = alarm_cfg->callback;
	data->channels[chan_id].alarm_user_data = alarm_cfg->user_data;

	CTIMER_SetupMatch(config->base, chan_id, &match_config);

	return 0;
}

static int mcux_lpc_ctimer_cancel_alarm(const struct device *dev, uint8_t chan_id)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;

	CTIMER_DisableInterrupts(config->base, (1 << chan_id));
	/*
	 * Drop any match flag latched for this channel (see set_alarm) so a
	 * cancelled alarm cannot be delivered once the channel is re-armed.
	 */
	CTIMER_ClearStatusFlags(config->base, (1U << chan_id));

	data->channels[chan_id].alarm_callback = NULL;
	data->channels[chan_id].alarm_user_data = NULL;

	return 0;
}

static int mcux_lpc_ctimer_set_top_value(const struct device *dev,
					 const struct counter_top_cfg *cfg)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;

#ifndef CONFIG_COUNTER_MCUX_CTIMER_RESERVE_CHANNEL_FOR_SETTOP
	/* Only allow max value when we do not reserve a ctimer channel for setting top value */
	if (cfg->ticks != config->info.max_top_value) {
		LOG_ERR("Wrap can only be set to 0x%x",
			config->info.max_top_value);
		return -ENOTSUP;
	}
#endif

	data->top_callback = cfg->callback;
	data->top_user_data = cfg->user_data;

	if (!(cfg->flags & COUNTER_TOP_CFG_DONT_RESET)) {
		CTIMER_Reset(config->base);
	} else if (mcux_lpc_ctimer_read(config->base) >= cfg->ticks) {
		if (cfg->flags & COUNTER_TOP_CFG_RESET_WHEN_LATE) {
			CTIMER_Reset(config->base);
		}
		return -ETIME;
	}

#ifdef CONFIG_COUNTER_MCUX_CTIMER_RESERVE_CHANNEL_FOR_SETTOP
	ctimer_match_config_t match_config = { .matchValue = cfg->ticks,
					       .enableCounterReset = true,
					       .enableCounterStop = false,
					       .outControl = kCTIMER_Output_NoAction,
					       .outPinInitState = false,
					       .enableInterrupt = true };

	CTIMER_SetupMatch(config->base, NUM_CHANNELS, &match_config);
#endif

	return 0;
}

static int mcux_lpc_ctimer_reset(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;

	CTIMER_Reset(config->base);

	return 0;
}

static uint32_t mcux_lpc_ctimer_get_pending_int(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	uint32_t mask = 0x0FU;

#ifdef CONFIG_COUNTER_CAPTURE
	mask |= 0xF0U;
#endif /* CONFIG_COUNTER_CAPTURE */

	return (CTIMER_GetStatusFlags(config->base) & mask) != 0;
}

static uint32_t mcux_lpc_ctimer_get_freq(const struct device *dev)
{
	/*
	 * The frequency of the timer is not known at compile time so we need to
	 * calculate at runtime when the frequency is known.
	 */
	const struct mcux_lpc_ctimer_config *config = dev->config;

	uint32_t clk_freq = 0;

	if (clock_control_get_rate(config->clock_dev, config->clock_subsys,
					&clk_freq)) {
		LOG_ERR("unable to get clock frequency");
		return 0;
	}

	/* prescale increments when the prescale counter is 0 so if prescale is 1
	 * the counter is incremented every 2 cycles of the clock so will actually
	 * divide by 2 hence the addition of 1 to the value here.
	 */
	return (clk_freq / (config->prescale + 1));
}

#ifdef CONFIG_COUNTER_CAPTURE
static int mcux_lpc_ctimer_apply_mux(const struct mcux_lpc_ctimer_config *config)
{
	for (uint8_t i = 0; i < config->mux_entries_count; i++) {
		const struct mcux_lpc_ctimer_mux_entry *entry = &config->mux_entries[i];
		int err;

		if (!device_is_ready(entry->dev)) {
			LOG_ERR_DEVICE_NOT_READY(entry->dev);
			return -ENODEV;
		}

		err = mux_state_apply(entry->dev, entry->state);
		if (err) {
			LOG_ERR("failed to apply mux state %u: %d", i, err);
			return err;
		}
	}

	return 0;
}

static int mcux_lpc_ctimer_capture_edge(counter_capture_flags_t flags,
						ctimer_capture_edge_t *edge)
{
	if ((flags & ~CTIMER_CAPTURE_VALID_FLAGS) != 0U) {
		return -EINVAL;
	}

	if ((flags & COUNTER_CAPTURE_BOTH_EDGES) == COUNTER_CAPTURE_BOTH_EDGES) {
		*edge = kCTIMER_Capture_BothEdge;
	} else if ((flags & COUNTER_CAPTURE_FALLING_EDGE) != 0U) {
		*edge = kCTIMER_Capture_FallEdge;
	} else if ((flags & COUNTER_CAPTURE_RISING_EDGE) != 0U) {
		*edge = kCTIMER_Capture_RiseEdge;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int mcux_lpc_ctimer_capture_configure(const struct device *dev, uint8_t chan_id,
					     counter_capture_flags_t flags,
					     counter_capture_cb_t cb, void *user_data)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;
	ctimer_capture_edge_t edge;
	int ret;

	if (chan_id >= NUM_CHANNELS) {
		return -EINVAL;
	}

	if (cb == NULL) {
		return -EINVAL;
	}

	if (data->channels[chan_id].alarm_callback != NULL) {
		LOG_ERR("channel %u already configured for alarm", chan_id);
		return -EBUSY;
	}

	if ((config->base->CCR & CTIMER_CAPTURE_INT_MASK(chan_id)) != 0U) {
		LOG_ERR("capture channel %u is enabled", chan_id);
		return -EBUSY;
	}

	ret = mcux_lpc_ctimer_capture_edge(flags, &edge);
	if (ret != 0) {
		return ret;
	}

	data->channels[chan_id].capture_callback = cb;
	data->channels[chan_id].capture_user_data = user_data;
	data->channels[chan_id].capture_edge = edge;
	data->channels[chan_id].capture_flags = flags & COUNTER_CAPTURE_BOTH_EDGES;
	data->channels[chan_id].capture_single_shot = (flags & COUNTER_CAPTURE_SINGLE_SHOT) != 0U;

	return 0;
}

static int mcux_lpc_ctimer_enable_capture(const struct device *dev, uint8_t chan_id)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;
	struct mcux_lpc_ctimer_channel_data *channel;

	if (chan_id >= NUM_CHANNELS) {
		return -EINVAL;
	}

	channel = &data->channels[chan_id];
	if (channel->alarm_callback != NULL) {
		LOG_ERR("channel %u already configured for alarm", chan_id);
		return -EBUSY;
	}

	if (channel->capture_callback == NULL) {
		LOG_ERR("capture callback not configured for channel %u", chan_id);
		return -EINVAL;
	}

	if ((config->base->CCR & CTIMER_CAPTURE_INT_MASK(chan_id)) != 0U) {
		return -EBUSY;
	}

	CTIMER_ClearStatusFlags(config->base, CTIMER_CAPTURE_STATUS_MASK(chan_id));
	CTIMER_SetupCapture(config->base, (ctimer_capture_channel_t)chan_id,
			   channel->capture_edge, true);

	return 0;
}

static int mcux_lpc_ctimer_disable_capture(const struct device *dev, uint8_t chan_id)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;
	ctimer_capture_channel_t channel;

	if (chan_id >= NUM_CHANNELS) {
		return -EINVAL;
	}

	channel = (ctimer_capture_channel_t)chan_id;
	CTIMER_DisableInterrupts(config->base, CTIMER_CAPTURE_INT_MASK(chan_id));
	CTIMER_EnableRisingEdgeCapture(config->base, channel, false);
	CTIMER_EnableFallingEdgeCapture(config->base, channel, false);
	CTIMER_ClearStatusFlags(config->base, CTIMER_CAPTURE_STATUS_MASK(chan_id));

	data->channels[chan_id].capture_callback = NULL;
	data->channels[chan_id].capture_user_data = NULL;
	data->channels[chan_id].capture_flags = 0U;
	data->channels[chan_id].capture_single_shot = false;

	return 0;
}
#endif /* CONFIG_COUNTER_CAPTURE */

static void mcux_lpc_ctimer_isr(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;

	uint32_t interrupt_stat = CTIMER_GetStatusFlags(config->base);

	CTIMER_ClearStatusFlags(config->base, interrupt_stat);

	uint32_t ticks = mcux_lpc_ctimer_read(config->base);

	for (uint8_t chan = 0; chan < NUM_CHANNELS; chan++) {
		uint8_t channel_mask = 0x01 << chan;

		if (((interrupt_stat & channel_mask) != 0) &&
		    (data->channels[chan].alarm_callback != NULL)) {
			counter_alarm_callback_t alarm_callback =
				data->channels[chan].alarm_callback;
			void *alarm_user_data = data->channels[chan].alarm_user_data;

			data->channels[chan].alarm_callback = NULL;
			data->channels[chan].alarm_user_data = NULL;
			alarm_callback(dev, chan, ticks, alarm_user_data);
		}
	}

#ifdef CONFIG_COUNTER_CAPTURE
	for (uint8_t chan = 0; chan < NUM_CHANNELS; chan++) {
		uint32_t capture_mask = CTIMER_CAPTURE_STATUS_MASK(chan);
		counter_capture_cb_t capture_callback;
		counter_capture_flags_t capture_flags;
		void *capture_user_data;
		uint32_t capture_ticks;

		if ((interrupt_stat & capture_mask) == 0U) {
			continue;
		}

		capture_callback = data->channels[chan].capture_callback;
		if (capture_callback == NULL) {
			continue;
		}

		capture_ticks = CTIMER_GetCaptureValue(config->base,
					       (ctimer_capture_channel_t)chan);
		capture_user_data = data->channels[chan].capture_user_data;
		capture_flags = data->channels[chan].capture_flags;

		if (data->channels[chan].capture_single_shot) {
			capture_flags |= COUNTER_CAPTURE_SINGLE_SHOT;
			(void)mcux_lpc_ctimer_disable_capture(dev, chan);
		} else {
			capture_flags |= COUNTER_CAPTURE_CONTINUOUS;
		}

		capture_callback(dev, chan, capture_flags, capture_ticks, capture_user_data);
	}
#endif /* CONFIG_COUNTER_CAPTURE */

#ifdef CONFIG_COUNTER_MCUX_CTIMER_RESERVE_CHANNEL_FOR_SETTOP
	if (((interrupt_stat & (0x01 << NUM_CHANNELS)) != 0) && data->top_callback) {
		data->top_callback(dev, data->top_user_data);
	}
#endif
}

/*
 * Re-applied whenever the block has to be configured from scratch, which is more
 * than once: a CTIMER that came back from a power-down lost its pin-mux and its
 * capture routing along with its registers.
 */
static int mcux_lpc_ctimer_pins_apply(const struct device *dev)
{
#ifdef CONFIG_COUNTER_CAPTURE
	const struct mcux_lpc_ctimer_config *config = dev->config;
	int ret;

	if (config->pincfg != NULL) {
		ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
		if (ret != 0) {
			return ret;
		}
	}

	return mcux_lpc_ctimer_apply_mux(config);
#else
	ARG_UNUSED(dev);

	return 0;
#endif /* CONFIG_COUNTER_CAPTURE */
}

static void mcux_lpc_ctimer_hw_init(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	ctimer_config_t ctimer_config;

	CTIMER_GetDefaultConfig(&ctimer_config);

	ctimer_config.mode = config->mode;
	ctimer_config.input = config->input;
	ctimer_config.prescale = config->prescale;

	CTIMER_Init(config->base, &ctimer_config);
}

#ifdef CONFIG_PM_DEVICE
static void mcux_lpc_ctimer_context_save(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;
	struct mcux_lpc_ctimer_context *ctx = &data->context;
	CTIMER_Type *base = config->base;

	ctx->tcr = base->TCR;
	ctx->tc = base->TC;
	ctx->pr = base->PR;
	ctx->mcr = base->MCR;
	ctx->ccr = base->CCR;
	ctx->emr = base->EMR;
	ctx->ctcr = base->CTCR;
	ctx->pwmc = base->PWMC;

	for (uint8_t i = 0; i < CTIMER_MR_COUNT; i++) {
		ctx->mr[i] = base->MR[i];
	}
#if defined(CTIMER_MSR_COUNT)
	for (uint8_t i = 0; i < CTIMER_MSR_COUNT; i++) {
		ctx->msr[i] = base->MSR[i];
	}
#endif

	ctx->valid = true;
}

static void mcux_lpc_ctimer_context_restore(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;
	struct mcux_lpc_ctimer_context *ctx = &data->context;
	CTIMER_Type *base = config->base;

	/*
	 * Match and capture flags may be latched already; drop them before MCR and
	 * CCR arm their interrupts, otherwise the first interrupt after resume is
	 * one that belongs to no alarm.
	 */
	CTIMER_ClearStatusFlags(base, 0xFFU);

	base->PR = ctx->pr;
	base->CTCR = ctx->ctcr;
	base->PWMC = ctx->pwmc;
	base->EMR = ctx->emr;

	for (uint8_t i = 0; i < CTIMER_MR_COUNT; i++) {
		base->MR[i] = ctx->mr[i];
	}
#if defined(CTIMER_MSR_COUNT)
	for (uint8_t i = 0; i < CTIMER_MSR_COUNT; i++) {
		base->MSR[i] = ctx->msr[i];
	}
#endif

	base->MCR = ctx->mcr;
	base->CCR = ctx->ccr;
	base->TC = ctx->tc;

	/*
	 * TCR last: its CEN bit restarts the counter, so every register the counter
	 * can act on is already back in place when it does.
	 */
	base->TCR = ctx->tcr;

	ctx->valid = false;
}

static int mcux_lpc_ctimer_clock_off(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;

	return clock_control_off(config->clock_dev, config->clock_subsys);
}

static int mcux_lpc_ctimer_suspend(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;

	/*
	 * Read out before the timer is stopped, so that TCR[CEN] records whether the
	 * counter was running. The few clocks between reading TC and stopping the
	 * counter are the whole error in the TC that resume puts back; the time
	 * spent suspended is not counted at all, which is a property of gating a
	 * counter rather than of this driver.
	 */
	mcux_lpc_ctimer_context_save(dev);
	CTIMER_StopTimer(config->base);

	return mcux_lpc_ctimer_clock_off(dev);
}

#endif /* CONFIG_PM_DEVICE */

/*
 * Bring the block into service from an unknown state: either the register image
 * the last suspend kept, or a fresh configuration out of devicetree.
 */
static void mcux_lpc_ctimer_bring_up(const struct device *dev)
{
#ifdef CONFIG_PM_DEVICE
	struct mcux_lpc_ctimer_data *data = dev->data;

	if (data->context.valid) {
		mcux_lpc_ctimer_context_restore(dev);
		return;
	}
#endif /* CONFIG_PM_DEVICE */

	mcux_lpc_ctimer_hw_init(dev);
}

static int mcux_lpc_ctimer_resume(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	int ret;

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret < 0) {
		return ret;
	}

	ret = mcux_lpc_ctimer_pins_apply(dev);
	if (ret != 0) {
		return ret;
	}

	mcux_lpc_ctimer_bring_up(dev);

	/*
	 * Unconditional: the NVIC enable is lost with the power domain, and
	 * re-enabling an already enabled interrupt is free.
	 */
	config->irq_config_func(dev);

	return 0;
}


static int mcux_lpc_ctimer_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		return mcux_lpc_ctimer_resume(dev);
#ifdef CONFIG_PM_DEVICE
	case PM_DEVICE_ACTION_SUSPEND:
		return mcux_lpc_ctimer_suspend(dev);
	case PM_DEVICE_ACTION_TURN_OFF:
		return mcux_lpc_ctimer_clock_off(dev);
#endif /* CONFIG_PM_DEVICE */
	case PM_DEVICE_ACTION_TURN_ON:
		/*
		 * Nothing. The block is brought up from RESUME, so that an instance
		 * which boots SUSPENDED under runtime device PM boots with its clock
		 * gated rather than running behind a PM state that says otherwise.
		 */
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int mcux_lpc_ctimer_init(const struct device *dev)
{
	const struct mcux_lpc_ctimer_config *config = dev->config;
	struct mcux_lpc_ctimer_data *data = dev->data;

	if (!device_is_ready(config->clock_dev)) {
		LOG_ERR("clock control device not ready");
		return -ENODEV;
	}

	for (uint8_t chan = 0; chan < NUM_CHANNELS; chan++) {
		data->channels[chan].alarm_callback = NULL;
		data->channels[chan].alarm_user_data = NULL;
#ifdef CONFIG_COUNTER_CAPTURE
		data->channels[chan].capture_callback = NULL;
		data->channels[chan].capture_user_data = NULL;
		data->channels[chan].capture_flags = 0U;
		data->channels[chan].capture_single_shot = false;
#endif /* CONFIG_COUNTER_CAPTURE */
	}

	/* The hardware is brought up from the PM_DEVICE_TURN_ON action that this
	 * call invokes.
	 */
	return pm_device_driver_init(dev, mcux_lpc_ctimer_pm_action);
}

static DEVICE_API(counter, mcux_ctimer_driver_api) = {
	.start = mcux_lpc_ctimer_start,
	.stop = mcux_lpc_ctimer_stop,
	.reset = mcux_lpc_ctimer_reset,
	.get_value = mcux_lpc_ctimer_get_value,
	.set_alarm = mcux_lpc_ctimer_set_alarm,
	.cancel_alarm = mcux_lpc_ctimer_cancel_alarm,
	.set_top_value = mcux_lpc_ctimer_set_top_value,
	.get_pending_int = mcux_lpc_ctimer_get_pending_int,
	.get_top_value = mcux_lpc_ctimer_get_top_value,
	.get_freq = mcux_lpc_ctimer_get_freq,
#ifdef CONFIG_COUNTER_CAPTURE
	.capture_configure = mcux_lpc_ctimer_capture_configure,
	.enable_capture = mcux_lpc_ctimer_enable_capture,
	.disable_capture = mcux_lpc_ctimer_disable_capture,
#endif /* CONFIG_COUNTER_CAPTURE */
};

#ifdef CONFIG_COUNTER_CAPTURE
#define COUNTER_LPC_CTIMER_PINCTRL_DEFINE(id) \
	IF_ENABLED(DT_INST_PINCTRL_HAS_NAME(id, default), (PINCTRL_DT_INST_DEFINE(id);))
#define COUNTER_LPC_CTIMER_PINCTRL_INIT(id) \
	.pincfg = COND_CODE_1(DT_INST_NODE_HAS_PROP(id, pinctrl_0), \
				 (PINCTRL_DT_INST_DEV_CONFIG_GET(id)), (NULL)),
#define COUNTER_LPC_CTIMER_MUX_ENTRY(node_id, prop, idx) \
	{ \
		.dev = MUX_STATE_DT_DEV_GET_BY_IDX(node_id, idx), \
		.state = MUX_STATE_DT_GET_BY_IDX(node_id, idx), \
	}
#define COUNTER_LPC_CTIMER_MUX_DEFINE(id) \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(id, mux_states), \
		    (MUX_STATE_DT_INST_SPEC_DEFINE_ALL(id); \
		     static const struct mcux_lpc_ctimer_mux_entry \
			    mcux_lpc_ctimer_mux_entries_##id[] = { \
			    DT_INST_FOREACH_PROP_ELEM_SEP(id, mux_states, \
							 COUNTER_LPC_CTIMER_MUX_ENTRY, (,)) \
		    };), ())
#define COUNTER_LPC_CTIMER_MUX_INIT(id) \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(id, mux_states), \
		    (.mux_entries = mcux_lpc_ctimer_mux_entries_##id, \
		     .mux_entries_count = ARRAY_SIZE(mcux_lpc_ctimer_mux_entries_##id),), ())
#else
#define COUNTER_LPC_CTIMER_PINCTRL_DEFINE(id)
#define COUNTER_LPC_CTIMER_PINCTRL_INIT(id)
#define COUNTER_LPC_CTIMER_MUX_DEFINE(id)
#define COUNTER_LPC_CTIMER_MUX_INIT(id)
#endif /* CONFIG_COUNTER_CAPTURE */

#define COUNTER_LPC_CTIMER_DEVICE(id) \
	COUNTER_LPC_CTIMER_PINCTRL_DEFINE(id) \
	COUNTER_LPC_CTIMER_MUX_DEFINE(id) \
	static void mcux_lpc_ctimer_irq_config_##id(const struct device *dev);                     \
	static struct mcux_lpc_ctimer_config mcux_lpc_ctimer_config_##id = { \
		.info = {						\
			.max_top_value = UINT32_MAX,			\
			.flags = COUNTER_CONFIG_INFO_COUNT_UP,		\
			.channels = NUM_CHANNELS,					\
		},\
		.base = (CTIMER_Type *)DT_INST_REG_ADDR(id),		\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(id)),	\
		.clock_subsys =				\
		(clock_control_subsys_t)(DT_INST_CLOCKS_CELL(id, name)),\
		.mode = DT_INST_PROP(id, mode),						\
		.input = DT_INST_PROP(id, input),					\
		.prescale = DT_INST_PROP(id, prescale),				\
		COUNTER_LPC_CTIMER_PINCTRL_INIT(id) \
		COUNTER_LPC_CTIMER_MUX_INIT(id) \
		.irq_config_func = mcux_lpc_ctimer_irq_config_##id,	\
	};                     \
	PM_DEVICE_DT_INST_DEFINE(id, mcux_lpc_ctimer_pm_action);                                   \
	static struct mcux_lpc_ctimer_data mcux_lpc_ctimer_data_##id;                              \
	DEVICE_DT_INST_DEFINE(id, &mcux_lpc_ctimer_init, PM_DEVICE_DT_INST_GET(id),                \
			      &mcux_lpc_ctimer_data_##id,                                          \
			      &mcux_lpc_ctimer_config_##id, POST_KERNEL,                           \
			      CONFIG_COUNTER_INIT_PRIORITY, &mcux_ctimer_driver_api);              \
	static void mcux_lpc_ctimer_irq_config_##id(const struct device *dev)                      \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(id), DT_INST_IRQ(id, priority), mcux_lpc_ctimer_isr,      \
			    DEVICE_DT_INST_GET(id), 0);                                            \
		irq_enable(DT_INST_IRQN(id));                                                      \
	}

DT_INST_FOREACH_STATUS_OKAY(COUNTER_LPC_CTIMER_DEVICE)
