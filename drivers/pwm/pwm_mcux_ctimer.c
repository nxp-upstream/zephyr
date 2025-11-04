/*
 * (c) Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_ctimer_pwm

#include <errno.h>
#include <fsl_ctimer.h>
#include <fsl_clock.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/mcux_lpc_syscon_clock.h>

#ifdef CONFIG_PWM_CAPTURE
#include <zephyr/irq.h>
#include <fsl_inputmux.h>
#include <fsl_reset.h>
#endif


#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pwm_mcux_ctimer, CONFIG_PWM_LOG_LEVEL);

#define CONFIG_PWM_CAPTURE 1

#ifdef CTIMER_MR_COUNT
#define CHANNEL_COUNT CTIMER_MR_COUNT
#else
#define CHANNEL_COUNT kCTIMER_Match_3 + 1
#endif

enum pwm_ctimer_channel_role {
	PWM_CTIMER_CHANNEL_ROLE_NONE = 0,
	PWM_CTIMER_CHANNEL_ROLE_PULSE,
	PWM_CTIMER_CHANNEL_ROLE_PERIOD,
	PWM_CTIMER_CHANNEL_ROLE_CAPTURE,
};

struct pwm_ctimer_channel_state {
	enum pwm_ctimer_channel_role role;
	uint32_t cycles;
};

#ifdef CONFIG_PWM_CAPTURE
struct pwm_mcux_ctimer_capture_data {
	pwm_capture_callback_handler_t callback;
	void *user_data;
	uint32_t overflow_count;
	uint32_t channel;
	uint32_t first_capture_value;
	bool continuous : 1;
	bool overflowed : 1;
	bool pulse_capture : 1;
	bool first_edge_captured : 1;
	bool inverted : 1;
};
#endif /* CONFIG_PWM_CAPTURE */

struct pwm_mcux_ctimer_data {
	struct pwm_ctimer_channel_state channel_states[CHANNEL_COUNT];
	ctimer_match_t current_period_channel;
	bool is_period_channel_set;
	uint32_t num_active_pulse_chans;
#ifdef CONFIG_PWM_CAPTURE
	struct pwm_mcux_ctimer_capture_data capture;
#endif /* CONFIG_PWM_CAPTURE */
};

struct pwm_mcux_ctimer_config {
	CTIMER_Type *base;
	uint32_t prescale;
	uint32_t period_channel;
	const struct device *clock_control;
	clock_control_subsys_t clock_id;
	const struct pinctrl_dev_config *pincfg;
#ifdef CONFIG_PWM_CAPTURE
	void (*irq_config_func)(const struct device *dev);
#endif /* CONFIG_PWM_CAPTURE */
};

/*
 * All pwm signals generated from the same ctimer must have same period. To avoid this, we check
 * if the new parameters will NOT change the period for a ctimer with active pulse channels
 */
static bool mcux_ctimer_pwm_is_period_valid(struct pwm_mcux_ctimer_data *data,
					    uint32_t new_pulse_channel, uint32_t new_period_cycles,
					    uint32_t current_period_channel)
{
	/* if we aren't changing the period, we're ok */
	if (data->channel_states[current_period_channel].cycles == new_period_cycles) {
		return true;
	}

	/*
	 * if we are changing it but there aren't any pulse channels that depend on it, then we're
	 * ok too
	 */
	if (data->num_active_pulse_chans == 0) {
		return true;
	}

	if (data->num_active_pulse_chans > 1) {
		return false;
	}

	/*
	 * there is exactly one pulse channel that depends on existing period and its not the
	 * one we're changing now
	 */
	if (data->channel_states[new_pulse_channel].role != PWM_CTIMER_CHANNEL_ROLE_PULSE) {
		return false;
	}

	return true;
}

/*
 * Each ctimer channel can either be used as a pulse or period channel.  Each channel has a counter.
 * The duty cycle is counted by the pulse channel. When the period channel counts down, it resets
 * the pulse channel (and all counters in the ctimer instance).  The pwm api does not permit us to
 * specify a period channel (only pulse channel). So we need to figure out an acceptable period
 * channel in the driver (if that's even possible)
 */
