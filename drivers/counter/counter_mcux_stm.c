/*
 * Copyright (c) 2025 NXP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_stm

#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/irq.h>
#include <fsl_stm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>

LOG_MODULE_REGISTER(mcux_stm, CONFIG_COUNTER_LOG_LEVEL);

#define DEV_CFG(_dev) ((const struct mcux_stm_config *)(_dev)->config)
#define DEV_DATA(_dev) ((struct mcux_stm_data *)(_dev)->data)

// Configuration structure for the STM driver
struct mcux_stm_config {
    // Counter configuration information
    struct counter_config_info info;

    // Memory - mapped I/O region for the STM
    DEVICE_MMIO_NAMED_ROM(stm_mmio);

    // Clock control device
    const struct device *clock_dev;
    // Clock control subsystem
    clock_control_subsys_t clock_subsys;
    // Clock source
    clock_name_t clock_source;
    // Function to configure the interrupt
    void (*irq_config_func)(void);
};

// Data structure for the STM driver
struct mcux_stm_data {
    // Memory - mapped I/O region for the STM (RAM)
    DEVICE_MMIO_NAMED_RAM(stm_mmio);
    // Alarm callback function
    counter_alarm_callback_t alarm_callback;
    // User data for the alarm callback
    void *alarm_user_data;
};

// Get the base address of the STM peripheral
static STM_Type *get_base(const struct device *dev)
{
    return (STM_Type *)DEVICE_MMIO_NAMED_GET(dev, stm_mmio);
}

// Start the STM timer
static int mcux_stm_start(const struct device *dev)
{
    STM_Type *base = get_base(dev);
    STM_StartTimer(base);
    return 0;
}

// Stop the STM timer
static int mcux_stm_stop(const struct device *dev)
{
    STM_Type *base = get_base(dev);
    STM_StopTimer(base);
    return 0;
}

// Get the current timer count value
static int mcux_stm_get_value(const struct device *dev, uint32_t *ticks)
{
    STM_Type *base = get_base(dev);
    *ticks = STM_GetTimerCount(base);
    return 0;
}

// Set an alarm for the STM
static int mcux_stm_set_alarm(const struct device *dev, uint8_t chan_id,
                              const struct counter_alarm_cfg *alarm_cfg)
{
    STM_Type *base = get_base(dev);
    struct mcux_stm_data *data = dev->data;
    uint32_t current = STM_GetTimerCount(base);
    uint32_t ticks = alarm_cfg->ticks;

    // Check if the channel ID is valid
    if (chan_id >= 4) {
        LOG_ERR("Invalid channel id");
        return -EINVAL;
    }

    // Calculate the absolute tick value if the alarm is relative
    if ((alarm_cfg->flags & COUNTER_ALARM_CFG_ABSOLUTE) == 0) {
        ticks += current;
    }

    // Check if an alarm is already set
    if (data->alarm_callback) {
        return -EBUSY;
    }

    // Set the alarm callback and user data
    data->alarm_callback = alarm_cfg->callback;
    data->alarm_user_data = alarm_cfg->user_data;

    // Set the compare value and enable the channel
    STM_SetCompare(base, (stm_channel_t)chan_id, ticks);
    STM_EnableCompareChannel(base, (stm_channel_t)chan_id);

    return 0;
}

// Cancel an alarm for the STM
static int mcux_stm_cancel_alarm(const struct device *dev, uint8_t chan_id)
{
    STM_Type *base = get_base(dev);
    struct mcux_stm_data *data = dev->data;

    // Check if the channel ID is valid
    if (chan_id >= 4) {
        LOG_ERR("Invalid channel id");
        return -EINVAL;
    }

    // Disable the channel and clear the alarm callback
    STM_DisableCompareChannel(base, (stm_channel_t)chan_id);
    data->alarm_callback = NULL;

    return 0;
}

// Interrupt service routine for the STM
void mcux_stm_isr(const struct device *dev)
{
    STM_Type *base = get_base(dev);
    struct mcux_stm_data *data = dev->data;
    uint32_t current = STM_GetTimerCount(base);
    uint32_t status;

    // Check the status flags for each channel
    for (int i = 0; i < 4; i++) {
        status = STM_GetStatusFlags(base, (stm_channel_t)i);
        if (status & STM_CIR_CIF_MASK) {
            // Clear the status flag
            STM_ClearStatusFlags(base, (stm_channel_t)i);
            barrier_dsync_fence_full();

            // Call the alarm callback if set
            if (data->alarm_callback) {
                STM_DisableCompareChannel(base, (stm_channel_t)i);
                counter_alarm_callback_t alarm_cb = data->alarm_callback;
                data->alarm_callback = NULL;
                alarm_cb(dev, i, current, data->alarm_user_data);
            }
        }
    }
}

// Get the pending interrupts
static uint32_t mcux_stm_get_pending_int(const struct device *dev)
{
    STM_Type *base = get_base(dev);
    uint32_t pending = 0;
    for (int i = 0; i < 4; i++) {
        pending |= STM_GetStatusFlags(base, (stm_channel_t)i);
    }
    return pending;
}

// Set the top value (not supported)
static int mcux_stm_set_top_value(const struct device *dev,
                                  const struct counter_top_cfg *cfg)
{
    LOG_ERR("Setting top value is not supported");
    return -ENOTSUP;
}

// Get the top value (return maximum 32 - bit value as a placeholder)
static uint32_t mcux_stm_get_top_value(const struct device *dev)
{
    return UINT32_MAX;
}

// Get the frequency of the STM
static uint32_t mcux_stm_get_freq(const struct device *dev)
{
    const struct mcux_stm_config *config = dev->config;
    uint32_t clock_freq;

    // Check if the clock device is ready
    if (!device_is_ready(config->clock_dev)) {
        LOG_ERR("clock control device not ready");
        return 0;
    }

    // Get the clock frequency
    if (clock_control_get_rate(config->clock_dev, config->clock_subsys,
                               &clock_freq)) {
        return 0;
    }

    // Calculate the frequency based on the prescale value
    stm_config_t stm_cfg;
    STM_GetDefaultConfig(&stm_cfg);
    return clock_freq / (stm_cfg.prescale + 1);
}

// Initialize the STM driver
static int mcux_stm_init(const struct device *dev)
{
    const struct mcux_stm_config *config = dev->config;
    stm_config_t stmConfig;
    uint32_t clock_freq;
    STM_Type *base;

    // Map the memory - mapped I/O region
    DEVICE_MMIO_NAMED_MAP(dev, stm_mmio, K_MEM_CACHE_NONE | K_MEM_DIRECT_MAP);

    // Check if the clock device is ready
    if (!device_is_ready(config->clock_dev)) {
        LOG_ERR("clock control device not ready");
        return -ENODEV;
    }

    // Get the clock frequency
    if (clock_control_get_rate(config->clock_dev, config->clock_subsys,
                               &clock_freq)) {
        return -EINVAL;
    }

    // Get the default STM configuration
    STM_GetDefaultConfig(&stmConfig);
    stmConfig.prescale = clock_freq / config->info.freq - 1;

    base = get_base(dev);
    // Initialize the STM
    STM_Init(base, &stmConfig);

    // Configure the interrupt
    config->irq_config_func();

    return 0;
}

// Zephyr counter driver API implementation
static const struct counter_driver_api mcux_stm_driver_api = {
    .start = mcux_stm_start,
    .stop = mcux_stm_stop,
    .get_value = mcux_stm_get_value,
    .set_alarm = mcux_stm_set_alarm,
    .cancel_alarm = mcux_stm_cancel_alarm,
    .set_top_value = mcux_stm_set_top_value,
    .get_pending_int = mcux_stm_get_pending_int,
    .get_top_value = mcux_stm_get_top_value,
    .get_freq = mcux_stm_get_freq
};

// Macro to initialize the STM device
#define STM_DEVICE_INIT_MCUX(n)                                           \
    static struct mcux_stm_data mcux_stm_data_ ## n;                      \
    static void mcux_stm_irq_config_ ## n(void);                          \
                                                                        \
    static const struct mcux_stm_config mcux_stm_config_ ## n = {         \
        DEVICE_MMIO_NAMED_ROM_INIT(stm_mmio, DT_DRV_INST(n)),           \
        .clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),              \
        .clock_subsys =                                                 \
            (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, name),        \
        .info = {                                                       \
            .max_top_value = UINT32_MAX,                                \
            .freq = DT_INST_PROP(n, stmfreq),                           \
            .channels = 4,                                              \
            .flags = COUNTER_CONFIG_INFO_COUNT_UP,                      \
        },                                                              \
        .irq_config_func = mcux_stm_irq_config_ ## n,                    \
    };                                                                  \
                                                                        \
    DEVICE_DT_INST_DEFINE(n,                                            \
                          mcux_stm_init,                                \
                          NULL,                                         \
                          &mcux_stm_data_ ## n,                          \
                          &mcux_stm_config_ ## n,                        \
                          POST_KERNEL,                                  \
                          CONFIG_COUNTER_INIT_PRIORITY,                 \
                          &mcux_stm_driver_api);                         \
                                                                        \
    static void mcux_stm_irq_config_ ## n(void)                          \
    {                                                                   \
        IRQ_CONNECT(DT_INST_IRQN(n),                                    \
                    DT_INST_IRQ(n, priority),                           \
                    mcux_stm_isr, DEVICE_DT_INST_GET(n), 0);             \
        irq_enable(DT_INST_IRQN(n));                                    \
    }                                                                   \

// Iterate over all compatible STM devices and initialize them
DT_INST_FOREACH_STATUS_OKAY(STM_DEVICE_INIT_MCUX)
