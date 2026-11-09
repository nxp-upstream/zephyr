/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Emulator of the NXP NAFE13388 analog front-end, see nafe13388_emul.h.
 *
 * Written from the data sheet (Rev. 5.0) and independent of the driver. Where
 * the data sheet does not settle the behaviour of the chip, the emulator makes
 * an ASSUMPTION. These are the assumptions (a passing test only proves that the
 * driver agrees with them, not that the chip behaves like this):
 *  - A command word is DEV_AD (bit 15), a 14 bit field (RW_L and 13 address
 *    bits) and bit 0 = 0. Fields with RW_L = 1 and an address of 0x00..0x05 are
 *    the conversion commands (Table 25), the others are register reads.
 *  - The chip accepts nothing but SYS_STATUS0 reads and CMD_RESET while
 *    CHIP_READY is 0 (the data sheet only says CHIP_READY is high when the chip
 *    is operational). CHIP_READY also drops after CMD_CLEAR_REG and CMD_RELOAD
 *    (the data sheet only says so for CMD_RELOAD).
 *  - After a reset the first SYS_STATUS0 reads can return 0xffff (reset_ones, default 0).
 *    Whether this is a not yet ready chip or a stuck bus is not known. A frame other
 *    than a SYS_STATUS0 read is refused while these reads are left.
 *  - CMD_SS and CMD_SC convert the channel selected by CMD_CHn, CMD_MM and
 *    CMD_MC convert the channels of CH_CONFIG4 (MCH_EN) in index order.
 *  - CMD_MS converts the channel at an internal pointer, which CMD_CHn sets
 *    (the next enabled channel if that one is not enabled) and which moves to
 *    the next channel after every conversion. DRDY follows SYS_CONFIG0.DRDY_PIN_EDGE
 *    (data sheet Figure 28): 0 = a DRDY after every CMD_MS, 1 = a DRDY only after
 *    the last enabled channel.
 *  - CMD_END lets the running continuous mode finish: SYS_STATUS0 shows the active bit for
 *    end_busy_reads more reads (default 0). CMD_ABORT clears it at once. How long the
 *    real chip needs is not known.
 *  - CMD_SC and CMD_MC only select the mode, the test calls nafe_emul_tick()
 *    for every conversion round. CMD_END stops without another DRDY.
 *  - A CH_DATAn register can be read with a 4 byte frame; it returns the upper 16 bit of the
 *    24 bit code in both output modes (the data sheet does not say it). A 5 byte frame
 *    always returns the 24 bit code, also in the 16 bit mode.
 *  - CMD_BURST_DATA returns the enabled channels in index order, 3 bytes each,
 *    or 2 bytes each (the upper 16 bit of the 24 bit code) with
 *    ADC_DATA_OUT_16BIT. CH_DATAn itself always holds the 24 bit code.
 *  - With ADC_SYNC = 1 a conversion command only arms the mode and the rising
 *    edge of the SYNC pin starts it.
 *  - Data registers (CH_DATAn) are left alone by CMD_RESET, RSTB, CMD_CLEAR_REG and
 *    CMD_RELOAD. CMD_RESET, RSTB and CMD_CLEAR_REG clear the thresholds
 *    (0x50..0x6F, an assumption); CMD_CLEAR_REG also sets
 *    the gain coefficients (0x80..0x8F) to 0x400000, CMD_RESET, RSTB and CMD_RELOAD
 *    load the factory value 0x404808 (an assumption). CMD_CLEAR_REG keeps CK_SRC_SEL.
 *  - CRC and the status byte are not emulated; enabling them is an error.
 *  - A write to CH_CONFIG0 with HV_SEL = 0 must carry HV_AIP = HV_AIN = 9 + LVSIG_IN
 *    (Table 6), else it is an error.
 */

#define DT_DRV_COMPAT nxp_nafe13388

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "nafe13388_emul.h"

struct nafe_emul_cfg {
	struct gpio_dt_spec drdy;
	struct gpio_dt_spec reset;
	struct gpio_dt_spec sync;
	uint8_t dev_addr;
};

/* Reading modes, in the order of the command codes 0x2000..0x2004 */
enum nafe_emul_mode {
	MODE_SS,
	MODE_SC,
	MODE_MM,
	MODE_MC,
	MODE_MS,
};

