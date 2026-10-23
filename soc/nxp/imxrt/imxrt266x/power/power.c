/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arch_interface.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/util.h>
#include <cmsis_core.h>

#include "power.h"

LOG_MODULE_REGISTER(soc_power, CONFIG_SOC_LOG_LEVEL);

/* substate-id of a zephyr,power-state node, by node label. */
#define POWER_SUBSTATE_ID(node_label) DT_PROP(DT_NODELABEL(node_label), substate_id)

/* setCoreLevel code of the Deep Sleep VDD_CORE, 0.65 V: 0.225 V + 17 x 25 mV. */
#define DEEP_SLEEP_CORE_LEVEL 0x11U

/* enableSensors mask with every PMU sensor off. */
#define PMU_SENSORS_OFF 0U

/*
 * Per-mode baselines of the HAL configurations. The NPU, COMM and MEDIA domain
 * events and TCMs and the COMM and MEDIA root clocks are not in them: they follow
 * the domains on each entry, see APPLY_VARIABLE_FIELDS(). The WAKE, WAKE_1M and
 * WAKE_2M root clocks stay on in every mode because the wakeup detection runs on
 * them.
 */

/* MCUXpresso SDK power manager, Sleep baseline. */
static const power_sleep_config_t sleep_baseline = {
	.setCpuDomainEvent = kPDCON_EventNoneOrActive,

	.setPmuMode = (uint8_t)kPMU_ModeHP,
	.setDcdcMode = kPMU_DcdcModePWM,
	.setCoreLevel = POWER_PMUCFG_CORELEVEL_080V,
	.setLdo0V8Mode = kPMU_LdoModeHP,
	.setLdo1V8Mode = kPMU_LdoModeHP,
	.setLdoVdda1V8Mode = (uint8_t)kPMU_LdoModeHP,
	.enableHqref = true,
	.enableSensors = PMU_SENSORS_OFF,

	.enableLdoa0V8ClockSource = true,
	.enableFro192MClockSource = true,
	.enableFro12MClockSource = true,
	.enableMainPllClockSource = true,
	.enableCorePllClockSource = true,
	.enableSysPllClockSource = true,
	.enableLdoq0V8ClockSource = true,
	.enableSxoscClockSource = true,
	.enableFro12MLpClockSource = true,

	.enableComputeRootClock = true,
	.enableMainRootClock = true,
	.enableWakeRootClock = true,
	.enableAudioRootClock = true,
	.enableWake1MRootClock = true,
	.enableWake2MRootClock = true,
};

/*
 * The SDK power manager powers down every memory no constraint holds. Zephyr has
 * no such constraint, so that would lose the RAM a suspend-to-idle state resumes
 * with: the memory modes are fixed per mode instead, from the power_mode_switch
 * demo.
 */

/*
 * MCUXpresso SDK power manager, Deep Sleep baseline; memory from the
 * power_mode_switch demo, Deep Sleep 1 column.
 */
static const power_deep_sleep_config_t deep_sleep1_baseline = {
	.setCpuDomainEvent = kPDCON_EventNoneOrActive,

	.setPmuMode = (uint8_t)kPMU_ModeLP,
	.setDcdcMode = kPMU_DcdcModePWM,
	.setCoreLevel = DEEP_SLEEP_CORE_LEVEL,
	.setLdo0V8Mode = kPMU_LdoModeHP,
	.setLdo1V8Mode = kPMU_LdoModeHP,
	.setLdoVdda1V8Mode = (uint8_t)kPMU_LdoModeHP,
	.enableHqref = true,
	.enableSensors = PMU_SENSORS_OFF,

	.enableLdoa0V8ClockSource = true,
	.enableFro192MClockSource = true,
	.enableFro12MClockSource = true,
	.enableMainPllClockSource = false,
	.enableCorePllClockSource = false,
	.enableSysPllClockSource = false,
	.enableLdoq0V8ClockSource = false,
	.enableSxoscClockSource = false,
	.enableFro12MLpClockSource = true,

	.enableComputeRootClock = false,
	.enableMainRootClock = false,
	.enableWakeRootClock = true,
	.enableAudioRootClock = false,
	.enableWake1MRootClock = true,
	.enableWake2MRootClock = true,

	.setWakeSramMode = kMEMCON_PowerModeActive,
	.setComputeSram01Mode = kMEMCON_PowerModeRetention,
	.setOcram0M23Mode = kMEMCON_PowerModeRetention,
	.setOcram0M4Mode = kMEMCON_PowerModeRetention,
	.setOcram0M5Mode = kMEMCON_PowerModeRetention,
	.setOcram1M0Mode = kMEMCON_PowerModeRetention,
	.setOcram1M1Mode = kMEMCON_PowerModeRetention,
	.setOcram2M0Mode = kMEMCON_PowerModeRetention,
	.setOcram2M1Mode = kMEMCON_PowerModeRetention,
	.setLlcCacheMode = kMEMCON_PowerModeRetention,
	.setAudio8KbMode = kMEMCON_PowerModeRetention,
	.setM85TcmMode = kMEMCON_PowerModeRetention,
};

