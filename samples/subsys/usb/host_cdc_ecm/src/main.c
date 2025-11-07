/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_core.h> 
#include <zephyr/net/net_pkt.h>    
#include <zephyr/net/net_ip.h>
#include <zephyr/net/icmp.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/usb/usbh.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net/net_stats.h>
#include <errno.h>
#include <fcntl.h>

//#include "icmpv4.h"

extern bool usbh_cdc_ecm_is_network_ready(const struct device *dev);
extern struct net_if *usbh_cdc_ecm_get_iface(const struct device *dev);

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// Use standard function to replace net_sprint_ipv4_addr
static char *ipv4_to_str(const struct in_addr *addr)
{
	static char str[16];  // xxx.xxx.xxx.xxx\0
	uint8_t *ip = (uint8_t *)&addr->s_addr;
	snprintf(str, sizeof(str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	return str;
}

/* USB Host Controller definition */
USBH_CONTROLLER_DEFINE(uhs_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));
const struct device *const cdc_ecm_host = DEVICE_DT_GET(DT_NODELABEL(cdc_ecm_host));
#define UDP_ECHO_PORT 4242
#define DNS_TEST_DOMAIN "nxp.com"

/* Application state */
enum app_state {
	APP_STATE_INIT,
	APP_STATE_WAITING_DEVICE,
	APP_STATE_DEVICE_DETECTED,
	APP_STATE_NETWORK_CONFIG,
	APP_STATE_NETWORK_READY,
	APP_STATE_ERROR
};

struct app_context {
	enum app_state state;
	struct net_if *cdc_ecm_iface;
	const struct device *cdc_ecm_dev;
	bool device_connected;
	bool dhcp_bound;
	int udp_echo_sock;
};

static struct app_context app_ctx;
static struct net_mgmt_event_callback mgmt_cb;

/* Forward declarations */
static void handle_device_connected(void);
static void handle_device_disconnected(void);

static void app_state_change(enum app_state new_state)
{
	enum app_state old_state = app_ctx.state;
	app_ctx.state = new_state;
	
	LOG_INF("State change: %d -> %d", old_state, new_state);
}

static bool is_cdc_ecm_interface(struct net_if *iface)
{
	if (!iface || net_if_l2(iface) != &NET_L2_GET_NAME(ETHERNET)) {
		return false;
	}

	const struct device *dev = net_if_get_device(iface);
	if (!dev) {
		return false;
	}

	/* Check if this is a CDC ECM device by examining the device name or driver */
	if (strstr(dev->name, "cdc_ecm") != NULL || 
	    strstr(dev->name, "usbh_cdc_ecm") != NULL) {
		return true;
	}

	return false;
}

static void iface_cb(struct net_if *iface, void *user_data)
{
	bool *found = (bool *)user_data;
	
	if (*found) {
		return; /* Already found one */
	}

	if (is_cdc_ecm_interface(iface)) {
		LOG_INF("Found CDC ECM interface: %p (device: %s)", 
			iface, net_if_get_device(iface)->name);
		app_ctx.cdc_ecm_iface = iface;
		*found = true;
	}
}

static struct net_if *find_cdc_ecm_interface(void)
{
	bool found = false;
	
	app_ctx.cdc_ecm_iface = NULL;
	net_if_foreach(iface_cb, &found);
	
	return app_ctx.cdc_ecm_iface;
}

/**
 * @brief DNS resolution test
 */
static void test_dns_resolution(void)
{
	static bool dns_test_done = false;
	int ret;

	if (dns_test_done || !IS_ENABLED(CONFIG_DNS_RESOLVER)) {
		return;
	}

	LOG_INF("Start resolving domain name (%s)...", DNS_TEST_DOMAIN);

	ret = getaddrinfo(DNS_TEST_DOMAIN, NULL, NULL, NULL);
	if (ret == 0) {
		LOG_INF("Domain name resolution success for %s", DNS_TEST_DOMAIN);
		dns_test_done = true;
	} else {
		LOG_WRN("Failed to resolve domain name %s: %d", DNS_TEST_DOMAIN, ret);
	}
}

static int init_udp_echo_server(void)
{
	struct sockaddr_in bind_addr;
	int ret;

	if (app_ctx.udp_echo_sock >= 0) {
		return 0;
	}

	app_ctx.udp_echo_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (app_ctx.udp_echo_sock < 0) {
		LOG_ERR("Failed to create UDP socket: %d", errno);
		return -errno;
	}

	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = INADDR_ANY;
	bind_addr.sin_port = htons(UDP_ECHO_PORT);

	ret = bind(app_ctx.udp_echo_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind UDP socket: %d", errno);
		close(app_ctx.udp_echo_sock);
		app_ctx.udp_echo_sock = -1;
		return -errno;
	}

	LOG_INF("UDP Echo server listening on port %d", UDP_ECHO_PORT);
	return 0;
}

static void handle_network_ready(void)
{
	if (app_ctx.state == APP_STATE_NETWORK_READY) {
		return;
	}

	LOG_INF("Network is ready!");
	app_state_change(APP_STATE_NETWORK_READY);

	/* Initialize UDP echo server */
	if (init_udp_echo_server() == 0) {
		LOG_INF("Network services initialized");
	}

	/* Test DNS resolution */
	test_dns_resolution();
}

static void handle_network_link_up(void)
{
	struct net_if *iface = NULL;

	/* First try to use the saved interface */
	if (app_ctx.cdc_ecm_iface) {
		iface = app_ctx.cdc_ecm_iface;
	} else {
		/* If not saved yet, get from CDC ECM driver */
		iface = usbh_cdc_ecm_get_iface(cdc_ecm_host);
		if (iface) {
			app_ctx.cdc_ecm_iface = iface;  /* Save for later use */
		}
	}

	if (!iface) {
		LOG_WRN("Network link up but no interface available");
		return;
	}

	LOG_INF("Network link is up - starting network configuration");
	app_state_change(APP_STATE_NETWORK_CONFIG);

	/* Start DHCP if enabled */
	if (IS_ENABLED(CONFIG_NET_DHCPV4)) {
		LOG_INF("Get IPv4 information from DHCP");
		net_dhcpv4_start(iface);
		LOG_INF("Waiting DHCP server process...");
	} else {
		/* For static IP configuration */
		LOG_INF("Using static IPv4 configuration");
		handle_network_ready();
	}
}

/* Simplified ping context */
struct simple_ping_ctx {
    struct net_icmp_ctx icmp;
    struct sockaddr_in addr4;
    const char *host_ip;
    int64_t send_time;
    bool reply_received;
};

static struct simple_ping_ctx ping_ctx;

/* Simplified ping context */
struct simple_ping_ctx {
    struct net_icmp_ctx icmp;
    struct sockaddr_in addr4;
    const char *host_ip;
    int64_t send_time;
    uint16_t identifier;
    uint16_t sequence;
    bool reply_received;
};

static struct simple_ping_ctx ping_ctx;

/* IPv4 reply handler - does not depend on any internal structures */
static int handle_ping_reply(struct net_icmp_ctx *ctx,
                 struct net_pkt *pkt,
                 struct net_icmp_ip_hdr *hdr,
                 struct net_icmp_hdr *icmp_hdr,
                 void *user_data)
{
    struct simple_ping_ctx *ping_ctx = (struct simple_ping_ctx *)user_data;
    struct net_ipv4_hdr *ip_hdr = hdr->ipv4;
    int64_t rtt;

    /* Simple handling: consider success as long as ICMP Echo Reply is received */
    if (icmp_hdr->type == NET_ICMPV4_ECHO_REPLY) {
        /* Calculate RTT */
        rtt = k_uptime_get() - ping_ctx->send_time;
        if (rtt < 1) rtt = 1;

        LOG_INF("ping: recv %s %lld ms (ttl=%d)", 
            ping_ctx->host_ip, rtt, ip_hdr->ttl);

        ping_ctx->reply_received = true;
    }

    return 0;
}

/* Simplified ping function */
void simple_ping_gateway(const char *gateway_ip, int sequence)
{
    struct net_icmp_ping_params params;
    int ret;

    LOG_INF("ping: send %s", gateway_ip);

    /* Initialize ping context */
    memset(&ping_ctx, 0, sizeof(ping_ctx));
    ping_ctx.host_ip = gateway_ip;
    ping_ctx.reply_received = false;
    ping_ctx.identifier = sys_rand32_get() & 0xFFFF;
    ping_ctx.sequence = sequence;

    /* Set target address */
    ping_ctx.addr4.sin_family = AF_INET;
    ret = net_addr_pton(AF_INET, gateway_ip, &ping_ctx.addr4.sin_addr);
    if (ret < 0) {
        LOG_ERR("Invalid IP address: %s", gateway_ip);
        return;
    }

    /* Initialize ICMP context */
    ret = net_icmp_init_ctx(&ping_ctx.icmp, NET_ICMPV4_ECHO_REPLY, 0,
                handle_ping_reply);
    if (ret < 0) {
        LOG_ERR("Failed to initialize ICMP context: %d", ret);
        return;
    }

    /* Set ping parameters */
    params.identifier = ping_ctx.identifier;
    params.sequence = ping_ctx.sequence;
    params.tc_tos = 0;
    params.priority = -1;
    params.data = NULL;
    params.data_size = 32;

    /* Record send time */
    ping_ctx.send_time = k_uptime_get();

    /* Check network interface */
    if (!app_ctx.cdc_ecm_iface) {
        LOG_ERR("Network interface not available");
        net_icmp_cleanup_ctx(&ping_ctx.icmp);
        return;
    }

    /* Send ping */
    ret = net_icmp_send_echo_request(&ping_ctx.icmp,
                     app_ctx.cdc_ecm_iface,
                     (struct sockaddr *)&ping_ctx.addr4,
                     &params,
                     &ping_ctx);

    if (ret < 0) {
        LOG_INF("ping: send %s failed (%d)", gateway_ip, ret);
        net_icmp_cleanup_ctx(&ping_ctx.icmp);
        return;
    }

    /* Wait for reply */
    int timeout_ms = 3000;
    int wait_step = 100;
    
    for (int i = 0; i < timeout_ms / wait_step; i++) {
        k_msleep(wait_step);
        if (ping_ctx.reply_received) {
            break;
        }
    }
    
    if (!ping_ctx.reply_received) {
        LOG_INF("ping: recv %s timeout", gateway_ip);
    }

    /* Cleanup ICMP context */
    net_icmp_cleanup_ctx(&ping_ctx.icmp);
}

/**
 * @brief Real ping gateway function - send ICMP and wait for reply
 */
void simple_ping_test(void)
{
	if (!app_ctx.cdc_ecm_iface) {
		return;
	}

	/* Get gateway IP address */
	struct net_if_ipv4 *ipv4 = app_ctx.cdc_ecm_iface->config.ip.ipv4;
	if (!ipv4) {
		return;
	}

	/* Check if gateway address is valid */
	if (net_ipv4_is_addr_unspecified(&ipv4->gw)) {
		LOG_INF("No gateway address configured");
		return;
	}

	char gateway_ip_str[NET_IPV4_ADDR_LEN] = {0};
	inet_ntop(AF_INET, &ipv4->gw, gateway_ip_str, sizeof(gateway_ip_str));

	if (strlen(gateway_ip_str) == 0) {
		return;
	}

	/* ping 4 times */
	for (int i = 0; i < 4; i++) {
		ping_single_gateway(gateway_ip_str, i + 1);
		k_msleep(2000); /* Wait 1 second */
	}
}

static void show_network_info(void)
{
	char addr_str[NET_IPV4_ADDR_LEN];
	bool found_ip = false;

	if (!app_ctx.cdc_ecm_iface) {
		return;
	}

	LOG_INF("************************************************");
	LOG_INF(" Network Interface Information");
	LOG_INF("************************************************");
	
	/* Iterate to find valid IPv4 address */
	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		struct net_if_addr *addr = &app_ctx.cdc_ecm_iface->config.ip.ipv4->unicast[i];
		
		if (addr->is_used && addr->addr_type != NET_ADDR_ANY) {
			/* Show IPv4 address */
			net_addr_ntop(AF_INET, &addr->address.in_addr, 
			              addr_str, sizeof(addr_str));
			LOG_INF(" IPv4 Address     : %s", addr_str);
			
			/* Use API to get corresponding subnet mask */
			struct in_addr netmask = net_if_ipv4_get_netmask_by_addr(
				app_ctx.cdc_ecm_iface, &addr->address.in_addr);
			
			net_addr_ntop(AF_INET, &netmask, addr_str, sizeof(addr_str));
			LOG_INF(" IPv4 Subnet mask : %s", addr_str);
			
			found_ip = true;
			break;
		}
	}
	
	if (!found_ip) {
		LOG_INF(" IPv4 Address     : Not assigned");
	}
	
	/* Show gateway */
	if (app_ctx.cdc_ecm_iface->config.ip.ipv4 && 
	    !net_ipv4_is_addr_unspecified(&app_ctx.cdc_ecm_iface->config.ip.ipv4->gw)) {
		net_addr_ntop(AF_INET, &app_ctx.cdc_ecm_iface->config.ip.ipv4->gw, 
		              addr_str, sizeof(addr_str));
		LOG_INF(" IPv4 Gateway     : %s", addr_str);
	}

	/* MAC address */
	struct net_linkaddr *link_addr = net_if_get_link_addr(app_ctx.cdc_ecm_iface);
	if (link_addr && link_addr->len == 6) {
		LOG_INF(" MAC Address      : %02X:%02X:%02X:%02X:%02X:%02X",
			link_addr->addr[0], link_addr->addr[1], link_addr->addr[2],
			link_addr->addr[3], link_addr->addr[4], link_addr->addr[5]);
	}

	LOG_INF(" Interface Status : %s", 
		net_if_is_up(app_ctx.cdc_ecm_iface) ? "UP" : "DOWN");
	LOG_INF("************************************************");

	/* Add ping test here */
	simple_ping_test();
}


