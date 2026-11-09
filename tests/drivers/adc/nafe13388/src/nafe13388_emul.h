/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Emulator of the NXP NAFE13388 analog front-end on a SPI emulation bus.
 *
 * The emulator is written from the data sheet (Rev. 5.0), not from the driver,
 * so that a passing test shows that the driver matches this reading of the
 * data sheet. It does NOT show that the chip behaves like this. Assumptions
 * that the data sheet does not settle are listed in nafe13388_emul.c.
 *
 * The emulator checks every SPI transfer (one transfer = one chip select
 * frame): the SPI mode, the device address bit, the frame length for the
 * command, the chip state. A violation increments "errors" and the transfer
 * fails with -EIO. The tests check that "errors" stays 0.
 */

#ifndef ZEPHYR_TESTS_DRIVERS_ADC_NAFE13388_EMUL_H_
#define ZEPHYR_TESTS_DRIVERS_ADC_NAFE13388_EMUL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>

struct k_sem;

#define NAFE_EMUL_CHANNELS  16
/* Longest frame: command word and 3 bytes for each of the 16 channels */
#define NAFE_EMUL_FRAME_MAX (2 + 3 * NAFE_EMUL_CHANNELS)
#define NAFE_EMUL_LOG_DEPTH 256

struct nafe_emul_frame {
	uint8_t len;
	uint8_t tx[NAFE_EMUL_FRAME_MAX];
};

struct nafe_emul_data {
	/* Register file */
	uint16_t ch[NAFE_EMUL_CHANNELS][4]; /* CH_CONFIG0..3 of each logical channel */
	uint16_t mch_en;                    /* CH_CONFIG4 */
	uint16_t sys_config0;
	uint16_t reg16[0x80]; /* all other 16 bit registers, index = address */
	uint32_t reg24[0xB0]; /* 24 bit registers (CH_DATAn, coefficients), index = address */
	uint16_t pn2;
	uint16_t pn1;
	uint16_t pn0;

	/* Settings of the test, restored by nafe_emul_defaults() */
	int reset_not_ready; /* SYS_STATUS0 reads with CHIP_READY = 0 after a reset, -1: never */
	int reset_ones;      /* SYS_STATUS0 reads of 0xffff before those, -1: never ends */
	bool auto_drdy;      /* pulse DRDY for CMD_SS, CMD_MM and CMD_MS */
	int32_t code[NAFE_EMUL_CHANNELS]; /* 24 bit result of each channel (sign extended) */
	int end_busy_reads; /* reads of SYS_STATUS0 that show the mode after CMD_END, -1: all */
	int fail_countdown; /* > 0: the transfer with this ordinal (1 = the next) fails with -EIO */
	struct k_sem *conv_sem; /* given after every conversion command, NULL: not used */

	/* Chip state */
	int not_ready_reads;
	int ones_reads;
	bool in_reset;   /* RSTB is low */
	uint8_t cfg_ptr; /* channel selected by CMD_CHn */
	uint8_t ms_next; /* channel that CMD_MS converts next */
	int cont_mode;   /* running continuous mode (0..4 as CMD code), -1 if none */
	int end_mode;    /* mode that CMD_END is finishing, -1 if none */
	int end_reads;   /* SYS_STATUS0 reads left that show it */
	uint8_t cont_ch; /* channel of a running CMD_SC */
	bool sync_armed;
	bool sync_level;
	unsigned int sync_mode;

	/* Observations of the test */
	uint32_t errors;      /* protocol errors, must stay 0 */
	uint32_t resets;      /* rising edges of RSTB */
	uint32_t rstb_low_us; /* length of the last RSTB low pulse */
	uint32_t pulses;      /* DRDY pulses */
	uint32_t convs;       /* converted channels */
	uint32_t conv_cmds;   /* conversion commands received */
	uint32_t aborts;
	uint32_t ends;
	uint32_t sync_edges;
	uint32_t status_reads;
	uint32_t ro_writes; /* writes to registers that are read only */
	unsigned int ms_last;

	/* Frame log */
	unsigned int n_frames;
	struct nafe_emul_frame log[NAFE_EMUL_LOG_DEPTH];

	/* Internal */
	const struct emul *self;
	struct gpio_callback rstb_cb;
	struct gpio_callback sync_cb;
	uint32_t rstb_fall;
};

/* Handle of the emulator of the device with this node label */
#define NAFE_EMUL(label) EMUL_DT_GET(DT_NODELABEL(label))

/* Set the settings of the test to their defaults and clear the log and the counters */
void nafe_emul_defaults(const struct emul *e);

/* Clear the frame log and the counters (not "errors" and "resets") */
void nafe_emul_clear_log(const struct emul *e);

unsigned int nafe_emul_frame_count(const struct emul *e);

/* Compare the transmitted bytes of a logged frame, print both on a mismatch */
bool nafe_emul_frame_is(const struct emul *e, unsigned int idx, const uint8_t *exp, size_t len);

/* One pulse on the DRDY pin */
void nafe_emul_pulse_drdy(const struct emul *e);

/*
 * One conversion round of the running continuous mode (CMD_SC or CMD_MC): update
 * CH_DATAn from "code" and pulse DRDY (once, or once per channel if DRDY_PIN_EDGE
 * is 0). Returns -EINVAL if no continuous mode is running.
 */
int nafe_emul_tick(const struct emul *e);

#endif /* ZEPHYR_TESTS_DRIVERS_ADC_NAFE13388_EMUL_H_ */