/* Instruction commands (Tables 23 to 25) */
#define CMD_ABORT       0x10U
#define CMD_END         0x11U
#define CMD_CLEAR_ALARM 0x12U
#define CMD_CLEAR_DATA  0x13U
#define CMD_RESET       0x14U
#define CMD_CLEAR_REG   0x15U
#define CMD_RELOAD      0x16U
#define CMD_BURST_DATA  0x05U /* with RW_L = 1, like CMD_SS = 0x00 ... CMD_MS = 0x04 */

/* Registers */
#define REG_CH_CONFIG0             0x20U
#define REG_CH_CONFIG4             0x24U
#define REG_SYS_CONFIG0            0x30U
#define REG_SYS_STATUS0            0x31U
#define REG_GLOBAL_ALARM_INTERRUPT 0x33U
#define REG_PN2                    0x7CU
#define REG_PN1                    0x7DU
#define REG_PN0                    0x7EU
#define REG_CH_DATA0               0x40U
#define REG_SERIAL1                0xAEU
#define REG_SERIAL0                0xAFU

#define SYS_CONFIG0_16BIT     BIT(14)
#define SYS_CONFIG0_CK_SRC    GENMASK(9, 8)
#define SYS_CONFIG0_CRC_EN    BIT(7)
#define SYS_CONFIG0_STATUS_EN BIT(6)
#define SYS_CONFIG0_ADC_SYNC  BIT(5)
#define SYS_CONFIG0_DRDY_EDGE BIT(4)
#define CH_CONFIG0_HV_SEL     BIT(4)

#define SYS_STATUS0_SINGLE_ACTIVE BIT(15)
#define SYS_STATUS0_MULTI_ACTIVE  BIT(14)
#define SYS_STATUS0_CHIP_READY    BIT(13)

static int nafe_emul_bad(struct nafe_emul_data *d, const char *msg)
{
	d->errors++;
	printk("nafe13388_emul: protocol error: %s\n", msg);

	return -EIO;
}

static bool reg16_valid(unsigned int a)
{
	return (a >= 0x20U && a <= 0x37U && a != 0x28U) || (a >= 0x7CU && a <= 0x7FU);
}

static bool reg24_valid(unsigned int a)
{
	return (a >= 0x40U && a <= 0x6FU) || (a >= 0x80U && a <= 0xAFU);
}

#define REG_THRS_FIRST 0x50U
#define REG_THRS_LAST  0x6FU
#define REG_GAIN_COEF0 0x80U
#define GAIN_COEFS     16U
#define GAIN_FACTORY   0x404808U
#define GAIN_CLEARED   0x400000U

static void nafe_emul_load_coefs(struct nafe_emul_data *d, uint32_t val)
{
	for (unsigned int i = 0; i < GAIN_COEFS; i++) {
		d->reg24[REG_GAIN_COEF0 + i] = val;
	}
}

static void nafe_emul_chip_reset(struct nafe_emul_data *d)
{
	for (unsigned int a = REG_THRS_FIRST; a <= REG_THRS_LAST; a++) {
		d->reg24[a] = 0;
	}
	nafe_emul_load_coefs(d, GAIN_FACTORY);
	memset(d->ch, 0, sizeof(d->ch));
	memset(d->reg16, 0, sizeof(d->reg16));
	d->mch_en = 0;
	d->sys_config0 = 0;
	d->cfg_ptr = 0;
	d->ms_next = 0;
	d->ms_last = 0;
	d->cont_mode = -1;
	d->end_mode = -1;
	d->end_reads = 0;
	d->sync_armed = false;
	d->not_ready_reads = (d->reset_not_ready < 0) ? INT_MAX : d->reset_not_ready;
	d->ones_reads = (d->reset_ones < 0) ? INT_MAX : d->reset_ones;
}

static void nafe_emul_log_frame(struct nafe_emul_data *d, const uint8_t *tx, size_t len)
{
	if (d->n_frames < NAFE_EMUL_LOG_DEPTH) {
		struct nafe_emul_frame *f = &d->log[d->n_frames];

		f->len = MIN(len, (size_t)NAFE_EMUL_FRAME_MAX);
		memcpy(f->tx, tx, f->len);
	}
	d->n_frames++;
}