static void process_udp_echo_server(void)
{
	static uint8_t recv_buf[1024];
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	ssize_t recv_len;
	int flags;
	
	if (app_ctx.udp_echo_sock < 0) {
		return;
	}

	/* Set non-blocking mode */
	flags = fcntl(app_ctx.udp_echo_sock, F_GETFL, 0);
	fcntl(app_ctx.udp_echo_sock, F_SETFL, flags | O_NONBLOCK);

	recv_len = recvfrom(app_ctx.udp_echo_sock, recv_buf, sizeof(recv_buf), 0,
			    (struct sockaddr *)&client_addr, &client_addr_len);
	
	if (recv_len > 0) {
		LOG_INF("UDP Echo: received %zd bytes from %s:%d", 
			recv_len, ipv4_to_str(&client_addr.sin_addr),
			ntohs(client_addr.sin_port));

		ssize_t sent_len = sendto(app_ctx.udp_echo_sock, recv_buf, recv_len, 0,
					  (struct sockaddr *)&client_addr, client_addr_len);
		
		if (sent_len != recv_len) {
			LOG_WRN("UDP Echo: sent %zd bytes, expected %zd", sent_len, recv_len);
		}
	}
}

/////////////////////////



extern uint8_t boud_flag;
extern uint8_t int_finished;
static void handle_device_connected(void)
{
	LOG_INF("CDC ECM USB device connected");
	app_ctx.device_connected = true;
	
	while(1) {
		if (int_finished) {
			break;
		}
	}
	
	usbh_cdc_ecm_submit_bulk_in_transfer(cdc_ecm_host);
	app_state_change(APP_STATE_DEVICE_DETECTED);

	/* Get network interface */
	app_ctx.cdc_ecm_iface = usbh_cdc_ecm_get_iface(cdc_ecm_host);
	if (!app_ctx.cdc_ecm_iface) {
		LOG_ERR("Failed to get CDC ECM network interface");
		return;
	}
	
	LOG_INF("CDC ECM network interface obtained: %p", app_ctx.cdc_ecm_iface);

	/* Start network link */
	net_if_carrier_on(app_ctx.cdc_ecm_iface);
	app_state_change(APP_STATE_NETWORK_CONFIG);

	/* Start DHCP */
	if (IS_ENABLED(CONFIG_NET_DHCPV4)) {
		LOG_INF("Get IPv4 information from DHCP");
		net_dhcpv4_start(app_ctx.cdc_ecm_iface);
		LOG_INF("Waiting DHCP server process...");
	} else {
		/* Static IP configuration - directly mark network ready */
		LOG_INF("Using static IPv4 configuration");
		app_state_change(APP_STATE_NETWORK_READY);
	}

	LOG_INF("Waiting for network link to come up...");
	while (1)
	{
		if (boud_flag)
		{
			break;
		}
	}
	/* Show network interface information */
	show_network_info();
}

