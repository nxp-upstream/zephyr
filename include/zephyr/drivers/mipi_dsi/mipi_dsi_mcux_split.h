/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_SPLIT_
#define ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_SPLIT_

#include <zephyr/device.h>

/*
 * HW specific flag- indicates to the MIPI DSI split peripheral that the
 * data being sent is framebuffer data, which the DSI peripheral may
 * byte swap depending on KConfig settings
 */
#define MCUX_DSI_SPLIT_FB_DATA BIT(0x1)

/*
 * Arms the DPI/video-mode interface an attach()ed panel requested with
 * MIPI_DSI_MODE_VIDEO. Split out of attach() itself because enabling it
 * before the panel's own LP-mode init command sequence collides with that
 * sequence; the panel driver (e.g. display_hx8394.c) has no hook to signal
 * "my init sequence is done", so this is instead called from the far side of
 * the DPI link, in the LCDIF/DCIF driver feeding this host over DPI, at the
 * point that driver itself starts driving real output
 * (display_nxp_dcif.c's nxp_dcif_display_blanking_off()/first
 * nxp_dcif_write() is the current caller, resolving this device with
 * DEVICE_DT_GET_ANY(nxp_mipi_dsi_split) rather than a devicetree phandle --
 * a phandle in either direction between the dcif and mipi_dsi nodes forms a
 * devicetree dependency cycle, since mipi_dsi already has its own,
 * load-bearing phandle to dcif via nxp,lcdif).
 *
 * Safe to call more than once; a call before attach() (video mode was not
 * requested, or the host is not yet attached at all) is a no-op, not an
 * error, since the DCIF/LCDIF driver calling this has no visibility into
 * whether a panel has attached yet either.
 */
void mcux_mipi_dsi_split_start_video_mode(const struct device *dev);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MIPI_DSI_MCUX_SPLIT_ */