void nafe_emul_clear_log(const struct emul *e)
{
	struct nafe_emul_data *d = e->data;

	d->n_frames = 0;
	d->pulses = 0;
	d->convs = 0;
	d->conv_cmds = 0;
	d->aborts = 0;
	d->ends = 0;
	d->sync_edges = 0;
	d->status_reads = 0;
	d->ro_writes = 0;
}

unsigned int nafe_emul_frame_count(const struct emul *e)
{
	const struct nafe_emul_data *d = e->data;

	return d->n_frames;
}

bool nafe_emul_frame_is(const struct emul *e, unsigned int idx, const uint8_t *exp, size_t len)
{
	const struct nafe_emul_data *d = e->data;
	const struct nafe_emul_frame *f;

	if (idx >= MIN(d->n_frames, (unsigned int)NAFE_EMUL_LOG_DEPTH)) {
		printk("frame %u does not exist (%u frames)\n", idx, d->n_frames);
		return false;
	}

	f = &d->log[idx];
	if (f->len == len && memcmp(f->tx, exp, len) == 0) {
		return true;
	}

	printk("frame %u differs, got:", idx);
	for (size_t i = 0; i < f->len; i++) {
		printk(" %02x", f->tx[i]);
	}
	printk("  expected:");
	for (size_t i = 0; i < len; i++) {
		printk(" %02x", exp[i]);
	}
	printk("\n");

	return false;
}

void nafe_emul_pulse_drdy(const struct emul *e)
{
	const struct nafe_emul_cfg *cfg = e->cfg;
	struct nafe_emul_data *d = e->data;

	d->pulses++;
	(void)gpio_emul_input_set(cfg->drdy.port, cfg->drdy.pin, 1);
	(void)gpio_emul_input_set(cfg->drdy.port, cfg->drdy.pin, 0);
}

void nafe_emul_defaults(const struct emul *e)
{
	struct nafe_emul_data *d = e->data;

	d->reset_not_ready = 0;
	d->reset_ones = 0;
	d->end_busy_reads = 0;
	d->auto_drdy = true;
	d->fail_countdown = 0;
	d->conv_sem = NULL;
	for (unsigned int ch = 0; ch < NAFE_EMUL_CHANNELS; ch++) {
		d->code[ch] = 0x10000 * (int32_t)(ch + 1);
	}

	d->pn2 = 0x1338;
	d->pn1 = 0x8B40;
	d->pn0 = 0x0001;
	memset(d->reg24, 0, sizeof(d->reg24));
	d->reg24[REG_SERIAL1] = 0x00A5A5U;
	d->reg24[REG_SERIAL0] = 0x5A5A5AU;

	nafe_emul_chip_reset(d);
	d->errors = 0;
	d->resets = 0;
	nafe_emul_clear_log(e);
}

static int nafe_emul_next_enabled(const struct nafe_emul_data *d, unsigned int from)
{
	for (unsigned int i = 0; i < NAFE_EMUL_CHANNELS; i++) {
		unsigned int ch = (from + i) % NAFE_EMUL_CHANNELS;

		if ((d->mch_en & BIT(ch)) != 0U) {
			return ch;
		}
	}

	return -1;
}

static void nafe_emul_convert(struct nafe_emul_data *d, unsigned int ch)
{
	d->reg24[REG_CH_DATA0 + ch] = (uint32_t)d->code[ch] & 0xFFFFFFU;
	d->ms_last = ch;
	d->convs++;
}

/* A conversion round of the enabled channels, with the DRDY pulses of DRDY_PIN_EDGE */
static void nafe_emul_multi_round(const struct emul *e)
{
	struct nafe_emul_data *d = e->data;
	const bool per_channel = (d->sys_config0 & SYS_CONFIG0_DRDY_EDGE) == 0U;

	for (unsigned int ch = 0; ch < NAFE_EMUL_CHANNELS; ch++) {
		if ((d->mch_en & BIT(ch)) == 0U) {
			continue;
		}
		nafe_emul_convert(d, ch);
		if (per_channel) {
			nafe_emul_pulse_drdy(e);
		}
	}
	if (!per_channel) {
		nafe_emul_pulse_drdy(e);
	}
}

