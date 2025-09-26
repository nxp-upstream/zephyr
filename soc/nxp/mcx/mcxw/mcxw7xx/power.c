/*
 * Copyright (c) 2025 NXP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <soc.h>

#include <fsl_cmc.h>

LOG_MODULE_REGISTER(soc, CONFIG_SOC_LOG_LEVEL);

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
    cmc_power_domain_config_t config;
    switch(state)
    {
        case PM_STATE_RUNTIME_IDLE:
        {
            config.main_domain = kCMC_SleepMode;
            if (substate_id == 1U) {
                config.wake_domain = kCMC_ActiveMode;
                config.clock_mode = kCMC_GateCoreClock;
            } else if (substate_id == 2U) {
                config.wake_domain = kCMC_SleepMode;
                config.clock_mode = kCMC_GateAllSystemClocksEnterLowPowerMode;
            } else {
                LOG_DBG("Unsupported substate id %u of power state %u", substate_id, state);
            }
            break;
        }
        case PM_STATE_SUSPEND_TO_IDLE:
        {
            config.main_domain = kCMC_DeepSleepMode;
            config.clock_mode = kCMC_GateAllSystemClocksEnterLowPowerMode;
            if (substate_id == 1U) {
                config.wake_domain = kCMC_SleepMode;
            } else if (substate_id == 2U) {
                config.wake_domain = kCMC_DeepSleepMode;
            } else {
                LOG_DBG("Unsupported substate id %u of power state %u", substate_id, state);
            }
            break;
        }
        case PM_STATE_SOFT_OFF:
        {
            config.main_domain = kCMC_DeepPowerDown;
            config.wake_domain = kCMC_DeepPowerDown;
            config.clock_mode = kCMC_GateAllSystemClocksEnterLowPowerMode;
            break;
        }
        default:
        {
            LOG_DBG("Unsupported power state %u", state);
            break;
        }
    }

    CMC_SetPowerModeProtection(CMC0, ((uint32_t)(config.main_domain) | (uint32_t)(config.wake_domain)));
    CMC_EnterLowPowerMode(CMC0, &config);
    
    /* Wakeup from Sleep/Deep Sleep. */
    CMC_SetPowerModeProtection(CMC0, 0UL);
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
    
}
