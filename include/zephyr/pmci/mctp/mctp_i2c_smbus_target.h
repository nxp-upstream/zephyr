#ifndef ZEPHYR_MCTP_I2C_SMBUS_TARGET_H_
#define ZEPHYR_MCTP_I2C_SMBUS_TARGET_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/devicetree.h>
#include <libmctp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DSP0237 SMBus/I2C command code */
#define MCTP_SMBUS_CMD_CODE 0x0F

/* Classic SMBus Block Write max count */
#ifndef CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX
#define CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX 32
#endif

/* DSP0237: first data byte is Source Slave Address */
#define MCTP_I2C_SMBUS_SRC_ADDR_LEN 1

/* Maximum MCTP packet bytes (MCTP hdr + payload) that can fit after src_sa */
#define MCTP_I2C_SMBUS_MAX_MCTP_BYTES (CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX - MCTP_I2C_SMBUS_SRC_ADDR_LEN)

struct mctp_binding_i2c_smbus_target {
	struct mctp_binding binding;

	const struct device *i2c;
	struct i2c_target_config i2c_target_cfg;

	uint8_t endpoint_id;

	/* Our target addr (7-bit) and BMC host slave addr (7-bit) */
	uint8_t ep_i2c_addr;
	uint8_t bmc_i2c_addr;

	/* RX */
	uint8_t rx_state;
	uint8_t rx_cmd;
	uint8_t rx_count;
	uint8_t rx_idx;
	uint8_t rx_buf[CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX];

	/* TX role-switch serialization + work */
	struct k_sem *tx_lock;
	struct k_work tx_work;

	bool tx_pending;
	uint8_t tx_len; /* total bytes in tx_buf[] = 1(src_sa) + mctp_len */
	uint8_t tx_buf[CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX];
};

int mctp_i2c_smbus_target_start(struct mctp_binding *binding);
int mctp_i2c_smbus_target_tx(struct mctp_binding *binding, struct mctp_pktbuf *pkt);

extern const struct i2c_target_callbacks mctp_i2c_smbus_target_callbacks;

/*
 * DeviceTree helper
 *
 * DT node properties expected (from your overlay):
 *  - i2c: phandle to controller (e.g. &mikrobus_i2c)
 *  - i2c-addr: 7-bit SMC target address (e.g. 0x10)
 *  - bmc-i2c-addr: 7-bit BMC host slave address (e.g. 0x11)
 *  - endpoint-id: MCTP EID (e.g. 9)
 */
#define MCTP_I2C_SMBUS_TARGET_DT_DEFINE(_name, _node_id)					\
	K_SEM_DEFINE(_name##_tx_lock, 1, 1);							\
	static struct mctp_binding_i2c_smbus_target _name = {					\
		.binding = {									\
			.name = STRINGIFY(_name),						\
			.version = 1,								\
			.start = mctp_i2c_smbus_target_start,					\
			.tx = mctp_i2c_smbus_target_tx,						\
			/* libmctp sees only MCTP bytes; bus can carry 1 extra src_sa */	\
			.pkt_size = MCTP_I2C_SMBUS_MAX_MCTP_BYTES,				\
		},										\
		.i2c = DEVICE_DT_GET(DT_PHANDLE(_node_id, i2c)),				\
		.i2c_target_cfg = {								\
			.address = DT_PROP(_node_id, i2c_addr),					\
			.callbacks = &mctp_i2c_smbus_target_callbacks,				\
		},										\
		.endpoint_id = DT_PROP(_node_id, endpoint_id),					\
		.ep_i2c_addr = DT_PROP(_node_id, i2c_addr),					\
		.bmc_i2c_addr = DT_PROP(_node_id, bmc_i2c_addr),				\
		.tx_lock = &_name##_tx_lock,							\
	};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_MCTP_I2C_SMBUS_TARGET_H_ */