/* Start a reading mode. Returns an error code for a state the data sheet leaves open. */
static int nafe_emul_run_mode(const struct emul *e, enum nafe_emul_mode mode)
{
	struct nafe_emul_data *d = e->data;
	int ch;

	d->cont_mode = -1;

	switch (mode) {
	case MODE_SS:
		nafe_emul_convert(d, d->cfg_ptr);
		if (d->auto_drdy) {
			nafe_emul_pulse_drdy(e);
		}
		break;
	case MODE_SC:
		d->cont_mode = MODE_SC;
		d->cont_ch = d->cfg_ptr;
		break;
	case MODE_MM:
		if (d->mch_en == 0U) {
			return nafe_emul_bad(d, "CMD_MM with MCH_EN = 0");
		}
		if (d->auto_drdy) {
			nafe_emul_multi_round(e);
		} else {
			for (unsigned int i = 0; i < NAFE_EMUL_CHANNELS; i++) {
				if ((d->mch_en & BIT(i)) != 0U) {
					nafe_emul_convert(d, i);
				}
			}
		}
		break;
	case MODE_MC:
		if (d->mch_en == 0U) {
			return nafe_emul_bad(d, "CMD_MC with MCH_EN = 0");
		}
		d->cont_mode = MODE_MC;
		break;
	case MODE_MS:
		ch = nafe_emul_next_enabled(d, d->ms_next);
		if (ch < 0) {
			return nafe_emul_bad(d, "CMD_MS with MCH_EN = 0");
		}
		nafe_emul_convert(d, ch);
		d->ms_next = (ch + 1) % NAFE_EMUL_CHANNELS;
		if (d->auto_drdy && ((d->sys_config0 & SYS_CONFIG0_DRDY_EDGE) == 0U ||
				     (d->mch_en >> (ch + 1)) == 0U)) {
			nafe_emul_pulse_drdy(e);
		}
		break;
	}

	if (d->conv_sem != NULL) {
		k_sem_give(d->conv_sem);
	}

	return 0;
}

