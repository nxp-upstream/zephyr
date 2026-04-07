#ifndef ZEPHYR_INCLUDE_ZEPHYR_PMCI_MCTP_I2C_SMBUS_TARGET_H_
#define ZEPHYR_INCLUDE_ZEPHYR_PMCI_MCTP_I2C_SMBUS_TARGET_H_

#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include <libmctp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DSP0237 SMBus/I2C command code. */
#define MCTP_SMBUS_CMD_CODE 0x0F

/* First SMBus payload byte is the source slave address. */
#define MCTP_I2C_SMBUS_SRC_ADDR_LEN 1U

/* Maximum MCTP bytes that fit after the source slave address byte. */
#define MCTP_I2C_SMBUS_MAX_MCTP_BYTES \
	(CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX - MCTP_I2C_SMBUS_SRC_ADDR_LEN)

struct mctp_binding_i2c_smbus_target {
	struct mctp_binding binding;

	const struct device *i2c;
	struct i2c_target_config i2c_target_cfg;

	uint8_t endpoint_id;
	uint8_t ep_i2c_addr;
	uint8_t remote_i2c_addr;

	/* RX state. */
	uint8_t rx_state;
	uint8_t rx_cmd;
	uint8_t rx_count;
	uint8_t rx_idx;
	uint8_t rx_buf[CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX];

	/* TX state. */
	struct k_sem *tx_lock;
	struct k_work tx_work;
	bool tx_pending;
	uint8_t tx_len;
	uint8_t tx_buf[CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX];
};

int mctp_i2c_smbus_target_start(struct mctp_binding *binding);
int mctp_i2c_smbus_target_tx(struct mctp_binding *binding,
			     struct mctp_pktbuf *pkt);

extern const struct i2c_target_callbacks mctp_i2c_smbus_target_callbacks;

#define MCTP_I2C_SMBUS_TARGET_DT_DEFINE(_name, _node_id)		\
	K_SEM_DEFINE(_name##_tx_lock, 1, 1);				\
	static struct mctp_binding_i2c_smbus_target _name = {		\
		.binding = {						\
			.name = STRINGIFY(_name),			\
			.version = 1,					\
			.start = mctp_i2c_smbus_target_start,		\
			.tx = mctp_i2c_smbus_target_tx,			\
			.pkt_size = MCTP_I2C_SMBUS_MAX_MCTP_BYTES,	\
		},							\
		.i2c = DEVICE_DT_GET(DT_PHANDLE(_node_id, i2c)),	\
		.i2c_target_cfg = {					\
			.address = DT_PROP(_node_id, i2c_addr),		\
			.callbacks = &mctp_i2c_smbus_target_callbacks,	\
		},							\
		.endpoint_id = DT_PROP(_node_id, endpoint_id),		\
		.ep_i2c_addr = DT_PROP(_node_id, i2c_addr),		\
		.remote_i2c_addr = DT_PROP(_node_id, remote_i2c_addr),	\
		.tx_lock = &_name##_tx_lock,				\
	}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_PMCI_MCTP_I2C_SMBUS_TARGET_H_ */
