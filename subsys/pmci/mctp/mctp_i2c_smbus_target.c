/*
 * MCTP over SMBus/I2C target binding for Zephyr (DSP0237-style)
 *
 * RX: SMBus Block Write (cmd=0x0F, count, data[count], PEC)
 *     data[0] = Source Slave Address (bit0 must be 1)
 *     data[1..] = MCTP packet bytes (MCTP header + payload)
 *
 * TX: role-switch to controller and emit SMBus Block Write back to BMC address.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <errno.h>

#include "libmctp.h"
#include <zephyr/pmci/mctp/mctp_i2c_smbus_target.h>

LOG_MODULE_REGISTER(mctp_i2c_smbus_target, CONFIG_MCTP_LOG_LEVEL);

/* DSP0237: first data byte is Source Slave Address */
#define MCTP_SMBUS_SRC_ADDR_LEN 1

enum {
	RX_WAIT_CMD = 0,
	RX_WAIT_COUNT,
	RX_WAIT_DATA,
	RX_WAIT_PEC,
	RX_DROP
};

/* SMBus PEC = CRC-8 poly 0x07 init 0x00 */
static uint8_t crc8_update(uint8_t crc, uint8_t data)
{
	crc ^= data;
	for (int i = 0; i < 8; i++) {
		crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
	}
	return crc;
}

/* PEC for SMBus Block Write:
 *   PEC over: (dest_addr<<1 | W), cmd, count, data[count]
 */
static uint8_t smbus_pec_calc(uint8_t dest_addr_7bit, uint8_t cmd,
			      uint8_t count, const uint8_t *data)
{
	uint8_t crc = 0x00;

	crc = crc8_update(crc, (uint8_t)((dest_addr_7bit << 1) | 0u)); /* addr + W */
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

	LOG_DBG("SMBus RX: write_requested");
	return 0;
}

