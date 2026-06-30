/*
 * SPDX-FileCopyrightText: 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr clock_monitor back-end for the NXP FREQME (Frequency Measurement)
 * module. Built on the FREQME frequency-measurement operate mode, it exposes
 * both clock_monitor abstraction modes:
 *
 *   - MEASURE: one-shot measurement; the result-ready interrupt delivers a
 *     MEASURE_DONE event and get_rate() returns the measured frequency.
 *   - WINDOW:  continuous measurement against MIN/MAX thresholds derived from
 *     the expected frequency and tolerance; the over-/under-range interrupts
 *     deliver FREQ_HIGH / FREQ_LOW events.
 *
 * Reference and target clocks are routed through the INPUTMUX peripheral; the
 * reference frequency is queried from clock_control and the peripheral gate is
 * enabled through clock_control.
 */

#define DT_DRV_COMPAT nxp_freqme

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_monitor.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <fsl_freqme.h>
#include <fsl_inputmux.h>

LOG_MODULE_REGISTER(clock_monitor_nxp_freqme, CONFIG_CLOCK_MONITOR_LOG_LEVEL);

/* Measurement result counter is 31 bits; the formula offset is +2. */
#define FREQME_RESULT_MAX  0x7FFFFFFFU
/* HW formula offset: Ftarget = (RESULT - 2) * Fref / 2^REF_SCALE. RESULT < 2
 * means no edges counted (clock lost). Fixed by silicon.
 */
#define FREQME_RESULT_BIAS 2U
/* WINDOW guard band (counts) added each side of MIN/MAX to absorb +/-1 LSB
 * quantization plus nominal truncation, so an in-tolerance clock isn't flagged.
 */
#define FREQME_WINDOW_MARGIN 2U

struct nxp_freqme_inputmux_entry {
	INPUTMUX_Type *base;
	uint16_t channel;
	uint32_t connection;
};

struct nxp_freqme_config {
	FREQME_Type *base;
	/* Peripheral gate clock (clocks "ipg"), enabled via clock_control_on(). */
	const struct device    *gate_clk_dev;
	clock_control_subsys_t  gate_clk_subsys;
	/* Reference timebase clock (clocks "reference"), queried for its rate. */
	const struct device    *ref_clk_dev;
	clock_control_subsys_t  ref_clk_subsys;
	/* Optional measured clock (clocks "monitored"); WINDOW mode uses it to
	 * auto-derive the expected frequency. NULL when not provided.
	 */
	const struct device    *mon_clk_dev;
	clock_control_subsys_t  mon_clk_subsys;
	const struct nxp_freqme_inputmux_entry *inputmux_entries;
	uint8_t inputmux_entries_count;
	void (*irq_config_func)(const struct device *dev);
};

enum nxp_freqme_state {
	NXP_FREQME_STATE_IDLE = 0,
	/* configure() is between its lock-free clock query / HAL init and its
	 * commit; concurrent configure()/start() get -EBUSY.
	 */
	NXP_FREQME_STATE_CONFIGURING,
	NXP_FREQME_STATE_CONFIGURED,
	NXP_FREQME_STATE_RUNNING,
};

struct nxp_freqme_data {
	/* Protects the state machine, cfg and result fields against the ISR
	 * and against concurrent API calls.
	 */
	struct k_spinlock lock;
	enum nxp_freqme_state state;
	struct clock_monitor_config cfg;
	uint32_t ref_hz;     /* cached from clock_control at configure */
	uint8_t  ref_scale;  /* cached REF_SCALE for ISR formula / re-arm */
	/* Most recent completed MEASURE result; retained across reads. */
	uint32_t last_rate_hz;
	bool has_rate;
	/* CLOCK_LOST latched since configure(). */
	bool clock_lost;
};

/*
 * Convert a measurement window (ns) into the REF_SCALE power-of-two exponent.
 * The FREQME window is 2^REF_SCALE reference-clock periods, so this is a
 * rounded base-2 logarithm of the requested cycle count, clamped to [0, 31].
 */