static void handle_device_disconnected(void)
{
	LOG_INF("CDC ECM USB device disconnected");

	/* Stop DHCP if running */
	if (app_ctx.dhcp_bound && app_ctx.cdc_ecm_iface && IS_ENABLED(CONFIG_NET_DHCPV4)) {
		net_dhcpv4_stop(app_ctx.cdc_ecm_iface);
	}

	/* Close UDP socket */
	if (app_ctx.udp_echo_sock >= 0) {
		close(app_ctx.udp_echo_sock);
		app_ctx.udp_echo_sock = -1;
	}

	/* Reset state */
	app_ctx.cdc_ecm_iface = NULL;
	app_ctx.device_connected = false;
	app_ctx.dhcp_bound = false;
	app_state_change(APP_STATE_WAITING_DEVICE);
}

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				   uint64_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_IF_UP:
		/* Check if this is a CDC ECM interface coming up */
		if (is_cdc_ecm_interface(iface)) {
			LOG_INF("CDC ECM interface UP: %p", iface);
			
			/* Only proceed if we're expecting a device */
			if (app_ctx.state == APP_STATE_DEVICE_DETECTED && 
			    app_ctx.device_connected && !app_ctx.cdc_ecm_iface) {
				
				app_ctx.cdc_ecm_iface = iface;
				app_state_change(APP_STATE_NETWORK_CONFIG);
				
				LOG_INF("CDC ECM network interface detected and configured");
				
				/* Start DHCP if enabled */
				if (IS_ENABLED(CONFIG_NET_DHCPV4)) {
					LOG_INF("Starting DHCP client...");
					net_dhcpv4_start(iface);
				} else {
					/* For static IP, we consider network ready immediately */
					handle_network_ready();
				}
			}
		}
		break;

	case NET_EVENT_IF_DOWN:
		/* Check if our CDC ECM interface went down */
		if (iface == app_ctx.cdc_ecm_iface) {
			LOG_INF("CDC ECM interface DOWN: %p", iface);
			
			/* This usually means the USB device was disconnected */
			if (!app_ctx.device_connected) {
				handle_device_disconnected();
			}
		}
		break;

	case NET_EVENT_IPV4_DHCP_BOUND:
		if (iface == app_ctx.cdc_ecm_iface) {
			LOG_INF("DHCP bound - IPv4 address assigned");
			app_ctx.dhcp_bound = true;
			
			/* Show network interface information */
			show_network_info();
			
			handle_network_ready();
		}
		break;

	case NET_EVENT_IPV4_DHCP_START:
		if (iface == app_ctx.cdc_ecm_iface) {
			LOG_INF("DHCP client started");
		}
		break;

	case NET_EVENT_IPV4_DHCP_STOP:
		if (iface == app_ctx.cdc_ecm_iface) {
			LOG_INF("DHCP client stopped");
			app_ctx.dhcp_bound = false;
		}
		break;

	case NET_EVENT_IPV4_ADDR_ADD:
		if (iface == app_ctx.cdc_ecm_iface && !app_ctx.dhcp_bound) {
			LOG_INF("Static IPv4 address configured");
			handle_network_ready();
		}
		break;

	default:
		break;
	}
}


