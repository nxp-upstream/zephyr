#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/printk.h>


const struct device *core_main_pd = DEVICE_DT_GET(DT_NODELABEL(pd_core_main));
const struct device *core_wake_pd = DEVICE_DT_GET(DT_NODELABEL(pd_core_wake));

int main(void)
{
    printk("Power management mode switch sample application started.\n");

    printk("Will enter Deep Sleep1 Mode\r\n");
    pm_device_action_run(core_main_pd, PM_DEVICE_ACTION_SUSPEND);
    pm_device_action_run(core_wake_pd, PM_DEVICE_ACTION_RESUME);
    k_sleep(K_SECONDS(2));
    printk("Wakeup from Deep Sleep1 Mode\r\n");

    // printk("Will enter Deep Sleep2 Mode\r\n");
    // pm_device_action_run(core_main_pd, PM_DEVICE_ACTION_SUSPEND);
    // pm_device_action_run(core_wake_pd, PM_DEVICE_ACTION_SUSPEND);
    // k_sleep(K_SECONDS(2));
    // printk("Wakeup from Deep Sleep2 Mode\r\n");

    // printk("Will enter Power Down 1 Mode\r\n");
    // pm_device_action_run(core_main_pd, PM_DEVICE_ACTION_TURN_OFF);
    // pm_device_action_run(core_wake_pd, PM_DEVICE_ACTION_SUSPEND);
    // k_sleep(K_SECONDS(2));
    // printk("Wakeup from Power Down 1 Mode\r\n");

    // printk("Will enter Power Down 2 Mode\r\n");
    // pm_device_action_run(core_main_pd, PM_DEVICE_ACTION_TURN_OFF);
    // pm_device_action_run(core_wake_pd, PM_DEVICE_ACTION_TURN_OFF);
    // k_sleep(K_SECONDS(2));
    // printk("Wakeup from Power Down 2 Mode\r\n");

    return 0;
}