static int freqme_window_to_ref_scale(uint32_t window_ns, uint32_t ref_hz, uint8_t *out)
{
	uint64_t cycles = (uint64_t)window_ns * (uint64_t)ref_hz / 1000000000ULL;

	if (cycles == 0U) {
		return -ERANGE;
	}

	uint32_t s = 0U;
	uint64_t v = cycles;

	while ((v >> 1) != 0U) {
		v >>= 1;
		s++;
	}

	/* Round to the nearer power of two. */
	if (s < 31U && (cycles - (1ULL << s)) > ((1ULL << (s + 1U)) - cycles)) {
		s++;
	}
	if (s > 31U) {
		/* REF_SCALE is a 5-bit field (max 31); a window this long cannot
		 * be represented. Reject rather than silently shorten it.
		 */
		return -ERANGE;
	}

	*out = (uint8_t)s;
	return 0;
}

/*
 * Derive the MIN/MAX result-counter thresholds for WINDOW mode. The expected
 * target frequency comes from cfg->window.expected_hz, or is auto-derived from
 * the monitored clock when that is 0. RESULT = round(f * 2^scale / ref) +
 * FREQME_RESULT_BIAS, so the nominal count is scaled by (1 +/- tol_ppm) and
 * biased; a small margin absorbs quantization jitter.
 *
 * Returns -EINVAL if the monitored clock is missing / unreadable, or -ERANGE
 * if the thresholds cannot be represented.
 */
static int freqme_compute_thresholds(const struct nxp_freqme_config *config,
				     const struct clock_monitor_config *cfg,
				     uint8_t ref_scale, uint32_t ref_hz,
				     uint32_t *min_val, uint32_t *max_val)
{
	uint32_t expected_hz = cfg->window.expected_hz;
	uint32_t tol_ppm = cfg->window.tolerance_ppm;

	if (expected_hz == 0U) {
		/* Auto-derive from the monitored clock. */
		int ret;

		if (config->mon_clk_dev == NULL) {
			return -EINVAL;
		}
		ret = clock_control_get_rate(config->mon_clk_dev,
					     config->mon_clk_subsys, &expected_hz);
		if (ret != 0 || expected_hz == 0U) {
			return -EINVAL;
		}
	}

	uint64_t scale = 1ULL << ref_scale;
	uint64_t nominal = (uint64_t)expected_hz * scale / (uint64_t)ref_hz;

	uint64_t hi = nominal * (1000000ULL + tol_ppm) / 1000000ULL
		      + FREQME_RESULT_BIAS + FREQME_WINDOW_MARGIN;
	uint64_t lo = nominal * (1000000ULL - tol_ppm) / 1000000ULL
		      + FREQME_RESULT_BIAS;

	if (hi > FREQME_RESULT_MAX) {
		LOG_ERR("FREQME WINDOW MAX %llu exceeds HW limit (expected_hz=%u, "
			"tol_ppm=%u, ref_scale=%u, ref_hz=%u)", hi, expected_hz,
			tol_ppm, ref_scale, ref_hz);
		return -ERANGE;
	}

	lo = (lo > FREQME_WINDOW_MARGIN) ? (lo - FREQME_WINDOW_MARGIN) : 0ULL;
	if (lo < FREQME_RESULT_BIAS) {
		lo = FREQME_RESULT_BIAS;
	}
	if (lo >= hi) {
		LOG_ERR("FREQME WINDOW thresholds collapse (min=%llu, max=%llu)", lo, hi);
		return -ERANGE;
	}

	*min_val = (uint32_t)lo;
	*max_val = (uint32_t)hi;
	return 0;
}

static void freqme_setup_inputmux(const struct nxp_freqme_config *config)
{
	for (uint8_t i = 0U; i < config->inputmux_entries_count; i++) {
		const struct nxp_freqme_inputmux_entry *e = &config->inputmux_entries[i];

		INPUTMUX_Init(e->base);
		INPUTMUX_AttachSignal(e->base, e->channel,
				      (inputmux_connection_t)e->connection);
		INPUTMUX_Deinit(e->base);
	}
}