int main(void)
{
	struct k_poll_signal sig;
	struct k_poll_event evt[1];
	k_timeout_t timeout = K_FOREVER;
	int err;
	int signaled, result;

	LOG_INF("USB Host CDC ECM Network Interface Sample");

	/* Initialize application context */
	memset(&app_ctx, 0, sizeof(app_ctx));
	app_ctx.udp_echo_sock = -1;
	app_state_change(APP_STATE_INIT);

	// 1. Check CDC ECM host device ready
	if (!device_is_ready(cdc_ecm_host)) {
		LOG_ERR("%s: CDC ECM host is not ready", cdc_ecm_host->name);
		return -ENODEV;
	}
	LOG_INF("CDC ECM host device: %s", cdc_ecm_host->name);

	/* Initialize USB Host */
	err = usbh_init(&uhs_ctx);
	if (err) {
		LOG_ERR("Failed to initialize USB host support: %d", err);
		return err;
	}

	err = usbh_enable(&uhs_ctx);
	if (err) {
		LOG_ERR("Failed to enable USB host support: %d", err);
		return err;
	}

	/* Register network management event callback */
	net_mgmt_init_event_callback(&mgmt_cb, net_mgmt_event_handler,
									NET_EVENT_IF_UP | NET_EVENT_IF_DOWN |
									NET_EVENT_IPV4_ADDR_ADD |
									NET_EVENT_IPV4_DHCP_BOUND |
									NET_EVENT_IPV4_DHCP_START |
									NET_EVENT_IPV4_DHCP_STOP);
	net_mgmt_add_event_callback(&mgmt_cb);

	/* Setup polling for device events */
	k_poll_signal_init(&sig);
	k_poll_event_init(&evt[0], K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);

	// 2. Setup signal for CDC ECM event notification
	err = usbh_cdc_ecm_set_signal(cdc_ecm_host, &sig);
	if (err != 0) {
		LOG_WRN("Failed to setup signal for CDC ECM device: %d", err);
		timeout = K_MSEC(1000);
	}

	app_state_change(APP_STATE_WAITING_DEVICE);

	LOG_INF("Waiting for USB CDC ECM device...");
	LOG_INF("Please connect a USB CDC ECM device (smartphone, tablet, etc.)");

	// 3. Detect CDC ECM device events through signal in main loop
	while (true) {
		err = k_poll(evt, ARRAY_SIZE(evt), timeout);
		if (err != 0 && err != -EAGAIN) {
			LOG_WRN("Poll failed with error %d, retrying...", err);
			continue;
		}

		k_poll_signal_check(&sig, &signaled, &result);
		if (!signaled) {
			goto process_services;
		}

		k_poll_signal_reset(&sig);

		switch (result) {
		case USBH_DEVICE_CONNECTED:
			handle_device_connected();
			break;

		case USBH_DEVICE_DISCONNECTED:
			handle_device_disconnected();
			break;

		case 3:  /* Network link ready */
			handle_network_link_up();
			break;

		default:
			LOG_DBG("Received signal: %d", result);
			break;
		}

process_services:
		/* Periodic check for CDC ECM interface */
		if (app_ctx.state == APP_STATE_DEVICE_DETECTED && 
		    app_ctx.device_connected && !app_ctx.cdc_ecm_iface) {
			
			static uint32_t last_check = 0;
			uint32_t now = k_uptime_get_32();
			
			if ((now - last_check) > 1000) {
				last_check = now;
				
				struct net_if *found_iface = find_cdc_ecm_interface();
				if (found_iface && net_if_is_up(found_iface)) {
					LOG_INF("CDC ECM interface found via periodic check");
					
					app_ctx.cdc_ecm_iface = found_iface;
					app_state_change(APP_STATE_NETWORK_CONFIG);
					
					if (IS_ENABLED(CONFIG_NET_DHCPV4)) {
						LOG_INF("Starting DHCP client...");
						net_dhcpv4_start(found_iface);
					} else {
						handle_network_ready();
					}
				}
			}
		}

		/* Process network services if ready */
		if (app_ctx.state == APP_STATE_NETWORK_READY) {
			process_udp_echo_server();
			
			/* Periodic DNS test */
			static uint32_t last_dns_test = 0;
			uint32_t now = k_uptime_get_32();
			if ((now - last_dns_test) > 30000) {
				last_dns_test = now;
				test_dns_resolution();
			}
		}
	}

	return 0;
}
