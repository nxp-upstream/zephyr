#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_target_test, LOG_LEVEL_INF);

#define I2C_TARGET_ADDR 0x10
#define REG_COUNT 16

static uint8_t registers[REG_COUNT];
static uint8_t current_reg;
static bool first_byte;

static int write_requested(struct i2c_target_config *config)
{
    first_byte = true;               // IMPORTANT: reset per transaction
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

static int read_requested(struct i2c_target_config *config, uint8_t *val)
{
    LOG_INF("Read requested (reg=0x%02x)", current_reg);

    if (current_reg < REG_COUNT) {
        *val = registers[current_reg++];
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

static int stop(struct i2c_target_config *config)
{
    first_byte = 0;
    LOG_INF("STOP condition");
    return 0;
}

static const struct i2c_target_callbacks target_callbacks = {
    .write_requested = write_requested,
    .write_received  = write_received,
    .read_requested  = read_requested,
    .read_processed  = read_processed,   // add this
    .stop            = stop,
};

static struct i2c_target_config target_config = {
    .address = I2C_TARGET_ADDR,
    .callbacks = &target_callbacks,
};

int main(void)
{
    const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(mikrobus_i2c));

    if (!device_is_ready(i2c)) {
        LOG_ERR("I2C device not ready");
        return -1;
    }

    int ret = i2c_target_register(i2c, &target_config);
    if (ret) {
        LOG_ERR("Failed to register target: %d", ret);
        return ret;
    }

    LOG_INF("I2C target ready at address 0x%02x", I2C_TARGET_ADDR);

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