static int nxp_freqme_configure(const struct device *dev,
				const struct clock_monitor_config *cfg)
{
	const struct nxp_freqme_config *config = dev->config;
	struct nxp_freqme_data *data = dev->data;
	k_spinlock_key_t key;
	enum nxp_freqme_state prev_state;
	int ret;

	/* Input-only validation, no state needed. */
	if (cfg->mode != CLOCK_MONITOR_MODE_MEASURE &&
	    cfg->mode != CLOCK_MONITOR_MODE_WINDOW) {
		return -ENOTSUP;
	}

	if (cfg->mode == CLOCK_MONITOR_MODE_MEASURE) {
		if (cfg->measure.window_ns == 0U) {
			return -EINVAL;
		}
	} else {
		if (cfg->window.window_ns == 0U ||
		    cfg->window.tolerance_ppm >= 1000000U) {
			return -EINVAL;
		}
	}

	/* Claim the configure transaction: CONFIGURING makes concurrent
	 * configure()/start() callers fail with -EBUSY while the clock query
	 * and HAL init below run outside the spinlock.
	 */
	key = k_spin_lock(&data->lock);
	if (data->state == NXP_FREQME_STATE_RUNNING ||
	    data->state == NXP_FREQME_STATE_CONFIGURING) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}
	prev_state = data->state;
	data->state = NXP_FREQME_STATE_CONFIGURING;
	k_spin_unlock(&data->lock, key);

	uint32_t ref_hz = 0U;

	ret = clock_control_get_rate(config->ref_clk_dev, config->ref_clk_subsys, &ref_hz);
	if (ret != 0 || ref_hz == 0U) {
		ret = -EIO;
		goto restore;
	}

	uint32_t window_ns = (cfg->mode == CLOCK_MONITOR_MODE_MEASURE)
				     ? cfg->measure.window_ns
				     : cfg->window.window_ns;
	uint8_t ref_scale;

	ret = freqme_window_to_ref_scale(window_ns, ref_hz, &ref_scale);
	if (ret != 0) {
		LOG_ERR("window_ns=%u out of range for ref_hz=%u", window_ns, ref_hz);
		ret = -EINVAL;
		goto restore;
	}

	uint32_t min_val = 0U;
	uint32_t max_val = FREQME_RESULT_MAX;
	uint32_t int_mask;

	if (cfg->mode == CLOCK_MONITOR_MODE_WINDOW) {
		ret = freqme_compute_thresholds(config, cfg, ref_scale, ref_hz,
						&min_val, &max_val);
		if (ret != 0) {
			ret = -EINVAL;
			goto restore;
		}
		int_mask = (uint32_t)kFREQME_OverflowInterruptEnable |
			   (uint32_t)kFREQME_UnderflowInterruptEnable;
	} else {
		int_mask = (uint32_t)kFREQME_ReadyInterruptEnable;
	}

	freq_measure_config_t hal_cfg;

	FREQME_GetDefaultConfig(&hal_cfg);
	hal_cfg.operateMode = kFREQME_FreqMeasurementMode;
	hal_cfg.operateModeAttribute.refClkScaleFactor = ref_scale;
	hal_cfg.enableContinuousMode = (cfg->mode == CLOCK_MONITOR_MODE_WINDOW);
	hal_cfg.startMeasurement = false;

	FREQME_Init(config->base, &hal_cfg);
	FREQME_SetMinExpectedValue(config->base, min_val);
	FREQME_SetMaxExpectedValue(config->base, max_val);
	FREQME_EnableInterrupts(config->base, int_mask);

	key = k_spin_lock(&data->lock);
	data->cfg = *cfg;
	data->ref_hz = ref_hz;
	data->ref_scale = ref_scale;
	data->last_rate_hz = 0U;
	data->has_rate = false;
	data->clock_lost = false;
	data->state = NXP_FREQME_STATE_CONFIGURED;
	k_spin_unlock(&data->lock, key);
	return 0;