static int mcux_ctimer_pwm_select_period_channel(struct pwm_mcux_ctimer_data *data,
						 uint32_t new_pulse_channel,
						 uint32_t new_period_cycles,
						 uint32_t *ret_period_channel)
{
	if (data->is_period_channel_set) {
		if (!mcux_ctimer_pwm_is_period_valid(data, new_pulse_channel, new_period_cycles,
						     data->current_period_channel)) {
			LOG_ERR("Cannot set channel %u to %u as period channel",
				*ret_period_channel, new_period_cycles);
			return -EINVAL;
		}

		*ret_period_channel = data->current_period_channel;
		if (new_pulse_channel != *ret_period_channel) {
			/* the existing period channel will not conflict with new pulse_channel */
			return 0;
		}
	}

	/* we need to find an unused channel to use as period_channel */
	*ret_period_channel = new_pulse_channel + 1;
	*ret_period_channel %= CHANNEL_COUNT;
	while (data->channel_states[*ret_period_channel].role != PWM_CTIMER_CHANNEL_ROLE_NONE) {
		if (new_pulse_channel == *ret_period_channel) {
			LOG_ERR("no available channel for period counter");
			return -EBUSY;
		}
		(*ret_period_channel)++;
		*ret_period_channel %= CHANNEL_COUNT;
	}

	return 0;
}

static void mcux_ctimer_pwm_update_state(struct pwm_mcux_ctimer_data *data, uint32_t pulse_channel,
					 uint32_t pulse_cycles, uint32_t period_channel,
					 uint32_t period_cycles)
{
	if (data->channel_states[pulse_channel].role != PWM_CTIMER_CHANNEL_ROLE_PULSE) {
		data->num_active_pulse_chans++;
	}

	data->channel_states[pulse_channel].role = PWM_CTIMER_CHANNEL_ROLE_PULSE;
	data->channel_states[pulse_channel].cycles = pulse_cycles;

	data->is_period_channel_set = true;
	data->current_period_channel = period_channel;
	data->channel_states[period_channel].role = PWM_CTIMER_CHANNEL_ROLE_PERIOD;
	data->channel_states[period_channel].cycles = period_cycles;
}

static int mcux_ctimer_pwm_set_cycles(const struct device *dev, uint32_t pulse_channel,
				      uint32_t period_cycles, uint32_t pulse_cycles,
				      pwm_flags_t flags)
{
	const struct pwm_mcux_ctimer_config *config = dev->config;
	struct pwm_mcux_ctimer_data *data = dev->data;
	uint32_t period_channel = data->current_period_channel;
	int ret = 0;
	status_t status;

	if (pulse_channel >= CHANNEL_COUNT) {
		LOG_ERR("Invalid channel %u. must be less than %u", pulse_channel, CHANNEL_COUNT);
		return -EINVAL;
	}

	if (period_cycles == 0) {
		LOG_ERR("Channel can not be set to zero");
		return -ENOTSUP;
	}

#ifdef CONFIG_PWM_CAPTURE
	/* Check if channel is being used for capture */
	if (data->channel_states[pulse_channel].role == PWM_CTIMER_CHANNEL_ROLE_CAPTURE) {
		LOG_ERR("Channel %u is being used for capture", pulse_channel);
		return -EBUSY;
	}
#endif /* CONFIG_PWM_CAPTURE */

	ret = mcux_ctimer_pwm_select_period_channel(data, pulse_channel, period_cycles,
						    &period_channel);
	if (ret != 0) {
		LOG_ERR("could not select valid period channel. ret=%d", ret);
		return ret;
	}

	if (flags & PWM_POLARITY_INVERTED) {
		if (pulse_cycles == 0) {
			/* make pulse cycles greater than period so event never occurs */
			pulse_cycles = period_cycles + 1;
		} else {
			pulse_cycles = period_cycles - pulse_cycles;
		}
	}

	status = CTIMER_SetupPwmPeriod(config->base, period_channel, pulse_channel, period_cycles,
				       pulse_cycles, false);
	if (kStatus_Success != status) {
		LOG_ERR("failed setup pwm period. status=%d", status);
		return -EIO;
	}
	mcux_ctimer_pwm_update_state(data, pulse_channel, pulse_cycles, period_channel,
				     period_cycles);

	CTIMER_StartTimer(config->base);
	return 0;
}

static int mcux_ctimer_pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel,
					      uint64_t *cycles)
{
	const struct pwm_mcux_ctimer_config *config = dev->config;
	int err = 0;

	/* clean up upper word of return parameter */
	*cycles &= 0xFFFFFFFF;

	err = clock_control_get_rate(config->clock_control, config->clock_id, (uint32_t *)cycles);
	if (err != 0) {
		LOG_ERR("could not get clock rate");
		return err;
	}

	if (config->prescale > 0) {
		*cycles /= config->prescale;
	}

	return err;
}

#ifdef CONFIG_PWM_CAPTURE
static inline bool mcux_ctimer_channel_is_active(const struct device *dev, uint32_t channel)
{
	struct pwm_mcux_ctimer_data *data = dev->data;

	return data->channel_states[channel].role == PWM_CTIMER_CHANNEL_ROLE_CAPTURE;
}

