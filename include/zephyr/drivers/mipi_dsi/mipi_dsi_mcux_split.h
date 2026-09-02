/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_SPLIT_
#define ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_SPLIT_

/*
 * HW specific flag- indicates to the MIPI DSI split peripheral that the
 * data being sent is framebuffer data, which the DSI peripheral may
 * byte swap depending on KConfig settings
 */
#define MCUX_DSI_SPLIT_FB_DATA BIT(0x1)

#endif /* ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_SPLIT_ */