restore:
	/* Hardware untouched: the previous configuration is still valid. */
	key = k_spin_lock(&data->lock);
	data->state = prev_state;
	k_spin_unlock(&data->lock, key);
	return ret;
}

static int nxp_freqme_start(const struct device *dev)
{
	const struct nxp_freqme_config *config = dev->config;
	struct nxp_freqme_data *data = dev->data;
	k_spinlock_key_t key;

	key = k_spin_lock(&data->lock);
	if (data->state == NXP_FREQME_STATE_RUNNING ||
	    data->state == NXP_FREQME_STATE_CONFIGURING) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}
	if (data->state != NXP_FREQME_STATE_CONFIGURED) {
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	/* State transition and HW kick share one critical section so the
	 * completion ISR can never observe RUNNING hardware with a
	 * not-yet-RUNNING state machine.
	 */
	data->state = NXP_FREQME_STATE_RUNNING;
	if (data->cfg.mode == CLOCK_MONITOR_MODE_WINDOW) {
		FREQME_EnableContinuousMode(config->base, true);
	}
	FREQME_StartMeasurementCycle(config->base);
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int nxp_freqme_stop(const struct device *dev)
{
	const struct nxp_freqme_config *config = dev->config;
	struct nxp_freqme_data *data = dev->data;
	k_spinlock_key_t key;

	key = k_spin_lock(&data->lock);
	if (data->state == NXP_FREQME_STATE_RUNNING) {
		if (data->cfg.mode == CLOCK_MONITOR_MODE_WINDOW) {
			FREQME_EnableContinuousMode(config->base, false);
		}
		FREQME_TerminateMeasurementCycle(config->base);
		data->state = NXP_FREQME_STATE_CONFIGURED;
	}
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int nxp_freqme_get_rate(const struct device *dev, uint32_t *rate_hz)
{
	struct nxp_freqme_data *data = dev->data;
	k_spinlock_key_t key;
	int ret;

	key = k_spin_lock(&data->lock);
	if (data->cfg.mode == CLOCK_MONITOR_MODE_WINDOW) {
		ret = -EAGAIN;
	} else if (data->clock_lost) {
		ret = -EIO;
	} else if (data->has_rate) {
		*rate_hz = data->last_rate_hz;
		ret = 0;
	} else {
		ret = -EAGAIN;
	}
	k_spin_unlock(&data->lock, key);
	return ret;
}

static int nxp_freqme_set_source(const struct device *dev, uint32_t reference, uint32_t target)
{
	const struct nxp_freqme_config *config = dev->config;
	struct nxp_freqme_data *data = dev->data;
	k_spinlock_key_t key;
	const struct nxp_freqme_inputmux_entry *ref_entry;
	const struct nxp_freqme_inputmux_entry *tar_entry;

	if (reference == 0U || target == 0U) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	if (data->state == NXP_FREQME_STATE_RUNNING ||
	    data->state == NXP_FREQME_STATE_CONFIGURING) {
		k_spin_unlock(&data->lock, key);
		return -EBUSY;
	}
	data->state = NXP_FREQME_STATE_CONFIGURING;
	k_spin_unlock(&data->lock, key);

	ref_entry = &config->inputmux_entries[0];
	tar_entry = &config->inputmux_entries[1];

	INPUTMUX_Init(ref_entry->base);
	INPUTMUX_AttachSignal(ref_entry->base, ref_entry->channel,
			      (inputmux_connection_t)reference);
	INPUTMUX_Deinit(ref_entry->base);

	INPUTMUX_Init(tar_entry->base);
	INPUTMUX_AttachSignal(tar_entry->base, tar_entry->channel,
			      (inputmux_connection_t)target);
	INPUTMUX_Deinit(tar_entry->base);

	/* Drop the cached configuration to IDLE so a fresh configure() must run
	 * before the next start().
	 */
	key = k_spin_lock(&data->lock);
	data->state = NXP_FREQME_STATE_IDLE;
	data->ref_hz = 0U;
	data->ref_scale = 0U;
	data->last_rate_hz = 0U;
	data->has_rate = false;
	data->clock_lost = false;
	k_spin_unlock(&data->lock, key);
	return 0;
}

static void nxp_freqme_isr(const struct device *dev)
{
	const struct nxp_freqme_config *config = dev->config;
	struct nxp_freqme_data *data = dev->data;
	uint32_t flags = FREQME_GetInterruptStatusFlags(config->base);
	uint32_t evts = 0U;
	uint32_t rate = 0U;
	clock_monitor_callback_t cb = NULL;
	void *user_data = NULL;
	k_spinlock_key_t key;

	FREQME_ClearInterruptStatusFlags(config->base, flags);

	key = k_spin_lock(&data->lock);

	/* A pending interrupt may run after stop() (or a fresh configure())
	 * has already left the RUNNING state; discard it.
	 */
	if (data->state != NXP_FREQME_STATE_RUNNING) {
		k_spin_unlock(&data->lock, key);
		return;
	}

	if (data->cfg.mode == CLOCK_MONITOR_MODE_MEASURE) {
		if ((flags & (uint32_t)kFREQME_ReadyInterruptStatusFlag) != 0U) {
			uint32_t result = FREQME_GetMeasurementResult(config->base);

			if (result < FREQME_RESULT_BIAS) {
				data->clock_lost = true;
				evts |= CLOCK_MONITOR_EVT_CLOCK_LOST;
			} else {
				rate = FREQME_CalculateTargetClkFreq(config->base,
								     data->ref_hz);
				data->last_rate_hz = rate;
				data->has_rate = true;
				evts |= CLOCK_MONITOR_EVT_MEASURE_DONE;
			}
			/* Auto-disarm before the callback so it may restart. */
			data->state = NXP_FREQME_STATE_CONFIGURED;
		}
	} else {
		if ((flags & (uint32_t)kFREQME_OverflowInterruptStatusFlag) != 0U) {
			evts |= CLOCK_MONITOR_EVT_FREQ_HIGH;
		}
		if ((flags & (uint32_t)kFREQME_UnderflowInterruptStatusFlag) != 0U) {
			evts |= CLOCK_MONITOR_EVT_FREQ_LOW;
		}
		/* An out-of-range result auto-clears CONTINUOUS_MODE_EN in
		 * hardware; re-arm so monitoring continues.
		 */
		if (evts != 0U) {
			FREQME_EnableContinuousMode(config->base, true);
			FREQME_StartMeasurementCycle(config->base);
		}
	}

	if (evts != 0U) {
		cb = data->cfg.callback;
		user_data = data->cfg.user_data;
	}
	k_spin_unlock(&data->lock, key);

	if (cb != NULL && evts != 0U) {
		struct clock_monitor_event_data evt = {
			.events = evts,
			.measured_hz = rate,
		};
		cb(dev, &evt, user_data);
	}
}

static int nxp_freqme_init(const struct device *dev)
{
	const struct nxp_freqme_config *config = dev->config;
	struct nxp_freqme_data *data = dev->data;
	int ret;

	data->state = NXP_FREQME_STATE_IDLE;

	if (!device_is_ready(config->gate_clk_dev)) {
		LOG_ERR("%s: gate clock device not ready", dev->name);
		return -ENODEV;
	}

	ret = clock_control_on(config->gate_clk_dev, config->gate_clk_subsys);
	if (ret != 0) {
		LOG_ERR("%s: failed to enable gate clock (%d)", dev->name, ret);
		return ret;
	}

	ret = clock_control_on(config->ref_clk_dev, config->ref_clk_subsys);
	if (ret != 0) {
		LOG_ERR("%s: failed to route reference clock (%d)", dev->name, ret);
		return ret;
	}
	if (config->mon_clk_dev != NULL) {
		ret = clock_control_on(config->mon_clk_dev, config->mon_clk_subsys);
		if (ret != 0) {
			LOG_WRN("%s: failed to route monitored clock (%d)", dev->name, ret);
		}
	}

	freqme_setup_inputmux(config);

	config->irq_config_func(dev);
	return 0;
}

static DEVICE_API(clock_monitor, nxp_freqme_api) = {
	.configure  = nxp_freqme_configure,
	.start      = nxp_freqme_start,
	.stop       = nxp_freqme_stop,
	.get_rate   = nxp_freqme_get_rate,
	.set_source = nxp_freqme_set_source,
};

#define NXP_FREQME_INPUTMUX_ENTRY(node_id, prop, idx)                          \
	{                                                                      \
		.base = (INPUTMUX_Type *)DT_REG_ADDR(                          \
			DT_PHANDLE_BY_IDX(node_id, prop, idx)),                \
		.channel = (uint16_t)DT_PHA_BY_IDX(node_id, prop, idx, channel), \
		.connection =                                                  \
			(uint32_t)DT_PHA_BY_IDX(node_id, prop, idx, connection), \
	}

#define NXP_FREQME_MON_CLK_DEV(inst)                                           \
	COND_CODE_1(DT_INST_CLOCKS_HAS_NAME(inst, monitored),                  \
		    (DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_NAME(inst, monitored))), \
		    (NULL))

#define NXP_FREQME_MON_CLK_SUBSYS(inst)                                        \
	COND_CODE_1(DT_INST_CLOCKS_HAS_NAME(inst, monitored),                  \
		    ((clock_control_subsys_t)(uintptr_t)                       \
			     DT_INST_CLOCKS_CELL_BY_NAME(inst, monitored, name)), \
		    ((clock_control_subsys_t)0))

#define NXP_FREQME_DEVICE_INIT(inst)                                           \
	static void nxp_freqme_irq_cfg_##inst(const struct device *dev)        \
	{                                                                      \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),   \
			    nxp_freqme_isr, DEVICE_DT_INST_GET(inst), 0);      \
		irq_enable(DT_INST_IRQN(inst));                                \
	}                                                                      \
	static const struct nxp_freqme_inputmux_entry                          \
		nxp_freqme_inputmux_##inst[] = {                               \
		DT_INST_FOREACH_PROP_ELEM_SEP(inst, inputmux_connections,      \
					      NXP_FREQME_INPUTMUX_ENTRY, (,))  \
	};                                                                     \
	static struct nxp_freqme_data nxp_freqme_data_##inst;                  \
	static const struct nxp_freqme_config nxp_freqme_cfg_##inst = {        \
		.base = (FREQME_Type *)DT_INST_REG_ADDR(inst),                 \
		.gate_clk_dev =                                                \
			DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_NAME(inst, ipg)), \
		.gate_clk_subsys = (clock_control_subsys_t)(uintptr_t)         \
			DT_INST_CLOCKS_CELL_BY_NAME(inst, ipg, name),          \
		.ref_clk_dev = DEVICE_DT_GET(                                  \
			DT_INST_CLOCKS_CTLR_BY_NAME(inst, reference)),         \
		.ref_clk_subsys = (clock_control_subsys_t)(uintptr_t)          \
			DT_INST_CLOCKS_CELL_BY_NAME(inst, reference, name),    \
		.mon_clk_dev = NXP_FREQME_MON_CLK_DEV(inst),                   \
		.mon_clk_subsys = NXP_FREQME_MON_CLK_SUBSYS(inst),             \
		.inputmux_entries = nxp_freqme_inputmux_##inst,                \
		.inputmux_entries_count =                                      \
			DT_INST_PROP_LEN(inst, inputmux_connections),          \
		.irq_config_func = nxp_freqme_irq_cfg_##inst,                  \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(inst, nxp_freqme_init, NULL,                     \
			      &nxp_freqme_data_##inst, &nxp_freqme_cfg_##inst, \
			      POST_KERNEL, CONFIG_CLOCK_MONITOR_INIT_PRIORITY, \
			      &nxp_freqme_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_FREQME_DEVICE_INIT)