static inline void mcux_ctimer_set_overflow(const struct device *dev, uint32_t channel)
{
	const struct pwm_mcux_ctimer_config *config = dev->config;
	uint32_t reg;

	reg = config->base->MCR;
    reg &= ~((uint32_t)((uint32_t)CTIMER_MCR_MR0R_MASK | (uint32_t)CTIMER_MCR_MR0S_MASK | 
			(uint32_t)CTIMER_MCR_MR0I_MASK) << ((uint32_t)channel * 3U));

    /* Enable match interrupt */
    reg |= (((uint32_t)CTIMER_MCR_MR0I_MASK) << (CTIMER_MCR_MR0I_SHIFT + ((uint32_t)channel * 3U)));
    /* Reset the counter when match on the channel */
    reg |= ((uint32_t)((uint32_t)CTIMER_MCR_MR0R_MASK) << ((uint32_t)channel * 3U));

    config->base->MCR = reg;
    /* Set match value to maximum counter to indicate overflow*/
    config->base->MR[channel] = 0xFFFFFFFFU;
}

static int mcux_ctimer_configure_capture(const struct device *dev,
					 uint32_t channel, pwm_flags_t flags,
					 pwm_capture_callback_handler_t cb,
					 void *user_data)
{
	const struct pwm_mcux_ctimer_config *config = dev->config;
	struct pwm_mcux_ctimer_data *data = dev->data;
	ctimer_capture_edge_t edge;
	bool inverted = (flags & PWM_POLARITY_MASK) == PWM_POLARITY_INVERTED;

	if (channel >= CHANNEL_COUNT) {
		LOG_ERR("invalid channel %d", channel);
		return -EINVAL;
	}

	if (mcux_ctimer_channel_is_active(dev, channel)) {
		LOG_ERR("pwm capture in progress");
		return -EBUSY;
	}

	/* Check if channel is being used for PWM output */
	if (data->channel_states[channel].role == PWM_CTIMER_CHANNEL_ROLE_PULSE ||
	    data->channel_states[channel].role == PWM_CTIMER_CHANNEL_ROLE_PERIOD) {
		LOG_ERR("Channel %u is being used for PWM output", channel);
		return -EBUSY;
	}

	if (!(flags & PWM_CAPTURE_TYPE_MASK)) {
		LOG_ERR("No capture type specified");
		return -EINVAL;
	}

	if ((flags & PWM_CAPTURE_TYPE_MASK) == PWM_CAPTURE_TYPE_BOTH) {
		LOG_ERR("Cannot capture both period and pulse width");
		return -ENOTSUP;
	}

	data->capture.callback = cb;
	data->capture.user_data = user_data;
	data->capture.channel = channel;
	data->capture.inverted = inverted;
	data->capture.continuous =
		(flags & PWM_CAPTURE_MODE_MASK) == PWM_CAPTURE_MODE_CONTINUOUS;

	if (flags & PWM_CAPTURE_TYPE_PERIOD) {
		data->capture.pulse_capture = false;
		/* For period capture, we need rising edge (or falling if inverted) */
		edge = inverted ? kCTIMER_Capture_FallEdge : kCTIMER_Capture_RiseEdge;
	} else {
		data->capture.pulse_capture = true;
		/* For pulse capture, we need both edges */
		edge = kCTIMER_Capture_BothEdge;
	}

	/* Setup capture on the specified channel and enable capture interrput */
	CTIMER_SetupCapture(config->base, (ctimer_capture_channel_t)channel, edge, true);

	/* Mark channel as being used for capture */
	data->channel_states[channel].role = PWM_CTIMER_CHANNEL_ROLE_CAPTURE;

	/* Enable match interrupt, match value is 0xffffffff, to get overflow count */
	mcux_ctimer_set_overflow(dev, channel);
    
	return 0;
}

static int mcux_ctimer_enable_capture(const struct device *dev, uint32_t channel)
{
	const struct pwm_mcux_ctimer_config *config = dev->config;
	struct pwm_mcux_ctimer_data *data = dev->data;

	if (channel >= CHANNEL_COUNT) {
		LOG_ERR("invalid channel %d", channel);
		return -EINVAL;
	}

	if (!data->capture.callback) {
		LOG_ERR("PWM capture not configured");
		return -EINVAL;
	}

	if (!mcux_ctimer_channel_is_active(dev, channel)) {
		LOG_ERR("PWM capture not configured for channel %u", channel);
		return -EINVAL;
	}

	data->capture.overflowed = false;
	data->capture.first_edge_captured = false;
	data->capture.overflow_count = 0;
	data->capture.first_capture_value = 0;

	CTIMER_StartTimer(config->base);

	return 0;
}

