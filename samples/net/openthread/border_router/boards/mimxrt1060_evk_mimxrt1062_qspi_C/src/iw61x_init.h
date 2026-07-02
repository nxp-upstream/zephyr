/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <zephyr/kernel.h>

/**
 * @brief Wait for IW61X bringup to complete:
 *        BT+15.4 firmware loaded + IO Expander SPI configured.
 *
 * @param timeout Maximum wait timeout
 * @return 0 if ready, -ETIMEDOUT, or bt_enable/ioexp error code
 */
int iw61x_wait_ready(k_timeout_t timeout);