/*
 * The SDK power manager has a single Deep Sleep baseline, the one above. Deep
 * Sleep 2 and 3 are the power_mode_switch demo's experimental columns, and their
 * low-power LDO modes are not confirmed on silicon.
 */

/* power_mode_switch demo, Deep Sleep 2 column. */
static const power_deep_sleep_config_t deep_sleep2_baseline = {
	.setCpuDomainEvent = kPDCON_EventNoneOrActive,

	.setPmuMode = (uint8_t)kPMU_ModeLP,
	.setDcdcMode = kPMU_DcdcModePFM,
	.setCoreLevel = DEEP_SLEEP_CORE_LEVEL,
	.setLdo0V8Mode = kPMU_LdoModeLP,
	.setLdo1V8Mode = kPMU_LdoModeLP,
	.setLdoVdda1V8Mode = (uint8_t)kPMU_LdoModeLP,
	.enableHqref = true,
	.enableSensors = PMU_SENSORS_OFF,

	.enableLdoa0V8ClockSource = false,
	.enableFro192MClockSource = false,
	.enableFro12MClockSource = false,
	.enableMainPllClockSource = false,
	.enableCorePllClockSource = false,
	.enableSysPllClockSource = false,
	.enableLdoq0V8ClockSource = false,
	.enableSxoscClockSource = false,
	.enableFro12MLpClockSource = false,

	.enableComputeRootClock = false,
	.enableMainRootClock = false,
	.enableWakeRootClock = true,
	.enableAudioRootClock = false,
	.enableWake1MRootClock = true,
	.enableWake2MRootClock = true,

	.setWakeSramMode = kMEMCON_PowerModeActive,
	.setComputeSram01Mode = kMEMCON_PowerModeRetention,
	.setOcram0M23Mode = kMEMCON_PowerModePowerDown,
	.setOcram0M4Mode = kMEMCON_PowerModePowerDown,
	.setOcram0M5Mode = kMEMCON_PowerModePowerDown,
	.setOcram1M0Mode = kMEMCON_PowerModePowerDown,
	.setOcram1M1Mode = kMEMCON_PowerModePowerDown,
	.setOcram2M0Mode = kMEMCON_PowerModePowerDown,
	.setOcram2M1Mode = kMEMCON_PowerModePowerDown,
	.setLlcCacheMode = kMEMCON_PowerModeRetention,
	.setAudio8KbMode = kMEMCON_PowerModeRetention,
	.setM85TcmMode = kMEMCON_PowerModeRetention,
};

/* power_mode_switch demo, Deep Sleep 3 column. */
static const power_deep_sleep_config_t deep_sleep3_baseline = {
	.setCpuDomainEvent = kPDCON_EventPowerOff,

	.setPmuMode = (uint8_t)kPMU_ModeLP,
	.setDcdcMode = kPMU_DcdcModePFM,
	.setCoreLevel = DEEP_SLEEP_CORE_LEVEL,
	.setLdo0V8Mode = kPMU_LdoModeLP,
	.setLdo1V8Mode = kPMU_LdoModeLP,
	.setLdoVdda1V8Mode = (uint8_t)kPMU_LdoModeLP,
	.enableHqref = true,
	.enableSensors = PMU_SENSORS_OFF,

	.enableLdoa0V8ClockSource = false,
	.enableFro192MClockSource = false,
	.enableFro12MClockSource = false,
	.enableMainPllClockSource = false,
	.enableCorePllClockSource = false,
	.enableSysPllClockSource = false,
	.enableLdoq0V8ClockSource = false,
	.enableSxoscClockSource = false,
	.enableFro12MLpClockSource = false,

	.enableComputeRootClock = false,
	.enableMainRootClock = false,
	.enableWakeRootClock = true,
	.enableAudioRootClock = false,
	.enableWake1MRootClock = true,
	.enableWake2MRootClock = true,

	.setWakeSramMode = kMEMCON_PowerModeActive,
	.setComputeSram01Mode = kMEMCON_PowerModeRetention,
	.setOcram0M23Mode = kMEMCON_PowerModePowerDown,
	.setOcram0M4Mode = kMEMCON_PowerModePowerDown,
	.setOcram0M5Mode = kMEMCON_PowerModePowerDown,
	.setOcram1M0Mode = kMEMCON_PowerModePowerDown,
	.setOcram1M1Mode = kMEMCON_PowerModePowerDown,
	.setOcram2M0Mode = kMEMCON_PowerModePowerDown,
	.setOcram2M1Mode = kMEMCON_PowerModePowerDown,
	.setLlcCacheMode = kMEMCON_PowerModeRetention,
	.setAudio8KbMode = kMEMCON_PowerModeRetention,
	.setM85TcmMode = kMEMCON_PowerModePowerDown,
};

