/*
 * Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if defined(CONFIG_OTBR_WEB_SERVER) || defined(CONFIG_HDLC_RCP_IF_SPI_NO_AUTO_START)
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(border_router_main, CONFIG_OTBR_LOG_LEVEL);
#endif

#ifdef CONFIG_HDLC_RCP_IF_SPI_NO_AUTO_START
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/openthread.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/hdlc_rcp_if/hdlc_rcp_if.h>
#include "iw61x_init.h"
#endif

#ifdef CONFIG_OTBR_WEB_SERVER
#include "web_server/web_server.h"
#endif

int main(void)
{
#ifdef CONFIG_HDLC_RCP_IF_SPI_NO_AUTO_START
	LOG_INF("=== OpenThread Border Router Starting ===");
	int err;

	/* Wait for IW61X bringup */
	err = iw61x_wait_ready(K_SECONDS(20));
	if (err) {
		LOG_ERR("IW612X bringup failed: %d", err);
		return err;
	}

	/* Get OpenThread network interface */
	struct net_if *ot_iface = net_if_get_first_by_type(
					&NET_L2_GET_NAME(OPENTHREAD));
	if (!ot_iface) {
		LOG_ERR("OpenThread interface not found");
		return -ENODEV;
	}

	/* Start HDLC RCP */
	const struct hdlc_api *api = net_if_get_device(ot_iface)->api;

	if (api && api->start) {
		api->start();
		LOG_INF("15.4 RCP SPI interface started");
	}

	/* Initialize IEEE 802.15.4 / OpenThread */
	ieee802154_init(ot_iface);
	LOG_INF("ieee802154_init done");

	/* Bring up OpenThread interface */
	net_if_up(ot_iface);
	LOG_INF("OpenThread interface up");

#endif /* CONFIG_HDLC_RCP_IF_SPI_NO_AUTO_START */

#ifdef CONFIG_OTBR_WEB_SERVER
	/* Setup web server */
	LOG_INF("Zephyr OTBR Web Server setup ...");
	web_server_setup();
#endif

	/* Nothing to do here. The Border Router is automatically started in the background. */

	return 0;
}