int nafe_emul_tick(const struct emul *e)
{
	struct nafe_emul_data *d = e->data;

	if (d->cont_mode == MODE_SC) {
		nafe_emul_convert(d, d->cont_ch);
		nafe_emul_pulse_drdy(e);
	} else if (d->cont_mode == MODE_MC) {
		nafe_emul_multi_round(e);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int nafe_emul_conv_cmd(const struct emul *e, enum nafe_emul_mode mode)
{
	struct nafe_emul_data *d = e->data;

	d->conv_cmds++;

	if ((d->sys_config0 & SYS_CONFIG0_ADC_SYNC) != 0U) {
		d->sync_armed = true;
		d->sync_mode = mode;
		d->cont_mode = -1;
		return 0;
	}

	d->sync_armed = false;

	return nafe_emul_run_mode(e, mode);
}

static int nafe_emul_instruction(const struct emul *e, unsigned int cmd)
{
	struct nafe_emul_data *d = e->data;
	uint16_t keep;

	switch (cmd) {
	case CMD_ABORT:
		d->aborts++;
		d->end_mode = -1;
		d->cont_mode = -1;
		d->sync_armed = false;
		break;
	case CMD_END:
		d->ends++;
		d->end_mode = d->cont_mode;
		d->end_reads = (d->end_busy_reads < 0) ? INT_MAX : d->end_busy_reads;
		d->cont_mode = -1;
		d->sync_armed = false;
		break;
	case CMD_CLEAR_ALARM:
		d->reg16[REG_GLOBAL_ALARM_INTERRUPT] = 0;
		break;
	case CMD_CLEAR_DATA:
		memset(&d->reg24[REG_CH_DATA0], 0, NAFE_EMUL_CHANNELS * sizeof(d->reg24[0]));
		break;
	case CMD_RESET:
		nafe_emul_chip_reset(d);
		break;
	case CMD_CLEAR_REG:
		keep = d->sys_config0 & SYS_CONFIG0_CK_SRC;
		nafe_emul_chip_reset(d);
		nafe_emul_load_coefs(d, GAIN_CLEARED);
		d->sys_config0 = keep;
		break;
	case CMD_RELOAD:
		nafe_emul_load_coefs(d, GAIN_FACTORY);
		d->not_ready_reads = (d->reset_not_ready < 0) ? INT_MAX : d->reset_not_ready;
		break;
	default:
		return nafe_emul_bad(d, "undefined instruction command");
	}

	return 0;
}

static uint16_t nafe_emul_read16(struct nafe_emul_data *d, unsigned int a)
{
	uint16_t v;
	int mode;

	switch (a) {
	case REG_CH_CONFIG0 ...(REG_CH_CONFIG0 + 3):
		return d->ch[d->cfg_ptr][a - REG_CH_CONFIG0];
	case REG_CH_CONFIG4:
		return d->mch_en;
	case REG_SYS_CONFIG0:
		return d->sys_config0;
	case REG_SYS_STATUS0:
		d->status_reads++;
		if (d->ones_reads > 0) {
			d->ones_reads--;
			return 0xFFFF;
		}
		v = (d->cfg_ptr << 8) | (d->ms_last << 4);
		if (d->not_ready_reads == 0) {
			v |= SYS_STATUS0_CHIP_READY;
		} else {
			d->not_ready_reads--;
		}
		mode = d->cont_mode;
		if (mode < 0 && d->end_mode >= 0 && d->end_reads > 0) {
			mode = d->end_mode;
			d->end_reads--;
		}
		if (mode == MODE_SC) {
			v |= SYS_STATUS0_SINGLE_ACTIVE;
		} else if (mode == MODE_MC) {
			v |= SYS_STATUS0_MULTI_ACTIVE;
		}
		return v;
	case REG_PN2:
		return d->pn2;
	case REG_PN1:
		return d->pn1;
	case REG_PN0:
		return d->pn0;
	default:
		return d->reg16[a];
	}
}

static int nafe_emul_write16(struct nafe_emul_data *d, unsigned int a, uint16_t v)
{
	switch (a) {
	case REG_CH_CONFIG0 ...(REG_CH_CONFIG0 + 3):
		if (a == REG_CH_CONFIG0 && (v & CH_CONFIG0_HV_SEL) == 0U) {
			unsigned int pair = 9U + ((v >> 1) & 0x7U);

			if (((v >> 12) & 0xFU) != pair || ((v >> 8) & 0xFU) != pair) {
				return nafe_emul_bad(d, "LV signal without Table 6 pair");
			}
		}
		d->ch[d->cfg_ptr][a - REG_CH_CONFIG0] = v;
		break;
	case REG_CH_CONFIG4:
		d->mch_en = v;
		break;
	case REG_SYS_CONFIG0:
		if ((v & (SYS_CONFIG0_CRC_EN | SYS_CONFIG0_STATUS_EN)) != 0U) {
			return nafe_emul_bad(d, "CRC or status byte enabled, not emulated");
		}
		d->sys_config0 = v;
		break;
	case REG_SYS_STATUS0:
	case 0x34 ... 0x36: /* DIE_TEMP, CH_STATUS0, CH_STATUS1 */
	case REG_PN2:
	case REG_PN1:
	case REG_PN0:
		d->ro_writes++;
		break;
	default:
		d->reg16[a] = v;
		break;
	}

	return 0;
}

static int nafe_emul_register(const struct emul *e, const struct spi_config *config, unsigned int a,
			      bool read, const uint8_t *tx, uint8_t *rx, size_t len)
{
	struct nafe_emul_data *d = e->data;

	ARG_UNUSED(config);

	if (reg16_valid(a)) {
		if (len != 4U) {
			return nafe_emul_bad(d, "16 bit register frame is not 4 bytes");
		}
		if (read) {
			if (rx == NULL) {
				return nafe_emul_bad(d, "register read without receive buffer");
			}
			sys_put_be16(nafe_emul_read16(d, a), &rx[2]);
			return 0;
		}
		return nafe_emul_write16(d, a, sys_get_be16(&tx[2]));
	}

	if (reg24_valid(a)) {
		if (read && len == 4U && a >= REG_CH_DATA0 &&
		    a < REG_CH_DATA0 + NAFE_EMUL_CHANNELS) {
			if (rx == NULL) {
				return nafe_emul_bad(d, "register read without receive buffer");
			}
			sys_put_be16((d->reg24[a] >> 8) & 0xFFFFU, &rx[2]);
			return 0;
		}
		if (len != 5U) {
			return nafe_emul_bad(d, "24 bit register frame is not 5 bytes");
		}
		if (read) {
			if (rx == NULL) {
				return nafe_emul_bad(d, "register read without receive buffer");
			}
			sys_put_be24(d->reg24[a], &rx[2]);
			return 0;
		}
		if ((a >= REG_CH_DATA0 && a < REG_CH_DATA0 + NAFE_EMUL_CHANNELS) ||
		    a == REG_SERIAL1 || a == REG_SERIAL0) {
			d->ro_writes++;
		} else {
			d->reg24[a] = sys_get_be24(&tx[2]);
		}
		return 0;
	}

	return nafe_emul_bad(d, "access to an unknown register address");
}

static int nafe_emul_burst(struct nafe_emul_data *d, uint8_t *rx, size_t len)
{
	const bool wire_16 = (d->sys_config0 & SYS_CONFIG0_16BIT) != 0U;
	const size_t w = wire_16 ? 2U : 3U;
	size_t pos = 2;

	if (d->mch_en == 0U) {
		return nafe_emul_bad(d, "CMD_BURST_DATA with MCH_EN = 0");
	}
	if (len != 2U + POPCOUNT(d->mch_en) * w) {
		return nafe_emul_bad(d, "CMD_BURST_DATA frame length does not match MCH_EN");
	}
	if (rx == NULL) {
		return nafe_emul_bad(d, "CMD_BURST_DATA without receive buffer");
	}

	for (unsigned int ch = 0; ch < NAFE_EMUL_CHANNELS; ch++) {
		if ((d->mch_en & BIT(ch)) == 0U) {
			continue;
		}
		if (wire_16) {
			sys_put_be16((d->reg24[REG_CH_DATA0 + ch] >> 8) & 0xFFFFU, &rx[pos]);
		} else {
			sys_put_be24(d->reg24[REG_CH_DATA0 + ch], &rx[pos]);
		}
		pos += w;
	}

	return 0;
}

static int nafe_emul_io(const struct emul *target, const struct spi_config *config,
			const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	const struct nafe_emul_cfg *cfg = target->cfg;
	struct nafe_emul_data *d = target->data;
	const uint8_t *tx;
	uint8_t *rx = NULL;
	size_t len;
	uint16_t word;
	unsigned int a;
	bool read;

	if (tx_bufs == NULL || tx_bufs->count != 1U) {
		return nafe_emul_bad(d, "a transfer is not exactly one transmit buffer");
	}
	tx = tx_bufs->buffers[0].buf;
	len = tx_bufs->buffers[0].len;
	if (rx_bufs != NULL) {
		if (rx_bufs->count != 1U || rx_bufs->buffers[0].len != len) {
			return nafe_emul_bad(d, "receive buffer differs from transmit buffer");
		}
		rx = rx_bufs->buffers[0].buf;
		memset(rx, 0, len);
	}

	if (tx == NULL || len < 2U) {
		return nafe_emul_bad(d, "frame shorter than a command word");
	}

	nafe_emul_log_frame(d, tx, len);

	if (d->fail_countdown > 0 && --d->fail_countdown == 0) {
		return -EIO;
	}

	/* One transfer is one chip select frame: SPI mode 1, MSB first, 8 bit, no CS hold */
	if (SPI_OP_MODE_GET(config->operation) != SPI_OP_MODE_CONTROLLER ||
	    (config->operation & (SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_LSB | SPI_MODE_LOOP |
				  SPI_HOLD_ON_CS | SPI_LOCK_ON)) != SPI_MODE_CPHA ||
	    SPI_WORD_SIZE_GET(config->operation) != 8U) {
		return nafe_emul_bad(d, "SPI config is not mode 1, MSB first, 8 bit");
	}

	if (d->in_reset) {
		return nafe_emul_bad(d, "frame while RSTB is low");
	}

	word = sys_get_be16(tx);
	if (((word >> 15) & 1U) != cfg->dev_addr) {
		return nafe_emul_bad(d, "device address bit does not match SPI_ADDR");
	}
	if ((word & 1U) != 0U) {
		return nafe_emul_bad(d, "bit 0 of the command word is set");
	}

	read = (word & BIT(14)) != 0U;
	a = (word >> 1) & 0x1FFFU;

	if ((d->not_ready_reads != 0 || d->ones_reads != 0) && !(read && a == REG_SYS_STATUS0) &&
	    !(!read && a == CMD_RESET)) {
		return nafe_emul_bad(d, "frame before CHIP_READY");
	}

	if (!read && a < 0x20U) {
		/* CMD_CHn (0x00..0x0F) and the instruction commands (0x10..0x16) */
		if (len != 2U) {
			return nafe_emul_bad(d, "instruction command frame is not 2 bytes");
		}
		if (a < 0x10U) {
			d->cfg_ptr = a;
			d->ms_next = a;
			return 0;
		}
		return nafe_emul_instruction(target, a);
	}

	if (read && a <= CMD_BURST_DATA) {
		if (a == CMD_BURST_DATA) {
			return nafe_emul_burst(d, rx, len);
		}
		if (len != 2U) {
			return nafe_emul_bad(d, "conversion command frame is not 2 bytes");
		}
		return nafe_emul_conv_cmd(target, (enum nafe_emul_mode)a);
	}

	if (a < 0x20U) {
		return nafe_emul_bad(d, "undefined command");
	}

	return nafe_emul_register(target, config, a, read, tx, rx, len);
}

static void nafe_emul_rstb_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct nafe_emul_data *d = CONTAINER_OF(cb, struct nafe_emul_data, rstb_cb);
	const struct nafe_emul_cfg *cfg = d->self->cfg;
	const bool high = gpio_emul_output_get(cfg->reset.port, cfg->reset.pin) != 0;

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	if (!high && !d->in_reset) {
		d->in_reset = true;
		d->rstb_fall = k_cycle_get_32();
	} else if (high && d->in_reset) {
		d->in_reset = false;
		d->rstb_low_us = k_cyc_to_us_floor32(k_cycle_get_32() - d->rstb_fall);
		d->resets++;
		nafe_emul_chip_reset(d);
	}
}

static void nafe_emul_sync_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct nafe_emul_data *d = CONTAINER_OF(cb, struct nafe_emul_data, sync_cb);
	const struct nafe_emul_cfg *cfg = d->self->cfg;
	const bool high = gpio_emul_output_get(cfg->sync.port, cfg->sync.pin) != 0;

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	if (high && !d->sync_level) {
		d->sync_edges++;
		if (d->sync_armed) {
			(void)nafe_emul_run_mode(d->self, d->sync_mode);
		}
	}
	d->sync_level = high;
}

