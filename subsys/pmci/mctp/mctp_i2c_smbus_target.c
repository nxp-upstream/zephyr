#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mctp_i2c_smbus_target, CONFIG_MCTP_LOG_LEVEL);

#include <string.h>
#include "libmctp.h"
#include <zephyr/pmci/mctp/mctp_i2c_smbus_target.h>

enum { RX_WAIT_CMD = 0, RX_WAIT_COUNT, RX_WAIT_DATA, RX_WAIT_PEC, RX_DROP };

/* SMBus PEC = CRC-8 poly 0x07 init 0x00 */
static uint8_t crc8_update(uint8_t crc, uint8_t data)
{
	crc ^= data;
	for (int i = 0; i < 8; i++) {
		crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
	}
	return crc;
}

static uint8_t smbus_pec_calc(uint8_t dest_addr_7bit, uint8_t cmd,
			      uint8_t count, const uint8_t *data)
{
	uint8_t crc = 0x00;

	/* address + write bit */
	crc = crc8_update(crc, (uint8_t)((dest_addr_7bit << 1) | 0u));
	crc = crc8_update(crc, cmd);
	crc = crc8_update(crc, count);
	for (uint8_t i = 0; i < count; i++) {
		crc = crc8_update(crc, data[i]);
	}
	return crc;
}

static inline struct mctp_binding_i2c_smbus_target *cfg_to_b(struct i2c_target_config *cfg)
{
	return CONTAINER_OF(cfg, struct mctp_binding_i2c_smbus_target, i2c_target_cfg);
}

static int tgt_write_requested(struct i2c_target_config *config)
{
	struct mctp_binding_i2c_smbus_target *b = cfg_to_b(config);

	b->rx_state = RX_WAIT_CMD;
	b->rx_cmd = 0;
	b->rx_count = 0;
	b->rx_idx = 0;
	return 0;
}

static int tgt_write_received(struct i2c_target_config *config, uint8_t val)
{
	struct mctp_binding_i2c_smbus_target *b = cfg_to_b(config);

	switch (b->rx_state) {
	case RX_WAIT_CMD:
		b->rx_cmd = val;
		b->rx_state = (b->rx_cmd == MCTP_SMBUS_CMD_CODE) ? RX_WAIT_COUNT : RX_DROP;
		return 0;

	case RX_WAIT_COUNT:
		b->rx_count = val;
		if (b->rx_count == 0u || b->rx_count > CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX) {
			b->rx_state = RX_DROP;
		} else {
			b->rx_idx = 0;
			b->rx_state = RX_WAIT_DATA;
		}
		return 0;

	case RX_WAIT_DATA:
		if (b->rx_idx >= b->rx_count) {
			b->rx_state = RX_DROP;
			return 0;
		}
		b->rx_buf[b->rx_idx++] = val;
		if (b->rx_idx == b->rx_count) {
			b->rx_state = RX_WAIT_PEC;
		}
		return 0;

	case RX_WAIT_PEC: {
		const uint8_t exp = smbus_pec_calc(b->ep_i2c_addr, b->rx_cmd, b->rx_count, b->rx_buf);
		if (val != exp) {
			LOG_WRN("Bad PEC: got 0x%02x exp 0x%02x", val, exp);
			b->rx_state = RX_DROP;
			return 0;
		}

		/* b->rx_buf contains the on-wire MCTP packet (hdr+payload). */
		struct mctp_pktbuf *pkt = mctp_pktbuf_alloc(&b->binding, (size_t)b->rx_count);
		if (!pkt) {
			LOG_WRN("pktbuf alloc failed (len=%u)", b->rx_count);
			b->rx_state = RX_DROP;
			return 0;
		}

		memcpy(pkt->data + pkt->mctp_hdr_off, b->rx_buf, b->rx_count);
		pkt->start = pkt->mctp_hdr_off;
		pkt->end = pkt->start + b->rx_count;

		mctp_bus_rx(&b->binding, pkt);

		b->rx_state = RX_WAIT_CMD;
		return 0;
	}

	case RX_DROP:
	default:
		return 0;
	}
}

