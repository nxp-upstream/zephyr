/* Networking DHCPv4 client */

/*
 * Copyright (c) 2017 ARM Ltd.
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_dhcpv4_client_sample, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <errno.h>
#include <stdio.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>

#include <zephyr/device.h>
#include <zephyr/usb/usbh.h>

USBH_CONTROLLER_DEFINE(uhs_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

#define DHCP_OPTION_NTP (42)

static uint8_t ntp_server[4];

static struct net_mgmt_event_callback mgmt_dhcp_cb;
static struct net_mgmt_event_callback mgmt_if_cb;

static struct net_dhcpv4_option_callback dhcp_cb;

static void handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_IPV4_DHCP_START:
		LOG_INF("DHCP Client start callback of %s", net_if_get_device(iface)->name);
		break;
	case NET_EVENT_IPV4_DHCP_STOP:
		LOG_INF("DHCP Client stop callback of %s", net_if_get_device(iface)->name);
		break;
	case NET_EVENT_IPV4_DHCP_BOUND: {
		LOG_INF("DHCP Client bound callback of %s", net_if_get_device(iface)->name);

		for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
			char buf[NET_IPV4_ADDR_LEN];

			if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
				continue;
			}

			LOG_INF("   Address[%d]: %s", net_if_get_by_iface(iface),
				net_addr_ntop(
					AF_INET,
					&iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
					buf, sizeof(buf)));
			LOG_INF("    Subnet[%d]: %s", net_if_get_by_iface(iface),
				net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].netmask,
					      buf, sizeof(buf)));
			LOG_INF("    Router[%d]: %s", net_if_get_by_iface(iface),
				net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, buf,
					      sizeof(buf)));
			LOG_INF("Lease time[%d]: %u seconds", net_if_get_by_iface(iface),
				iface->config.dhcpv4.lease_time);
		}
		break;
	}
	case NET_EVENT_IF_UP:
		LOG_INF("Start on %s: index=%d", net_if_get_device(iface)->name,
			net_if_get_by_iface(iface));
		net_dhcpv4_start(iface);
		break;
	case NET_EVENT_IF_DOWN:
		LOG_INF("Stop %s: index=%d", net_if_get_device(iface)->name,
			net_if_get_by_iface(iface));
		net_dhcpv4_stop(iface);
		break;
	}
}

static void option_handler(struct net_dhcpv4_option_callback *cb, size_t length,
			   enum net_dhcpv4_msg_type msg_type, struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	LOG_INF("DHCP Option %d: %s", cb->option,
		net_addr_ntop(AF_INET, cb->data, buf, sizeof(buf)));
}

int main(void)
{
	int err;

	err = usbh_init(&uhs_ctx);
	if (err) {
		LOG_ERR("Failed to initialize USB host: %d", err);
		return err;
	}

	err = usbh_enable(&uhs_ctx);
	if (err) {
		LOG_ERR("Failed to enable USB host: %d", err);
		return err;
	}

	LOG_INF("Run dhcpv4 client");

	net_mgmt_init_event_callback(&mgmt_dhcp_cb, handler,
				     NET_EVENT_IPV4_DHCP_START | NET_EVENT_IPV4_DHCP_STOP |
					     NET_EVENT_IPV4_DHCP_BOUND);
	net_mgmt_init_event_callback(&mgmt_if_cb, handler, NET_EVENT_IF_UP | NET_EVENT_IF_DOWN);
	net_mgmt_add_event_callback(&mgmt_dhcp_cb);
	net_mgmt_add_event_callback(&mgmt_if_cb);

	net_dhcpv4_init_option_callback(&dhcp_cb, option_handler, DHCP_OPTION_NTP, ntp_server,
					sizeof(ntp_server));

	net_dhcpv4_add_option_callback(&dhcp_cb);

	return 0;
}