/* power_mode_switch demo, Power Down column. The HAL sets the domains and clocks. */
static const power_down_config_t power_down_config = {
	.setWakeSramMode = kMEMCON_PowerModeRetention,
	.setComputeSram01Mode = kMEMCON_PowerModeRetention,
};

#define PD_OWNER_BUILT(node_label)                                                                 \
	UTIL_AND(IS_ENABLED(CONFIG_POWER_DOMAIN_NXP_PDCON),                                        \
		 DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(node_label)))

/* A regulator node is enabled even under a disabled pmu node, so both are checked. */
#define LDO_OWNER_BUILT(node_label)                                                                \
	UTIL_AND(UTIL_AND(IS_ENABLED(CONFIG_REGULATOR_NXP_RT2K_PMU),                               \
			  DT_NODE_HAS_STATUS_OKAY(DT_PARENT(DT_NODELABEL(node_label)))),           \
		 DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(node_label)))

/*
 * A resource whose use is unknown stays on -- its state cannot be read, or the
 * driver that tracks it is not built in: better to waste power than to cut a
 * resource in use. The driver is checked at build time, so nothing refers to a
 * device that does not exist.
 */
#define PD_STAYS_ON(node_label)                                                                    \
	COND_CODE_1(PD_OWNER_BUILT(node_label),                                                    \
		    (domain_stays_on(DEVICE_DT_GET(DT_NODELABEL(node_label)))), (true))

#define LDO_REQUESTED(node_label)                                                                  \
	COND_CODE_1(LDO_OWNER_BUILT(node_label),                                                   \
		    (regulator_is_enabled(DEVICE_DT_GET(DT_NODELABEL(node_label)))), (true))

__maybe_unused static bool domain_stays_on(const struct device *domain)
{
	enum pm_device_state state;

	return (pm_device_state_get(domain, &state) != 0) || (state == PM_DEVICE_STATE_ACTIVE);
}

/* The state the variable fields follow, read once so that every field agrees. */
struct resource_state {
	bool npu_domain_on;
	bool comm_domain_on;
	bool media_domain_on;
	bool ldoa0v8_requested;
	bool ldoq0v8_requested;
};

static struct resource_state resource_state_read(void)
{
	return (struct resource_state){
		.npu_domain_on = PD_STAYS_ON(pd_npu),
		.comm_domain_on = PD_STAYS_ON(pd_comm),
		.media_domain_on = PD_STAYS_ON(pd_media),
		.ldoa0v8_requested = LDO_REQUESTED(ldoa0v8),
		.ldoq0v8_requested = LDO_REQUESTED(ldoq0v8),
	};
}

static pdcon_event_t domain_event(bool domain_on)
{
	return domain_on ? kPDCON_EventNoneOrActive : kPDCON_EventPowerOff;
}

/* A domain that is powered off cannot retain its TCM. */
static memcon_power_mode_t tcm_mode(bool domain_on)
{
	return domain_on ? kMEMCON_PowerModeRetention : kMEMCON_PowerModePowerDown;
}

/*
 * power_sleep_config_t and power_deep_sleep_config_t share these field names, so
 * one macro fills both and the compiler still checks every field by name. A
 * requested LDO stays on where its baseline turns it off; one that is not
 * requested keeps its baseline.
 */
#define APPLY_VARIABLE_FIELDS(config, resources)                                                   \
	do {                                                                                       \
		(config)->setNpuDomainEvent = domain_event((resources)->npu_domain_on);            \
		(config)->setCommDomainEvent = domain_event((resources)->comm_domain_on);          \
		(config)->setMediaDomainEvent = domain_event((resources)->media_domain_on);        \
		(config)->enableCommRootClock = (resources)->comm_domain_on;                       \
		(config)->enableMediaRootClock = (resources)->media_domain_on;                     \
		(config)->enableLdoa0V8ClockSource =                                               \
			(config)->enableLdoa0V8ClockSource || (resources)->ldoa0v8_requested;      \
		(config)->enableLdoq0V8ClockSource =                                               \
			(config)->enableLdoq0V8ClockSource || (resources)->ldoq0v8_requested;      \
	} while (false)

static void log_unsupported(enum pm_state state, uint8_t substate_id)
{
	LOG_DBG("Unsupported power state %u, substate %u", state, substate_id);
}

