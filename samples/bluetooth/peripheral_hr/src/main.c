/* main.c - Application main entry point */

/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/hrs.h>

//-------------- LED -------------------------------------------
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/led/is31fl3733.h>

#define HW_ROW_COUNT 12
#define HW_COL_COUNT 16

#define CONFIG_LED_ROW_COUNT 12       // TODO move to Kconfig
#define CONFIG_LED_COLUMN_COUNT 16

/* LED matrix is addressed using a row major format */
#define LED_MATRIX_COORD(x, y) ((x) * HW_COL_COUNT) + (y)

static uint8_t led_state[HW_COL_COUNT * HW_ROW_COUNT];
const struct device *led_dev = DEVICE_DT_GET_ONE(issi_is31fl3733);

static bool bt_connected = false;

//----------------------------------------------------------------

const uint8_t nxp_scr[HW_ROW_COUNT * HW_COL_COUNT] = 
{
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 0,
	 1, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1,
	 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1,
	 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1,
	 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0,
	 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0,
	 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};		

const uint8_t bsh_scr[HW_ROW_COUNT * HW_COL_COUNT] = 
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1,
	1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1,
	1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1,
	1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1,
	1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1,
	1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1,
	1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1,
	1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 1,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const uint8_t zephyr_scr[HW_ROW_COUNT * HW_COL_COUNT] = 
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
	1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0,
	0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0,
	0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0,
	0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0,
	1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0,
	1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const uint8_t demo_scr[HW_ROW_COUNT * HW_COL_COUNT] = 
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0,
	1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 1,
	1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1,
	1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1,
	1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1,
	1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1,
	1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1,
	1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const uint8_t clock_scr1[HW_ROW_COUNT * HW_COL_COUNT] = 
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0,
	1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0,
	1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const uint8_t clock_scr2[HW_ROW_COUNT * HW_COL_COUNT] = 
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0,
	1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
	1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
	1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const uint8_t clock_scr3[HW_ROW_COUNT * HW_COL_COUNT] = 
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0,
	0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0,
	1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
	1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0,
	1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
	1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define	NUM_SCREENS 7
#define BRIGHT 20
uint8_t *display_scr[NUM_SCREENS];
//----------------------------------------------------------------

static bool hrf_ntf_enabled;


static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL)),
#if defined(CONFIG_BT_EXT_ADV)
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
#endif /* CONFIG_BT_EXT_ADV */
};

#if !defined(CONFIG_BT_EXT_ADV)
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
#endif /* !CONFIG_BT_EXT_ADV */

enum {
	STATE_CONNECTED,
	STATE_DISCONNECTED,
	STATE_BITS,
};

static ATOMIC_DEFINE(state, STATE_BITS);

static void blink_start(void);
static void blink_stop(void);

//-------------- LED -------------------------------------------
static int led_brightness(const struct device *led, uint8_t val)
{
	int ret;
	uint8_t row, col;

	/* Set LED brightness to low value sequentially */
	printk("Set LEDs to half brightness sequentially\n");
	for (row = 0; row < CONFIG_LED_ROW_COUNT; row++) {
		for (col = 0; col < CONFIG_LED_COLUMN_COUNT; col++) {
			ret = led_set_brightness(led, LED_MATRIX_COORD(row, col), val);
			if (ret < 0) {
				printk("Error: could not enable led at [%d, %d]: (%d)\n",
					row, col, ret);
				return ret;
			}
			k_msleep(50); /* Reduced delay for BT responsiveness */
		}
	}
	return 0;
}

static int led_on_off(const struct device *led)
{
	int ret;
	uint8_t row, col;

	printk("Toggle each led\n");
	/* Turn on each led for a short duration */
	for (row = 0; row < CONFIG_LED_ROW_COUNT; row++) {
		for (col = 0; col < CONFIG_LED_COLUMN_COUNT; col++) {
			ret = led_off(led, LED_MATRIX_COORD(row, col));
			if (ret < 0) {
				printk("Error: could not disable led at [%d, %d]: (%d)\n",
					row, col, ret);
				return ret;
			}
			k_msleep(10); /* Reduced delay for BT responsiveness */
			ret = led_on(led, LED_MATRIX_COORD(row, col));
			if (ret < 0) {
				printk("Error: could not enable led at [%d, %d]: (%d)\n",
					row, col, ret);
				return ret;
			}
		}
	}
	k_msleep(500);
	return 0;
}