static int tgt_stop(struct i2c_target_config *config)
{
	struct mctp_binding_i2c_smbus_target *b = cfg_to_b(config);

	if (b->rx_state == RX_DROP) {
		b->rx_state = RX_WAIT_CMD;
	}
	return 0;
}

const struct i2c_target_callbacks mctp_i2c_smbus_target_callbacks = {
	.write_requested = tgt_write_requested,
	.write_received = tgt_write_received,
	.stop = tgt_stop,
};

/* ---- TX role-switch work item ---- */
static void tx_work_fn(struct k_work *work)
{
	struct mctp_binding_i2c_smbus_target *b =
		CONTAINER_OF(work, struct mctp_binding_i2c_smbus_target, tx_work);

	uint8_t payload[CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX];
	uint8_t len;

	k_sem_take(b->tx_lock, K_FOREVER);
	if (!b->tx_pending) {
		k_sem_give(b->tx_lock);
		return;
	}
	len = b->tx_len;
	memcpy(payload, b->tx_buf, len);
	b->tx_pending = false;
	k_sem_give(b->tx_lock);

	/* SMBus block write: cmd,count,data...,pec */
	uint8_t out[2 + CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX + 1];
	out[0] = MCTP_SMBUS_CMD_CODE;
	out[1] = len;
	memcpy(&out[2], payload, len);
	out[2 + len] = smbus_pec_calc(b->bmc_i2c_addr, out[0], out[1], &out[2]);

	/* Role-switch: temporarily stop target, write as controller, re-enable target */
	(void)i2c_target_unregister(b->i2c, &b->i2c_target_cfg);
	int rc = i2c_write(b->i2c, out, (size_t)(2 + len + 1), b->bmc_i2c_addr);
	if (rc) {
		LOG_WRN("TX i2c_write to 0x%02x failed: %d", b->bmc_i2c_addr, rc);
	}
	rc = i2c_target_register(b->i2c, &b->i2c_target_cfg);
	if (rc) {
		LOG_ERR("re-register target failed: %d", rc);
	}
}

int mctp_i2c_smbus_target_tx(struct mctp_binding *binding, struct mctp_pktbuf *pkt)
{
	struct mctp_binding_i2c_smbus_target *b =
		CONTAINER_OF(binding, struct mctp_binding_i2c_smbus_target, binding);

	const size_t pkt_len = mctp_pktbuf_size(pkt);
	if (pkt_len == 0 || pkt_len > CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX) {
		return -EMSGSIZE;
	}

	k_sem_take(b->tx_lock, K_FOREVER);
	if (b->tx_pending) {
		k_sem_give(b->tx_lock);
		return -EBUSY;
	}

	memcpy(b->tx_buf, pkt->data + pkt->mctp_hdr_off, pkt_len);
	b->tx_len = (uint8_t)pkt_len;
	b->tx_pending = true;
	k_sem_give(b->tx_lock);

	k_work_submit(&b->tx_work);
	return 0;
}

int mctp_i2c_smbus_target_start(struct mctp_binding *binding)
{
	struct mctp_binding_i2c_smbus_target *b =
		CONTAINER_OF(binding, struct mctp_binding_i2c_smbus_target, binding);

	if (!device_is_ready(b->i2c)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	k_work_init(&b->tx_work, tx_work_fn);

	int rc = i2c_target_register(b->i2c, &b->i2c_target_cfg);
	if (rc) {
		LOG_ERR("i2c_target_register(0x%02x) failed: %d", b->ep_i2c_addr, rc);
		return rc;
	}
LOG_INF("SMBus target start: i2c=%s ep_addr=0x%02x bmc_addr=0x%02x eid=%u",
        b->i2c->name, b->ep_i2c_addr, b->bmc_i2c_addr, b->endpoint_id);

LOG_INF("Registering I2C target at 0x%02x", b->i2c_target_cfg.address);

	mctp_binding_set_tx_enabled(binding, true);
	return 0;
}