static int mcux_ctimer_disable_capture(const struct device *dev, uint32_t channel)
{
	const struct pwm_mcux_ctimer_config *config = dev->config;
	struct pwm_mcux_ctimer_data *data = dev->data;

	if (channel >= CHANNEL_COUNT) {
		LOG_ERR("invalid channel %d", channel);
		return -EINVAL;
	}

	/* Disable capture interrupts */
	CTIMER_DisableInterrupts(config->base, 
				(kCTIMER_Capture0InterruptEnable << ((__builtin_ctz(CTIMER_CCR_CAP0I_MASK) + 1)*channel)));

	/* Mark channel as unused */
	data->channel_states[channel].role = PWM_CTIMER_CHANNEL_ROLE_NONE;

	return 0;
}

static int mcux_ctimer_calc_ticks(uint32_t first_capture, uint32_t second_capture,
				 uint32_t overflows, uint32_t *result)
{
	uint32_t ticks;

	if (second_capture >= first_capture) {
		/* No timer overflow between captures */
		ticks = second_capture - first_capture;
	} else {
		/* Timer overflowed between captures */
		ticks = (0xFFFFFFFF - first_capture) + second_capture + 1;
		if (overflows > 0) {
			overflows--; /* Account for the overflow we just calculated */
		}
	}

	/* Add additional overflows */
	if (u32_mul_overflow(overflows, 0xFFFFFFFF, &overflows)) {
		return -ERANGE;
	}

	if (u32_add_overflow(ticks, overflows, &ticks)) {
		return -ERANGE;
	}

	*result = ticks;
	return 0;
}

static void mcux_ctimer_isr(const struct device *dev)
{
	const struct pwm_mcux_ctimer_config *config = dev->config;
	struct pwm_mcux_ctimer_data *data = dev->data;
	uint32_t flags;
	uint32_t capture_value;
	uint32_t ticks = 0;
	int err = 0;
	uint32_t channel = data->capture.channel;

	flags = CTIMER_GetStatusFlags(config->base);

	/* Clear the capture flag */
	CTIMER_ClearStatusFlags(config->base, flags);

	/* Check overflow */
	if ((flags & (kCTIMER_Match0Flag << channel)) != 0U) {
		data->capture.overflowed |= u32_add_overflow(1,
				data->capture.overflow_count, &data->capture.overflow_count);
	}
	
	if ((flags & (kCTIMER_Capture0Flag << channel)) == 0) {
		return;
	}

	/* Get the captured value */
	capture_value = CTIMER_GetCaptureValue(config->base, (ctimer_capture_channel_t)channel);

	if (!data->capture.first_edge_captured) {
		/* First edge captured */
		data->capture.first_edge_captured = true;
		data->capture.first_capture_value = capture_value;
		data->capture.overflow_count = 0;
		data->capture.overflowed = false;
		return;
	}

	/* Second edge captured - calculate the measurement */
	if (data->capture.overflowed) {
		err = -ERANGE;
	} else {
		err = mcux_ctimer_calc_ticks(data->capture.first_capture_value, 
					    capture_value, data->capture.overflow_count, &ticks);
	}

	/* Call the callback */
	if (data->capture.pulse_capture) {
		data->capture.callback(dev, channel, 0, ticks, err, data->capture.user_data);
	} else {
		data->capture.callback(dev, channel, ticks, 0, err, data->capture.user_data);
	}

	/* Prepare for next capture */
	data->capture.overflowed = false;
	data->capture.overflow_count = 0;

	if (data->capture.continuous) {
		if (data->capture.pulse_capture) {
			/* For pulse capture, we need to wait for the next rising edge */
			data->capture.first_edge_captured = false;
		} else {
			/* For period capture, this edge becomes the start of next period */
			data->capture.first_capture_value = capture_value;
		}
	} else {
		/* Single capture mode - disable capture */
		data->capture.first_edge_captured = false;
		mcux_ctimer_disable_capture(dev, channel);
	}
}
#endif /* CONFIG_PWM_CAPTURE */