static void led_indicate_bt_status(void)
{
	if (!device_is_ready(led_dev)) {
		return;
	}

	if (bt_connected) {

		blink_start(); // Indicate BT connnection Green LED blink
	} 
	// BT connection lost -disconnected
	else {

		blink_stop(); // Indicate BT connection is Lost!
	}
}
//------------------ LED SHOW -----------------------------------

static void led_show_scr(const uint8_t *msg, uint8_t bright, uint8_t move)
{
	int ret;
	uint8_t row, col, idx=move;
	
	if (!device_is_ready(led_dev)) {
		return;
	}
	for (row = 0; row < CONFIG_LED_ROW_COUNT; row++) {
		for (col = CONFIG_LED_COLUMN_COUNT; col > 0; col--) {
			ret = led_off(led_dev, LED_MATRIX_COORD(row, col-1)); // Clr screen		    
			if(msg[idx] == 1)
			{
			  //ret = led_on(led_dev, LED_MATRIX_COORD(row, col-1));
			  ret = led_set_brightness(led_dev, LED_MATRIX_COORD(row, col-1), bright);
			}
			idx++;
		}
	}
}
//----------------------------------------------------------------

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
	} else {
		printk("Connected\n");

		(void)atomic_set_bit(state, STATE_CONNECTED);
                bt_connected = true;
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));

	(void)atomic_set_bit(state, STATE_DISCONNECTED);
        bt_connected = false;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void hrs_ntf_changed(bool enabled)
{
	hrf_ntf_enabled = enabled;

	printk("HRS notification status changed: %s\n",
	       enabled ? "enabled" : "disabled");
}

static struct bt_hrs_cb hrs_cb = {
	.ntf_changed = hrs_ntf_changed,
};

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

static void bas_notify(void)
{
	uint8_t battery_level = bt_bas_get_battery_level();

	battery_level--;

	if (!battery_level) {
		battery_level = 100U;
	}

	bt_bas_set_battery_level(battery_level);
}

static void hrs_notify(void)
{
	static uint8_t heartrate = 90U;

	/* Heartrate measurements simulation */
	heartrate++;
	if (heartrate == 160U) {
		heartrate = 90U;
	}

	if (hrf_ntf_enabled) {
		bt_hrs_notify(heartrate);
	}
}

#if defined(CONFIG_GPIO)
/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#include <zephyr/drivers/gpio.h>
#define HAS_LED     1
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#define BLINK_ONOFF K_MSEC(500)

static struct k_work_delayable blink_work;
static bool                  led_is_on;

static void blink_timeout(struct k_work *work)
{
	led_is_on = !led_is_on;
	gpio_pin_set(led.port, led.pin, (int)led_is_on);

	k_work_schedule(&blink_work, BLINK_ONOFF);
}

static int blink_setup(void)
{
	int err;

	printk("Checking LED device...");
	if (!gpio_is_ready_dt(&led)) {
		printk("failed.\n");
		return -EIO;
	}
	printk("done.\n");

	printk("Configuring GPIO pin...");
	err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (err) {
		printk("failed.\n");
		return -EIO;
	}
	printk("done.\n");

	k_work_init_delayable(&blink_work, blink_timeout);

	return 0;
}

static void blink_start(void)
{
	printk("Start blinking LED...\n");
	led_is_on = false;
	gpio_pin_set(led.port, led.pin, (int)led_is_on);
	k_work_schedule(&blink_work, BLINK_ONOFF);
}

static void blink_stop(void)
{
	struct k_work_sync work_sync;

	printk("Stop blinking LED.\n");
	k_work_cancel_delayable_sync(&blink_work, &work_sync);

	/* Keep LED on */
	//led_is_on = true;
	led_is_on = false;
	
	gpio_pin_set(led.port, led.pin, (int)led_is_on);
}
#endif /* LED0_NODE */
#endif /* CONFIG_GPIO */

