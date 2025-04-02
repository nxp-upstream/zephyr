/*
 * Copyright NXP 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <mbedtls/platform_time.h>
#include <zephyr/kernel.h>

#include <Hse_Ip.h>

__nocache Hse_Ip_ReqType HseIp_aRequest[HSE_NUM_OF_CHANNELS_PER_MU];
__nocache hseSrvDescriptor_t Hse_aSrvDescriptor[HSE_NUM_OF_CHANNELS_PER_MU];

mbedtls_ms_time_t __wrap_mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)k_uptime_get();
}
