/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_PM_PM_CPU_SHELL_H_
#define ZEPHYR_SUBSYS_PM_PM_CPU_SHELL_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * @brief thread_node.
 *
 * @note All fields in this structure are meant for private usage.
 */
struct thread_event {
        /** @cond INTERNAL_HIDDEN */
        sys_snode_t node;
        struct k_thread *thread;
        /** @endcond */
};

/**
 * @brief Check if shell-forced CPU idle mode is active.
 *
 * This function indicates whether the CPU idle mode was explicitly
 * triggered and forced by a shell command, instead of being decided
 * by the regular power management policy. If true, the system should
 * enter only CPU idle and not SoC-level low power states.
 *
 * @retval true  Shell is forcing CPU idle mode.
 * @retval false Normal power management policy is in effect.
 */
bool pm_cpu_shell_forced_idle(void);

/**
 * @brief Resume all threads previously suspended by the shell low power entry.
 *
 * This function resumes all application threads that were suspended
 * during entry to shell-triggered low power mode. It should be called
 * after the system wakes up from shell-invoked low power state to
 * restore normal thread execution.
 */
void pm_resume_threads(void);
#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_PM_PM_CPU_SHELL_H_ */