int main(void)
{
	int err;
	uint8_t shift_text =0;
	uint8_t i;
	uint8_t scr=0;
	
	display_scr[0] = (uint8_t*)&nxp_scr;
	display_scr[1] = (uint8_t*)&bsh_scr;
	display_scr[2] = (uint8_t*)&zephyr_scr;
	display_scr[3] = (uint8_t*)&demo_scr;
	display_scr[4] = (uint8_t*)&clock_scr1;
	display_scr[5] = (uint8_t*)&clock_scr2;
	display_scr[6] = (uint8_t*)&clock_scr3;

    // LED display driver init
	if (!device_is_ready(led_dev)) {
		printk("Warning: LED device is not ready\n");
	} else {
		printk("LED device initialized\n");
	}

    // BT Init continues here
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	bt_conn_auth_cb_register(&auth_cb_display);

	bt_hrs_cb_register(&hrs_cb);

#if !defined(CONFIG_BT_EXT_ADV)
	printk("Starting Legacy Advertising (connectable and scannable)\n");
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

#else /* CONFIG_BT_EXT_ADV */
	struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0U,
		.secondary_max_skip = 0U,
		.options = (BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_CODED),
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
		.peer = NULL,
	};
	struct bt_le_ext_adv *adv;

	printk("Creating a Coded PHY connectable non-scannable advertising set\n");
	err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
	if (err) {
		printk("Failed to create Coded PHY extended advertising set (err %d)\n", err);

		printk("Creating a non-Coded PHY connectable non-scannable advertising set\n");
		adv_param.options &= ~BT_LE_ADV_OPT_CODED;
		err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
		if (err) {
			printk("Failed to create extended advertising set (err %d)\n", err);
			return 0;
		}
	}

	printk("Setting extended advertising data\n");
	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Failed to set extended advertising data (err %d)\n", err);
		return 0;
	}

	printk("Starting Extended Advertising (connectable non-scannable)\n");
	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		printk("Failed to start extended advertising set (err %d)\n", err);
		return 0;
	}
#endif /* CONFIG_BT_EXT_ADV */

	printk("Advertising successfully started\n");

#if defined(HAS_LED)
	err = blink_setup();
	if (err) {
		return 0;
	}

	blink_stop(); // Indicate BT connection
#endif /* HAS_LED */

	/* Implement notification. */
	while (1) {
		k_sleep(K_MSEC(1000));

		/* Heartrate measurements simulation */
		hrs_notify();

		/* Battery level simulation */
		bas_notify();

        // LED display animations
        led_indicate_bt_status();

		//led_show_scr(nxp_scr, 0);
		//led_show_scr(display_scr[1], 0);

		led_show_scr(display_scr[scr], BRIGHT, 0);
		scr++;

        if (scr >= NUM_SCREENS) 
		{
			
			/*
			for(i=BRIGHT;i<100;i++){
				//i++;
            	led_show_scr(clock_scr3, i, 0); // Brightness ramp up
				k_msleep(5);
			}
			*/
			
			for(i=BRIGHT;i>0;i--){
            	led_show_scr(clock_scr3, i, 0); // Brightness ramp down
				k_msleep(5);
			}
			for(i=0;i<100;i++){
				i++;
            	led_show_scr(clock_scr3, i, 0); // Brightness ramp up
				k_msleep(5);
			}
		scr = 0U;
		}
		
		if (atomic_test_and_clear_bit(state, STATE_CONNECTED)) {
			/* Connected callback executed */

#if defined(HAS_LED)
			blink_stop();
#endif /* HAS_LED */
		} else if (atomic_test_and_clear_bit(state, STATE_DISCONNECTED)) {
#if !defined(CONFIG_BT_EXT_ADV)
			printk("Starting Legacy Advertising (connectable and scannable)\n");
			err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
					      ARRAY_SIZE(sd));
			if (err) {
				printk("Advertising failed to start (err %d)\n", err);
				return 0;
			}

#else /* CONFIG_BT_EXT_ADV */
			printk("Starting Extended Advertising (connectable and non-scannable)\n");
			err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
			if (err) {
				printk("Failed to start extended advertising set (err %d)\n", err);
				return 0;
			}
#endif /* CONFIG_BT_EXT_ADV */

#if defined(HAS_LED)
			blink_start();
#endif /* HAS_LED */
		}
	}

	return 0;
}