/* POWER_Enter*() return before their WFI when CSSI refuses the low-power entry. */
static void log_if_refused(enum pm_state state, uint8_t substate_id, status_t status)
{
	if (status != kStatus_Success) {
		LOG_ERR("Power state %u, substate %u refused: %d", state, substate_id, status);
	}
}

static void enter_runtime_idle(void)
{
	unsigned int key = arch_pm_state_set_prepare();

	__WFI();

	arch_pm_state_set_finish(key);
}

static status_t enter_sleep(void)
{
	const struct resource_state resources = resource_state_read();
	power_sleep_config_t config = sleep_baseline;
	unsigned int key;
	status_t status;

	APPLY_VARIABLE_FIELDS(&config, &resources);

	key = arch_pm_state_set_prepare();
	status = POWER_EnterSleep(&config);
	arch_pm_state_set_finish(key);

	return status;
}

static void deep_sleep_config_build(power_deep_sleep_config_t *config,
				    const power_deep_sleep_config_t *baseline)
{
	const struct resource_state resources = resource_state_read();

	*config = *baseline;
	APPLY_VARIABLE_FIELDS(config, &resources);
	config->setNpuTcmMode = tcm_mode(resources.npu_domain_on);
	config->setCommTcmMode = tcm_mode(resources.comm_domain_on);
	config->setMediaTcmMode = tcm_mode(resources.media_domain_on);
}

static status_t enter_resuming_deep_sleep(const power_deep_sleep_config_t *baseline)
{
	power_deep_sleep_config_t config;
	unsigned int key;
	status_t status;

	deep_sleep_config_build(&config, baseline);

	key = arch_pm_state_set_prepare();
	status = POWER_EnterDeepSleep(&config);
	arch_pm_state_set_finish(key);

	return status;
}

static status_t enter_deep_sleep3(void)
{
	power_deep_sleep_config_t config;

	deep_sleep_config_build(&config, &deep_sleep3_baseline);

	return POWER_EnterDeepSleep(&config);
}

static status_t enter_deep_power_down1(void)
{
	const bool retain_vbat_sram = true;

	return power_enter_deep_power_down(retain_vbat_sram);
}

/* The modes that resume where they were entered. */
static void enter_suspend_to_idle(uint8_t substate_id)
{
	status_t status;

	switch (substate_id) {
	case POWER_SUBSTATE_ID(sleep):
		status = enter_sleep();
		break;

	case POWER_SUBSTATE_ID(deep_sleep1):
		status = enter_resuming_deep_sleep(&deep_sleep1_baseline);
		break;

	case POWER_SUBSTATE_ID(deep_sleep2):
		status = enter_resuming_deep_sleep(&deep_sleep2_baseline);
		break;

	default:
		log_unsupported(PM_STATE_SUSPEND_TO_IDLE, substate_id);
		return;
	}

	log_if_refused(PM_STATE_SUSPEND_TO_IDLE, substate_id, status);
}

/*
 * The modes that keep part of the RAM but wake through a system reset. These and
 * Deep Power Down 1 keep the PM core's interrupt lock as it is, the way
 * z_sys_poweroff() enters Deep Power Down 2: arch_pm_state_set_prepare() would let
 * an interrupt already pending end the WFI at once, and POWER_EnterPowerDown() and
 * POWER_EnterDeepPowerDown() then spin forever.
 */
static void enter_suspend_to_ram(uint8_t substate_id)
{
	status_t status;

	switch (substate_id) {
	case POWER_SUBSTATE_ID(deep_sleep3):
		status = enter_deep_sleep3();
		break;

	case POWER_SUBSTATE_ID(power_down):
		status = POWER_EnterPowerDown(&power_down_config);
		break;

	default:
		log_unsupported(PM_STATE_SUSPEND_TO_RAM, substate_id);
		return;
	}

	log_if_refused(PM_STATE_SUSPEND_TO_RAM, substate_id, status);
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	switch (state) {
	case PM_STATE_RUNTIME_IDLE:
		enter_runtime_idle();
		break;

	case PM_STATE_SUSPEND_TO_IDLE:
		enter_suspend_to_idle(substate_id);
		break;

	case PM_STATE_SUSPEND_TO_RAM:
		enter_suspend_to_ram(substate_id);
		break;

	case PM_STATE_SUSPEND_TO_DISK:
		log_if_refused(state, substate_id, enter_deep_power_down1());
		break;

	default:
		log_unsupported(state, substate_id);
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	/*
	 * POWER_Enter*() set SLEEPDEEP for their WFI and leave it set. Clearing it
	 * keeps the WFI of the idle thread a core-only sleep.
	 */
	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}