static int tgt_write_received(struct i2c_target_config *config, uint8_t val)
{
	struct mctp_binding_i2c_smbus_target *b = cfg_to_b(config);

	switch (b->rx_state) {
	case RX_WAIT_CMD:
		b->rx_cmd = val;
		b->rx_state = (b->rx_cmd == MCTP_SMBUS_CMD_CODE) ? RX_WAIT_COUNT : RX_DROP;
		LOG_DBG("SMBus RX: CMD=0x%02x", b->rx_cmd);
		return 0;

	case RX_WAIT_COUNT:
		b->rx_count = val;

		/* SMBus block write count: 1..32 (classic SMBus) */
		if (b->rx_count == 0u || b->rx_count > CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX) {
			LOG_WRN("SMBus RX: bad COUNT=%u", b->rx_count);
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
			LOG_WRN("SMBus RX: bad PEC got=0x%02x exp=0x%02x (cmd=0x%02x count=%u)",
				val, exp, b->rx_cmd, b->rx_count);
			b->rx_state = RX_DROP;
			return 0;
		}

		/* DSP0237: first data byte is Source Slave Address */
		if (b->rx_count <= MCTP_SMBUS_SRC_ADDR_LEN) {
			LOG_WRN("SMBus RX: too short (count=%u)", b->rx_count);
			b->rx_state = RX_WAIT_CMD;
			return 0;
		}

		const uint8_t src_sa = b->rx_buf[0];
		if ((src_sa & 0x01u) == 0u) {
			LOG_WRN("SMBus RX: Source SA bit0=0 (0x%02x)", src_sa);
			/* Keep going for debug; can drop if you want strictness */
		}

		const uint8_t *mctp_bytes = &b->rx_buf[MCTP_SMBUS_SRC_ADDR_LEN];
		const size_t mctp_len = (size_t)b->rx_count - MCTP_SMBUS_SRC_ADDR_LEN;

		if (mctp_len < 4) {
			LOG_WRN("SMBus RX: MCTP too short (len=%u)", (unsigned)mctp_len);
			b->rx_state = RX_WAIT_CMD;
			return 0;
		}

		/* Optional sanity: MCTP header version is low nibble and should be 0x1 */
		if ((mctp_bytes[0] & 0x0Fu) != 0x01u) {
			LOG_WRN("SMBus RX: unexpected MCTP hdr ver byte0=0x%02x", mctp_bytes[0]);
		}

		struct mctp_pktbuf *pkt = mctp_pktbuf_alloc(&b->binding, mctp_len);
		if (!pkt) {
			LOG_WRN("SMBus RX: pktbuf alloc failed (len=%u)", (unsigned)mctp_len);
			b->rx_state = RX_DROP;
			return 0;
		}

		memcpy(pkt->data + pkt->mctp_hdr_off, mctp_bytes, mctp_len);
		pkt->start = pkt->mctp_hdr_off;
		pkt->end   = pkt->start + mctp_len;

		LOG_DBG("SMBus RX: deliver mctp_len=%u (src_sa=0x%02x)", (unsigned)mctp_len, src_sa);

		/* Hand off to libmctp core */
		mctp_bus_rx(&b->binding, pkt);

		/* Ready for next */
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

	/* If STOP happens before PEC, drop incomplete frame */
	if (b->rx_state == RX_WAIT_PEC) {
		LOG_WRN("SMBus RX: STOP before PEC (cmd=0x%02x count=%u idx=%u) dropping",
			b->rx_cmd, b->rx_count, b->rx_idx);
	}

	/* Reset RX state for next transaction */
	b->rx_state = RX_WAIT_CMD;
	b->rx_cmd = 0;
	b->rx_count = 0;
	b->rx_idx = 0;

	return 0;
}

const struct i2c_target_callbacks mctp_i2c_smbus_target_callbacks = {
	.write_requested = tgt_write_requested,
	.write_received  = tgt_write_received,
	.stop            = tgt_stop,
};

static void tx_work_fn(struct k_work *work)
{
	struct mctp_binding_i2c_smbus_target *b =
		CONTAINER_OF(work, struct mctp_binding_i2c_smbus_target, tx_work);

	uint8_t mctp_bytes[CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX];
	uint8_t mctp_len;
	uint8_t out[2 + CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX + 1];
	size_t tx_sz;
	int rc = 0;
	int rc2 = 0;

	LOG_INF("SMBus TX worker start");

	/* Fetch pending TX */
	k_sem_take(b->tx_lock, K_FOREVER);
	if (!b->tx_pending) {
		k_sem_give(b->tx_lock);
		LOG_DBG("SMBus TX: no pending packet");
		return;
	}

	mctp_len = b->tx_len;
	memcpy(mctp_bytes, b->tx_buf, mctp_len);
	b->tx_pending = false;
	k_sem_give(b->tx_lock);

	LOG_INF("SMBus TX entry: mctp_len=%u", (unsigned)mctp_len);
	if (mctp_len > 0U) {
		char line[3 * 32 + 1];
		size_t n = (mctp_len > 32U) ? 32U : (size_t)mctp_len;
		size_t o = 0U;

		for (size_t i = 0; i < n; i++) {
			o += snprintk(&line[o], sizeof(line) - o, "%02x ", mctp_bytes[i]);
		}
		line[o ? (o - 1U) : 0U] = '\0';
		LOG_INF("SMBus TX MCTP +0: %s", line);
	}

	/* SMBus block write: cmd, count, data[count], pec
	 * count = 1 (src_sa) + mctp_len
	 */
	if ((1U + mctp_len) > CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX) {
		LOG_WRN("SMBus TX: too large (mctp_len=%u)", (unsigned)mctp_len);
		return;
	}

	out[0] = MCTP_SMBUS_CMD_CODE;
	out[1] = (uint8_t)(1U + mctp_len);

	/* Data[0] = Source SA (our 7-bit addr + bit0=1 per DSP0237) */
	out[2] = (uint8_t)((b->ep_i2c_addr << 1) | 0x01U);

	/* Data[1..] = MCTP bytes */
	memcpy(&out[3], mctp_bytes, mctp_len);

	/* PEC over destination address (BMC), cmd/count/data */
	out[3 + mctp_len] = smbus_pec_calc(b->bmc_i2c_addr, out[0], out[1], &out[2]);

	tx_sz = 2U + (size_t)out[1] + 1U;

	LOG_INF("SMBus TX write prep: bmc_addr=0x%02x tx_sz=%u",
		b->bmc_i2c_addr, (unsigned)tx_sz);

	{
		char line[3 * 32 + 1];
		size_t n = (tx_sz > 32U) ? 32U : tx_sz;
		size_t o = 0U;

		for (size_t i = 0; i < n; i++) {
			o += snprintk(&line[o], sizeof(line) - o, "%02x ", out[i]);
		}
		line[o ? (o - 1U) : 0U] = '\0';
		LOG_INF("SMBus TX OUT +0: %s", line);
	}

	/*
	 * Role-switch:
	 * 1) stop target mode
	 * 2) wait a bit so BMC can release/settle
	 * 3) write as controller with retries
	 * 4) re-enable target mode
	 */
	rc2 = i2c_target_unregister(b->i2c, &b->i2c_target_cfg);
	if (rc2) {
		LOG_WRN("SMBus TX: target unregister rc=%d", rc2);
	}
	printk("\n[SSUMIT] ROLE CHANGE --> i2c_target_unregister passed \n");
	/* This is workqueue context, so sleeping is better than busy-waiting */
	k_sleep(K_MSEC(2));

	for (int attempt = 0; attempt < 5; attempt++) {
		rc = i2c_write(b->i2c, out, tx_sz, b->bmc_i2c_addr);

	printk("\n[SSUMIT] ROLE CHANGE --> i2c_write return = %d \n",rc);
		LOG_INF("SMBus TX attempt=%d rc=%d", attempt + 1, rc);

		if (rc == 0) {
			break;
		}

		/* give the BMC slave side time to re-arm */
		k_sleep(K_MSEC(2 + attempt));
	}

	/* let the bus settle before re-registering target mode */
	k_sleep(K_MSEC(2));

	rc2 = i2c_target_register(b->i2c, &b->i2c_target_cfg);
	if (rc2) {
		LOG_ERR("SMBus TX: re-register target failed rc=%d", rc2);
	} else {
		LOG_INF("SMBus TX: target re-registered");
	}

	printk("\n[SSUMIT] i2c_target_re-register return code =%d \n",rc2);
	if (rc) {
		LOG_WRN("SMBus TX: i2c_write ultimately failed rc=%d", rc);
	} else {
		LOG_INF("SMBus TX: i2c_write OK");
	}

	LOG_INF("SMBus TX done: write_rc=%d rereg_rc=%d", rc, rc2);
}
#if 0
/* ---- TX role-switch work item ----
 * libmctp calls binding->tx() with an MCTP packet.
 * We enqueue the raw MCTP bytes and then perform a controller-side SMBus Block Write to bmc_i2c_addr.
 */
static void tx_work_fn(struct k_work *work)
{
	struct mctp_binding_i2c_smbus_target *b =
		CONTAINER_OF(work, struct mctp_binding_i2c_smbus_target, tx_work);

	uint8_t mctp_bytes[CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX];
	uint8_t mctp_len;

	/* Fetch pending TX */
	k_sem_take(b->tx_lock, K_FOREVER);
	if (!b->tx_pending) {
		k_sem_give(b->tx_lock);
		return;
	}
	mctp_len = b->tx_len;
	memcpy(mctp_bytes, b->tx_buf, mctp_len);
	b->tx_pending = false;
	k_sem_give(b->tx_lock);

	/* SMBus block write: cmd, count, data[count], pec
	 * count = 1 (src_sa) + mctp_len
	 */
	if ((1u + mctp_len) > CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX) {
		LOG_WRN("SMBus TX: too large (mctp_len=%u)", (unsigned)mctp_len);
		return;
	}

	uint8_t out[2 + CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX + 1];

	out[0] = MCTP_SMBUS_CMD_CODE;
	out[1] = (uint8_t)(1u + mctp_len);

	/* Data[0] = Source SA (our 7-bit addr + bit0=1 per DSP0237) */
	out[2] = (uint8_t)((b->ep_i2c_addr << 1) | 0x01u);

	/* Data[1..] = MCTP bytes */
	memcpy(&out[3], mctp_bytes, mctp_len);

	/* PEC over destination address (BMC), cmd/count/data */
	out[3 + mctp_len] = smbus_pec_calc(b->bmc_i2c_addr, out[0], out[1], &out[2]);

	const size_t tx_sz = 2u + (size_t)out[1] + 1u;

	LOG_DBG("SMBus TX: role-switch write to 0x%02x bytes=%u", b->bmc_i2c_addr, (unsigned)tx_sz);

	k_busy_wait(500);
	/* Role-switch: temporarily stop being a target, write as controller, then re-enable target */
	(void)i2c_target_unregister(b->i2c, &b->i2c_target_cfg);
	
	LOG_INF("SMBus TX: writing to BMC addr 0x%02x sz=%u", b->bmc_i2c_addr, (unsigned)tx_sz);

	int rc = i2c_write(b->i2c, out, tx_sz, b->bmc_i2c_addr);

	LOG_INF("SMBus TX rc=%d", rc);

	/* allow bus to settle again */
	k_busy_wait(500);

	int rc2 = i2c_target_register(b->i2c, &b->i2c_target_cfg);

	if (rc) {
		LOG_WRN("SMBus TX: i2c_write failed rc=%d", rc);
	}
	if (rc2) {
		LOG_ERR("SMBus TX: re-register target failed rc=%d", rc2);
	}

	LOG_DBG("SMBus TX: done (write rc=%d, rereg rc=%d)", rc, rc2);
}
#endif
int mctp_i2c_smbus_target_tx(struct mctp_binding *binding, struct mctp_pktbuf *pkt)
{
	struct mctp_binding_i2c_smbus_target *b =
		CONTAINER_OF(binding, struct mctp_binding_i2c_smbus_target, binding);

	const size_t mctp_len = mctp_pktbuf_size(pkt);
	
	LOG_INF("BINDING TX entry: pkt=%p size=%u", pkt, (unsigned)mctp_pktbuf_size(pkt));

	if (mctp_len == 0 || mctp_len > CONFIG_MCTP_I2C_SMBUS_BLOCK_MAX) {
		return -EMSGSIZE;
	}

	k_sem_take(b->tx_lock, K_FOREVER);

	if (b->tx_pending) {
		k_sem_give(b->tx_lock);
		return -EBUSY;
	}

	memcpy(b->tx_buf, pkt->data + pkt->mctp_hdr_off, mctp_len);
	b->tx_len = (uint8_t)mctp_len;
	b->tx_pending = true;

	k_sem_give(b->tx_lock);

	k_work_submit(&b->tx_work);
	return 0;
}

int mctp_i2c_smbus_target_start(struct mctp_binding *binding)
{
	struct mctp_binding_i2c_smbus_target *b =
		CONTAINER_OF(binding, struct mctp_binding_i2c_smbus_target, binding);
//printk("\n[SSUMIT]mctp_i2c_smbus_target_start\n");
LOG_INF("\n[Target_Start]BIND START: binding=%p i2c=%p\n", binding, b->i2c);
	if (!device_is_ready(b->i2c)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	k_work_init(&b->tx_work, tx_work_fn);

	LOG_INF("SMBus binding: i2c=%s ep_addr=0x%02x bmc_addr=0x%02x eid=0x%02x",
		b->i2c->name, b->ep_i2c_addr, b->bmc_i2c_addr, b->endpoint_id);

	int rc = i2c_target_register(b->i2c, &b->i2c_target_cfg);
	if (rc) {
		LOG_ERR("i2c_target_register(addr=0x%02x) failed: %d", b->ep_i2c_addr, rc);
		return rc;
	}
LOG_INF("\n[Target Start]BIND START done: binding=%p\n", binding);
	mctp_binding_set_tx_enabled(binding, true);
	return 0;
}
