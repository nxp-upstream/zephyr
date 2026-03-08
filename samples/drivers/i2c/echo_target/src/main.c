#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(i2c_target_test, LOG_LEVEL_INF);

#define I2C_TARGET_ADDR 0x10
#define BMC_TARGET_ADDR 0x11
#define REG_COUNT 16

static const struct device *i2c;

/* Simple register file for target-mode test */
static uint8_t registers[REG_COUNT];
static uint8_t current_reg;
static bool first_byte;

/* Work item to send bytes to BMC after STOP */
static struct k_work send_work;
static atomic_t send_pending;

/* Forward decl */
static void send_to_bmc_workfn(struct k_work *work);

static int write_requested(struct i2c_target_config *config)
{
	first_byte = true; /* reset per transaction */
	LOG_INF("Write requested");
	return 0;
}

static int write_received(struct i2c_target_config *config, uint8_t val)
{
	if (first_byte) {
		current_reg = val;
		first_byte = false;
		LOG_INF("Set register pointer to 0x%02x", current_reg);
	} else {
		if (current_reg < REG_COUNT) {
			registers[current_reg++] = val;
		}
	}
	return 0;
}
#if 1
static int read_requested(struct i2c_target_config *config, uint8_t *val)
{
	LOG_INF("Read requested (reg=0x%02x)", current_reg);

	if (current_reg < REG_COUNT) {
		*val = registers[current_reg];
	} else {
		*val = 0xFF;
	}
	return 0;
}

static int read_processed(struct i2c_target_config *config, uint8_t *val)
{
	LOG_INF("Read processed (reg=0x%02x)", current_reg);

	if (current_reg < REG_COUNT) {
		*val = registers[current_reg++];
	} else {
		*val = 0xFF;
	}
	return 0;
}
#endif
static int stop(struct i2c_target_config *config)
{
	first_byte = false;
	LOG_INF("STOP condition");

	/*
	 * Trigger a controller-mode send after STOP (safe point to switch roles).
	 * We only schedule once until it runs.
	 */
#if 0
	if (atomic_cas(&send_pending, 0, 1)) {
		k_work_submit(&send_work);
	}
#endif
	return 0;
}

static const struct i2c_target_callbacks target_callbacks = {
	.write_requested = write_requested,
	.write_received  = write_received,
	.read_requested  = read_requested,
	.read_processed  = read_processed,
	.stop            = stop,
};

static struct i2c_target_config target_config = {
	.address = I2C_TARGET_ADDR,
	.callbacks = &target_callbacks,
};

static void send_to_bmc_workfn(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Example payload for i2c-slave-eeprom: [offset][data...] */
	uint8_t buf[] = { 0x00, 0xDE, 0xAD, 0xBE, 0xEF };

	LOG_INF("Sending %u bytes to BMC target 0x%02x", (unsigned)sizeof(buf), BMC_TARGET_ADDR);

	/*
	 * Safest approach in Zephyr: temporarily unregister target,
	 * perform controller write, then re-register target.
	 */
	(void)i2c_target_unregister(i2c, &target_config);

	int rc = i2c_write(i2c, buf, sizeof(buf), BMC_TARGET_ADDR);
	if (rc) {
		LOG_WRN("i2c_write to 0x%02x failed: %d", BMC_TARGET_ADDR, rc);
	} else {
		LOG_INF("i2c_write to 0x%02x OK", BMC_TARGET_ADDR);
	}

	rc = i2c_target_register(i2c, &target_config);
	if (rc) {
		LOG_ERR("Failed to re-register target: %d", rc);
	} else {
		LOG_INF("Re-registered target at 0x%02x", I2C_TARGET_ADDR);
	}

	atomic_set(&send_pending, 0);
}

int main(void)
{
	i2c = DEVICE_DT_GET(DT_NODELABEL(mikrobus_i2c));

	if (!device_is_ready(i2c)) {
		LOG_ERR("I2C device not ready");
		return -1;
	}

	k_work_init(&send_work, send_to_bmc_workfn);
	atomic_set(&send_pending, 0);

	int ret = i2c_target_register(i2c, &target_config);
	if (ret) {
		LOG_ERR("Failed to register target: %d", ret);
		return ret;
	}

	LOG_INF("I2C target ready at address 0x%02x", I2C_TARGET_ADDR);
	LOG_INF("Will send to BMC target at 0x%02x after first STOP", BMC_TARGET_ADDR);

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
