/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(border_router_main, LOG_LEVEL_INF);

/* WiFi credentials*/
#define WIFI_SSID        "NXP-RW612-otbr-Zephyr"
#define WIFI_PSK         "wcs12345"
#define WIFI_KEY_MGMT    1  /* 1 = WPA2-PSK, 0 = OPEN, 2 = WPA3-SAE */

static int auto_connect_wifi(void)
{
	struct net_if *iface;
	struct wifi_connect_req_params cnx_params = {0};
	struct wifi_iface_status status = {0};
	int ret;

	/* Get the WiFi STA interface (not default) */
	iface = net_if_get_wifi_sta();
	if (!iface) {
		LOG_ERR("No WiFi STA interface found");
		return -ENODEV;
	}

	/* Check current WiFi status */
	ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
	if (ret == 0) {
		if (status.state >= WIFI_STATE_ASSOCIATED) {
			LOG_INF("WiFi already connected to SSID: %s", status.ssid);
			return 0;
		}
		if (status.state == WIFI_STATE_SCANNING || status.state == WIFI_STATE_ASSOCIATING) {
			LOG_INF("WiFi connection already in progress");
			return 0;
		}
	}

	/* Configure WiFi connection parameters */
	cnx_params.ssid = WIFI_SSID;
	cnx_params.ssid_length = strlen(WIFI_SSID);
	cnx_params.psk = WIFI_PSK;
	cnx_params.psk_length = strlen(WIFI_PSK);
	
	/* Set security type based on key management */
	if (WIFI_KEY_MGMT == 0) {
		cnx_params.security = WIFI_SECURITY_TYPE_NONE;
	} else if (WIFI_KEY_MGMT == 1) {
		cnx_params.security = WIFI_SECURITY_TYPE_PSK;
	} else if (WIFI_KEY_MGMT == 2) {
		cnx_params.security = WIFI_SECURITY_TYPE_SAE;
		cnx_params.mfp = WIFI_MFP_REQUIRED;  /* Required for WPA3 */
	} else {
		cnx_params.security = WIFI_SECURITY_TYPE_PSK;
	}

	cnx_params.band = WIFI_FREQ_BAND_UNKNOWN;  /* Auto-detect */
	cnx_params.channel = WIFI_CHANNEL_ANY;
	cnx_params.timeout = SYS_FOREVER_MS;  /* No timeout */

	LOG_INF("Connecting to WiFi SSID: %s (security: %d)", WIFI_SSID, cnx_params.security);

	/* Send WiFi connect request */
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
		       &cnx_params, sizeof(struct wifi_connect_req_params));
	
	if (ret) {
		if (ret == -EALREADY) {
			LOG_INF("WiFi connection already in progress or established");
			return 0;
		}
		LOG_ERR("WiFi connection request failed: %d", ret);
		return ret;
	}

	LOG_INF("WiFi connection request sent successfully");
	return 0;
}

int main(void)
{
	int ret;

	LOG_INF("NXP Zephyr OpenThread Border Router starting...");

	/* Wait for network interfaces to be ready */
	k_sleep(K_SECONDS(2));

	/* Auto-connect to WiFi */
	ret = auto_connect_wifi();
	if (ret) {
		LOG_ERR("Failed to initiate WiFi connection: %d", ret);
		/* Continue anyway - Border Router can still work */
	}

	/* The Border Router is automatically started in the background. */
	LOG_INF("Border Router running...");

	return 0;
}