static int nafe_emul_init(const struct emul *target, const struct device *parent)
{
	const struct nafe_emul_cfg *cfg = target->cfg;
	struct nafe_emul_data *d = target->data;

	ARG_UNUSED(parent);

	d->self = target;
	nafe_emul_defaults(target);

	gpio_init_callback(&d->rstb_cb, nafe_emul_rstb_cb, BIT(cfg->reset.pin));
	(void)gpio_add_callback(cfg->reset.port, &d->rstb_cb);

	if (cfg->sync.port != NULL) {
		gpio_init_callback(&d->sync_cb, nafe_emul_sync_cb, BIT(cfg->sync.pin));
		(void)gpio_add_callback(cfg->sync.port, &d->sync_cb);
	}

	return 0;
}

static const struct spi_emul_api nafe_emul_api = {
	.io = nafe_emul_io,
};

#define NAFE_EMUL_INIT(n)                                                                          \
	static struct nafe_emul_data nafe_emul_data_##n;                                           \
	static const struct nafe_emul_cfg nafe_emul_cfg_##n = {                                    \
		.drdy = GPIO_DT_SPEC_INST_GET(n, drdy_gpios),                                      \
		.reset = GPIO_DT_SPEC_INST_GET(n, reset_gpios),                                    \
		.sync = GPIO_DT_SPEC_INST_GET_OR(n, sync_gpios, {0}),                              \
		.dev_addr = DT_INST_PROP(n, nxp_dev_addr),                                         \
	};                                                                                         \
	EMUL_DT_INST_DEFINE(n, nafe_emul_init, &nafe_emul_data_##n, &nafe_emul_cfg_##n,            \
			    &nafe_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(NAFE_EMUL_INIT)