static int mcux_ctimer_pwm_init(const struct device *dev)
{
	const struct pwm_mcux_ctimer_config *config = dev->config;
	ctimer_config_t pwm_config;
	int err;

	err = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (err) {
		return err;
	}

	if (config->period_channel >= CHANNEL_COUNT) {
		LOG_ERR("invalid period_channel: %d. must be less than %d", config->period_channel,
			CHANNEL_COUNT);
		return -EINVAL;
	}

	CTIMER_GetDefaultConfig(&pwm_config);
	pwm_config.prescale = config->prescale;

	CTIMER_Init(config->base, &pwm_config);

#ifdef CONFIG_PWM_CAPTURE

    /*  CtimerInp8 connect to Timer0Captsel 0 */
    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, 0U, kINPUTMUX_CtimerInp8ToTimer0Captsel);
	if (config->irq_config_func) {
		config->irq_config_func(dev);
	}
#endif /* CONFIG_PWM_CAPTURE */

	return 0;
}

static DEVICE_API(pwm, pwm_mcux_ctimer_driver_api) = {
	.set_cycles = mcux_ctimer_pwm_set_cycles,
	.get_cycles_per_sec = mcux_ctimer_pwm_get_cycles_per_sec,
#ifdef CONFIG_PWM_CAPTURE
	.configure_capture = mcux_ctimer_configure_capture,
	.enable_capture = mcux_ctimer_enable_capture,
	.disable_capture = mcux_ctimer_disable_capture,
#endif /* CONFIG_PWM_CAPTURE */
};

#define PWM_MCUX_CTIMER_PINCTRL_DEFINE(n) PINCTRL_DT_INST_DEFINE(n);
#define PWM_MCUX_CTIMER_PINCTRL_INIT(n)   .pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),

#ifdef CONFIG_PWM_CAPTURE
#define CTIMER_CONFIG_FUNC(n) \
static void mcux_ctimer_config_func_##n(const struct device *dev) \
{ \
	IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), \
		    mcux_ctimer_isr, DEVICE_DT_INST_GET(n), 0); \
	irq_enable(DT_INST_IRQN(n)); \
}
#define CTIMER_CFG_CAPTURE_INIT(n) \
	.irq_config_func = mcux_ctimer_config_func_##n
#define CTIMER_INIT_CFG(n)	CTIMER_DECLARE_CFG(n, CTIMER_CFG_CAPTURE_INIT(n))
#else /* !CONFIG_PWM_CAPTURE */
#define CTIMER_CONFIG_FUNC(n)
#define CTIMER_CFG_CAPTURE_INIT
#define CTIMER_INIT_CFG(n)	CTIMER_DECLARE_CFG(n, CTIMER_CFG_CAPTURE_INIT)
#endif /* !CONFIG_PWM_CAPTURE */

#define CTIMER_DECLARE_CFG(n, CAPTURE_INIT) \
	static const struct pwm_mcux_ctimer_config pwm_mcux_ctimer_config_##n = { \
		.base = (CTIMER_Type *)DT_INST_REG_ADDR(n), \
		.prescale = DT_INST_PROP(n, prescaler), \
		.clock_control = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)), \
		.clock_id = (clock_control_subsys_t)(DT_INST_CLOCKS_CELL(n, name)), \
		PWM_MCUX_CTIMER_PINCTRL_INIT(n) \
		CAPTURE_INIT \
	}

#define PWM_MCUX_CTIMER_DEVICE_INIT_MCUX(n) \
	static struct pwm_mcux_ctimer_data pwm_mcux_ctimer_data_##n = { \
		.channel_states =                                                                  \
			{                                                                          \
				[kCTIMER_Match_0] = {.role = PWM_CTIMER_CHANNEL_ROLE_NONE,         \
						     .cycles = 0},                                 \
				[kCTIMER_Match_1] = {.role = PWM_CTIMER_CHANNEL_ROLE_NONE,         \
						     .cycles = 0},                                 \
				[kCTIMER_Match_2] = {.role = PWM_CTIMER_CHANNEL_ROLE_NONE,         \
						     .cycles = 0},                                 \
				[kCTIMER_Match_3] = {.role = PWM_CTIMER_CHANNEL_ROLE_NONE,         \
						     .cycles = 0},                                 \
			},                                                                         \
		.current_period_channel = kCTIMER_Match_0,                                         \
		.is_period_channel_set = false,                                                    \
	};	\
	PWM_MCUX_CTIMER_PINCTRL_DEFINE(n) \
	CTIMER_CONFIG_FUNC(n) \
	CTIMER_INIT_CFG(n); \
	DEVICE_DT_INST_DEFINE(n, mcux_ctimer_pwm_init, NULL, &pwm_mcux_ctimer_data_##n, \
			      &pwm_mcux_ctimer_config_##n, POST_KERNEL, \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &pwm_mcux_ctimer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_MCUX_CTIMER_DEVICE_INIT_MCUX)
