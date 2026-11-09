/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extension API of the NXP NAFE13388 analog front-end
 * @ingroup nafe13388_interface
 *
 * The standard ADC API cannot express everything the NAFE13388 can do. This
 * API gives access to the complete channel configuration, to the reading
 * modes, to the raw registers and to the device commands. All functions must
 * be called from thread context, they use SPI and may sleep.
 *
 * The device is used by one client at a time. A read of the standard ADC API
 * and a reading started with this API exclude each other: whoever comes
 * second gets -EBUSY. The call does not wait for the running read, because
 * the driver does not hold its lock while it waits for DRDY. A call can still
 * wait for another call that is inside the driver at that moment, for example
 * NAFE13388_RESET with its wait for CHIP_READY, or a stop with
 * NAFE13388_STOP_END with its wait for the end of the conversion.
 * Two adc_read() calls queue on the lock of the ADC API instead of failing.
 * A channel setup (adc_channel_setup(),
 * nafe13388_channel_setup_ext(), nafe13388_cfg_rate()) is also refused with
 * -EBUSY while any reading owns the device: stop the reading, set up the
 * channel, start the reading again.
 *
 * The result of a conversion is the signed 24 bit code of the device (a sign
 * extended int32_t, or an int16_t for a resolution of 16). The standard helper
 * adc_raw_to_millivolts() is only right for the high voltage differential
 * channels, which is the only kind of high voltage channel that the driver
 * accepts. It does not fit the low voltage channels (nxp,input-select = "lv"):
 * convert their codes with Table 20 of the data sheet.
 *
 * The helper always scales with the 5000 mV span of Table 20, which is the span
 * with the 2.5 V reference. It does not look at nxp,reference-select. With
 * REF_SEL 1 to 3 the data sheet also gives 2.5 V for the external reference, but
 * an external reference with another voltage gives wrong millivolt values without
 * an error (ASSUMPTION: the result scales with the reference; the data sheet text
 * does not say so). The factory coefficients are for the internal reference only,
 * and the data sheet asks for TCC_OFF = 1 with an external reference.
 *
 * Error codes for a call that needs a started reading, when there is none:
 * nafe13388_stop() and nafe13388_continuous_stop() return -EALREADY,
 * nafe13388_trigger() and nafe13388_sync_pulse() return -EINVAL, and
 * nafe13388_drdy_arm(), nafe13388_drdy_wait(), nafe13388_read_burst() and
 * nafe13388_read_data() return -EBUSY. The codes differ between the functions,
 * they are fixed by the tests of the driver and kept as they are.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_NAFE13388_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_NAFE13388_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NXP NAFE13388 analog front-end extension API
 * @defgroup nafe13388_interface NAFE13388
 * @ingroup adc_interface_ext
 * @{
 */

/**
 * @brief Device commands for nafe13388_command().
 */
enum nafe13388_command_id {
	/** Clear the global alarm interrupt register (CMD_CLEAR_ALARM) */
	NAFE13388_CLEAR_ALARM,
	/** Set all channel data registers to zero (CMD_CLEAR_DATA) */
	NAFE13388_CLEAR_DATA,
	/** Set the user registers to their defaults (CMD_CLEAR_REG), then reload the
	 *  factory coefficients (CMD_RELOAD)
	 */
	NAFE13388_CLEAR_REG,
	/** Reload the trim and calibration coefficients from the NVM (CMD_RELOAD) */
	NAFE13388_RELOAD,
	/** Reset the device, same as the RSTB pin (CMD_RESET) */
	NAFE13388_RESET,
};

/**
 * @brief Reading modes.
 */
enum nafe13388_mode {
	/** Single channel, single reading (CMD_SS) */
	NAFE13388_MODE_SS,
	/** Single channel, continuous reading (CMD_SC) */
	NAFE13388_MODE_SC,
	/** Multichannel, multi-reading: all enabled channels once (CMD_MM) */
	NAFE13388_MODE_MM,
	/** Multichannel, continuous reading (CMD_MC) */
	NAFE13388_MODE_MC,
	/** Multichannel, single reading: one channel per trigger (CMD_MS) */
	NAFE13388_MODE_MS,
};

/**
 * @brief Ways to end a reading.
 */
enum nafe13388_stop {
	/** Stop at once, the data of the running conversion is lost (CMD_ABORT) */
	NAFE13388_STOP_ABORT,
	/** Finish the running conversion first (CMD_END) */
	NAFE13388_STOP_END,
	/** Send nothing, only give the device back after a reading that ended itself */
	NAFE13388_STOP_IDLE,
};

/**
 * @brief Description of a reading.
 */
struct nafe13388_seq {
	/** Channels to convert, bit n is logical channel n. All must be set up. SS and SC
	 *  need exactly one channel.
	 */
	uint16_t channels;
	/** Reading mode */
	enum nafe13388_mode mode;
	/** Width of the results, 16 or 24 bit */
	uint8_t resolution;
	/** Start the conversions with a pulse on the SYNC pin instead of the SPI command
	 *  (SYS_CONFIG0.ADC_SYNC). The command then only selects the mode. Needs
	 *  sync-gpios in the devicetree.
	 */
	bool sync_pin;
	/** One DRDY pulse per converted channel (DRDY_PIN_EDGE = 0). The default is one
	 *  pulse per sequence, after the last enabled channel. Ignored in the mode
	 *  NAFE13388_MODE_MS, which always gives a pulse per conversion.
	 */
	bool drdy_per_channel;
};

/**
 * @brief Callback of a continuous reading.
 *
 * It is called from the acquisition thread of the driver, never from an
 * interrupt, with the device unlocked. It may call nafe13388_continuous_stop()
 * (nafe13388_cfg_rate() and adc_channel_setup() return -EBUSY there). It should be
 * quicker than a sequence: the DRDY events are not queued, only one pending event is
 * remembered.
 *
 * @param dev NAFE13388 device
 * @param raw One sign extended value per enabled channel in channel index order,
 *            valid only during the call. NULL when @p status is a fatal error.
 * @param n Number of values in @p raw
 * @param status 0 or -EOVERFLOW (data valid, but the next DRDY was already pending when
 *               the driver went back to wait for it, so the processing is running late
 *               and sequences may have been lost; only one pending DRDY is counted).
 *               -ETIMEDOUT (no DRDY, the reading was aborted) or an SPI error are fatal:
 *               the driver aborts the reading and this is the last call.
 * @param user_data Pointer given to nafe13388_continuous_start()
 *
 * @return 0 to go on, anything else to abort the reading
 */
typedef int (*nafe13388_seq_cb)(const struct device *dev, const int32_t *raw, unsigned int n,
				int status, void *user_data);

/**
 * @brief Complete configuration of one logical channel (CH_CONFIG0..3).
 *
 * The fields are the register fields of the data sheet, see the NAFE13388_*
 * constants of dt-bindings/adc/nafe13388.h for the encodings.
 */
struct nafe13388_channel_cfg {
	/** CH_CONFIG0.HV_AIP, positive input, used when @p lv is false (0..8). With @p lv
	 *  the driver writes the pair that the data sheet (Table 6) requires for the
	 *  signal (HV_AIP = HV_AIN = 9 + lv_signal) and ignores both fields.
	 */
	uint8_t hv_aip;
	/** CH_CONFIG0.HV_AIN, negative input, used when @p lv is false (0..8) */
	uint8_t hv_ain;
	/** CH_CONFIG0.CH_GAIN: 0 = 0.2x, 1 = 0.4x, 2 = 0.8x, 3 = 1x, 4 = 2x, 5 = 4x,
	 *  6 = 8x, 7 = 16x
	 */
	uint8_t gain_code;
	/** Select a low voltage signal instead of the HV inputs (HV_SEL = 0) */
	bool lv;
	/** CH_CONFIG0.LVSIG_IN, used when @p lv is true (0..5) */
	uint8_t lv_signal;
	/** CH_CONFIG0.TCC_OFF, disable the temperature coefficient correction */
	bool tcc_off;

	/** CH_CONFIG1.CH_CAL_GAIN_OFFSET, coefficient pair (0..15) */
	uint8_t cal_pair;
	/** CH_CONFIG1.CH_THRS, threshold pair (0..15) */
	uint8_t thrs_pair;
	/** CH_CONFIG1.ADC_DATA_RATE code (0..28) */
	uint8_t rate;
	/** CH_CONFIG1.ADC_SINC (0..4) */
	uint8_t sinc;

	/** CH_CONFIG2.CH_DELAY code (0..63) */
	uint8_t delay;
	/** CH_CONFIG2.ADC_NORMAL_SETTLING */
	bool normal_settling;
	/** CH_CONFIG2.ADC_FILTER_RESET */
	bool filter_reset;
	/** CH_CONFIG2.CH_CHOP, precision mode */
	bool chop;

	/** CH_CONFIG3.VIEX_VI, the excitation is a current (else a voltage) */
	bool viex_current;
	/** CH_CONFIG3.VIEX_POL, negative polarity */
	bool viex_negative;
	/** CH_CONFIG3.VIEX_MAG, magnitude code (0..15) */
	uint8_t viex_mag;
	/** CH_CONFIG3.VEXC_EN */
	bool vexc_en;
	/** CH_CONFIG3.OPEN_DET_CURRENT */
	bool odc;
	/** CH_CONFIG3.VIEX_CHOP, must be equal on all channels of a sequence */
	bool viex_chop;
	/** CH_CONFIG3.VIEX_AIP_EN, 0 = none, 1..4 = AI1P..AI4P */
	uint8_t viex_aip;
	/** CH_CONFIG3.VIEX_AIN_EN, 0 = none, 1..4 = AI1N..AI4N */
	uint8_t viex_ain;
};

/**
 * @brief Set up a logical channel with the complete configuration.
 *
 * Replaces the configuration given by the devicetree or by adc_channel_setup().
 * Refused with -EBUSY while a reading of any kind owns the device, and so is
 * adc_channel_setup(). The data sheet (7.4.2.2) says that a change of the channel in use
 * only takes effect with the next conversion command, so a register
 * write to a channel setting while a reading runs can be lost (see
 * nafe13388_cfg_rate()); stop the reading first.
 *
 * @param dev NAFE13388 device
 * @param channel Logical channel, 0..15
 * @param cfg Channel configuration
 *
 * @retval 0 on success
 * @retval -EINVAL if the channel or a field of @p cfg is out of range
 * @retval -EBUSY if a reading owns the device
 * @retval <0 SPI error
 */
int nafe13388_channel_setup_ext(const struct device *dev, uint8_t channel,
				const struct nafe13388_channel_cfg *cfg);

/**
 * @brief Change the data rate and the delay of a configured channel.
 *
 * Writes only CH_CONFIG1 and CH_CONFIG2, and only when a value changes. It returns
 * -EBUSY while a reading of any kind owns the device, also from the callback of a
 * continuous reading, and sends nothing then. Reason: a CH_CONFIG1 write from the
 * callback of a running MC reading can be accepted on the SPI without an error while
 * the chip keeps the old rate. The data sheet (7.4.2.2) only says that a change of the
 * converting channel is not taken "until next ADC conversion command". Stop the
 * reading, change the rate and start the reading again.
 *
 * @param dev NAFE13388 device
 * @param channel Logical channel, 0..15, already set up
 * @param rate ADC_DATA_RATE code, 0..28
 * @param delay CH_DELAY code, 0..63
 *
 * @retval 0 on success
 * @retval -EINVAL if an argument is out of range or the channel is not set up
 * @retval -EBUSY if a reading owns the device
 * @retval <0 SPI error
 */
int nafe13388_cfg_rate(const struct device *dev, uint8_t channel, uint8_t rate, uint8_t delay);

/**
 * @brief Start a reading and take the device until nafe13388_stop().
 *
 * Writes SYS_CONFIG0 and the channel enable register, selects the first enabled
 * channel, forgets an old DRDY and sends the command of the mode. With
 * @p seq.sync_pin the command only selects the mode and the first conversion
 * is started by nafe13388_sync_pulse().
 *
 * The device is free again after nafe13388_stop(), also after SS, MM and MS
 * which end by themselves (use NAFE13388_STOP_IDLE then).
 *
 * @param dev NAFE13388 device
 * @param seq Reading to start
 *
 * @retval 0 on success
 * @retval -EINVAL for an invalid sequence or a channel that is not set up
 * @retval -ENOTSUP if sync_pin is set and there is no sync-gpios
 * @retval -EBUSY if the device is used
 * @retval <0 SPI error
 */
int nafe13388_start(const struct device *dev, const struct nafe13388_seq *seq);

/**
 * @brief End a reading started with nafe13388_start().
 *
 * NAFE13388_STOP_END lets the device finish the running conversion, so this
 * function waits until SYS_STATUS0 shows no conversion mode any more, at most
 * the conversion time of the enabled channels plus CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS.
 * If the device does not get idle it is
 * aborted and the function returns -ETIMEDOUT. NAFE13388_STOP_ABORT takes effect
 * at once and does not wait. The device is given back in every case.
 *
 * @param dev NAFE13388 device
 * @param how How to end it
 *
 * @retval 0 on success
 * @retval -ETIMEDOUT if a stop with NAFE13388_STOP_END did not finish in time
 * @retval -EALREADY if no reading was started
 * @retval -EBUSY if the device is used by something else, or by a continuous reading
 *                (use nafe13388_continuous_stop())
 * @retval -EINVAL for an unknown @p how
 * @retval <0 SPI error
 */
int nafe13388_stop(const struct device *dev, enum nafe13388_stop how);

/**
 * @brief Trigger the next conversion of a reading started with nafe13388_start().
 *
 * Sends the command of the mode again, or pulses the SYNC pin if the reading
 * was started with sync_pin. The DRDY event is armed first. This is how the
 * next channel of the MS mode is converted, and how an SS or MM reading is
 * repeated. The device restarts from the first enabled channel (MS: from the
 * same channel) if a conversion is running.
 *
 * @retval 0 on success
 * @retval -EINVAL if no reading was started
 * @retval -EBUSY if the device is used by something else
 * @retval <0 SPI or GPIO error
 */
int nafe13388_trigger(const struct device *dev);

/**
 * @brief Pulse the SYNC pin.
 *
 * Only valid while a reading started with sync_pin is active. The pulse is
 * at least 2 system clocks (434 ns) long.
 *
 * @retval 0 on success
 * @retval -ENOTSUP if there is no sync-gpios
 * @retval -EINVAL if no reading with sync_pin was started
 * @retval <0 GPIO error
 */
int nafe13388_sync_pulse(const struct device *dev);

/**
 * @brief Forget a pending DRDY event, before the command or pulse that starts a conversion.
 *
 * nafe13388_start() and nafe13388_trigger() do this themselves. Only needed
 * before nafe13388_sync_pulse() if the DRDY of an earlier conversion may still
 * be pending.
 *
 * @retval 0 on success
 * @retval -EBUSY if no reading was started with nafe13388_start()
 */
int nafe13388_drdy_arm(const struct device *dev);

/**
 * @brief Wait for the DRDY event of a reading started with nafe13388_start().
 *
 * It does not abort the conversion on a timeout, call nafe13388_stop() for that.
 *
 * @param dev NAFE13388 device
 * @param timeout How long to wait
 *
 * @retval 0 on DRDY
 * @retval -ETIMEDOUT if there was no DRDY in time
 * @retval -EBUSY if no reading was started with nafe13388_start()
 */
int nafe13388_drdy_wait(const struct device *dev, k_timeout_t timeout);

/**
 * @brief Read the results of all enabled channels with one burst frame.
 *
 * Call it after DRDY. The values are sign extended to 32 bit, also for a
 * resolution of 16.
 *
 * @param dev NAFE13388 device
 * @param raw Output, one value per enabled channel in channel index order
 * @param n Number of enabled channels of the reading, must match exactly
 *
 * @retval 0 on success
 * @retval -EINVAL for a NULL pointer or a wrong @p n
 * @retval -EBUSY if no reading was started with nafe13388_start()
 * @retval <0 SPI error
 */
int nafe13388_read_burst(const struct device *dev, int32_t *raw, unsigned int n);

/**
 * @brief Read the result of one channel from its data register.
 *
 * With a resolution of 16 the driver reads the register with a 4 byte frame (16 data
 * bits) and sign extends the value to 32 bit, the same as the burst read does. The data
 * sheet does not say how the register reads in this mode; the driver assumes that the
 * 4 byte frame returns the upper 16 bit of the 24 bit value in both output modes, and
 * that a 5 byte frame returns the full 24 bit value also in the 16 bit mode.
 *
 * @retval 0 on success
 * @retval -EINVAL for a NULL pointer or a channel above 15
 * @retval -EBUSY if no reading was started with nafe13388_start()
 * @retval <0 SPI error
 */
int nafe13388_read_data(const struct device *dev, uint8_t channel, int32_t *raw);

/**
 * @brief Start a continuous reading (SC or MC) that reports every sequence to a callback.
 *
 * The driver waits for DRDY, reads the results with one burst frame and calls
 * @p cb, until nafe13388_continuous_stop(), a fatal error, or a callback that
 * returns non-zero. The callback is called from the acquisition thread of the
 * driver, see ::nafe13388_seq_cb. With @p seq.sync_pin the first SYNC pulse has
 * to come within the time that the channels need plus
 * CONFIG_ADC_NAFE13388_DRDY_TIMEOUT_MS, else the reading is
 * aborted with -ETIMEDOUT. While it runs the device is taken: the
 * standard ADC API (reads and adc_channel_setup()), the other reading functions,
 * nafe13388_cfg_rate() and nafe13388_channel_setup_ext() return -EBUSY. These work: the
 * raw register access and NAFE13388_CLEAR_ALARM.
 *
 * @param dev NAFE13388 device
 * @param seq Reading, the mode must be NAFE13388_MODE_SC or NAFE13388_MODE_MC
 * @param cb Callback
 * @param user_data Passed to the callback
 *
 * @retval 0 on success
 * @retval -EINVAL for an invalid sequence, mode or a NULL callback, or for
 *                 drdy_per_channel with more than one channel (the driver reads
 *                 one burst per DRDY, so it needs one DRDY per sequence)
 * @retval -ENOTSUP if sync_pin is set and there is no sync-gpios
 * @retval -EBUSY if the device is used
 * @retval <0 SPI error
 */
int nafe13388_continuous_start(const struct device *dev, const struct nafe13388_seq *seq,
			       nafe13388_seq_cb cb, void *user_data);

/**
 * @brief Stop a continuous reading.
 *
 * Sends the stop command and returns when the callback can no longer be
 * running. Called from the callback itself it does not wait for the callback.
 * NAFE13388_STOP_END also waits until the device has finished the running
 * conversion (see nafe13388_stop()) and returns -ETIMEDOUT, after an abort, if
 * it does not.
 *
 * Do not use the CH_DATA registers after a stop with NAFE13388_STOP_END as the
 * last result of the reading: no DRDY follows, and the registers can hold a part of a
 * new sequence. ASSUMPTION: the first channel of the next sequence is converted
 * while the device finishes the running conversion.
 *
 * @param dev NAFE13388 device
 * @param how NAFE13388_STOP_ABORT or NAFE13388_STOP_END
 *
 * @retval 0 on success
 * @retval -EALREADY if no continuous reading is running
 * @retval -EBUSY if the device is used by something else
 * @retval -EINVAL for another @p how
 * @retval <0 SPI error (the reading is stopped anyway)
 */
int nafe13388_continuous_stop(const struct device *dev, enum nafe13388_stop how);

/**
 * @brief Read a 16-bit register.
 *
 * Valid registers are 0x20..0x37 except 0x28 (no register there), and 0x7C..0x7F.
 * Reading 0x20..0x23 returns the configuration of the channel that was selected last.
 *
 * @retval 0 on success
 * @retval -EINVAL for an invalid register or a NULL pointer
 * @retval <0 SPI error
 */
int nafe13388_read_reg16(const struct device *dev, uint8_t reg, uint16_t *val);

/**
 * @brief Write a 16-bit register.
 *
 * The registers the driver keeps a copy of (CH_CONFIG0..4 and SYS_CONFIG0) are
 * refused, use the channel and reading functions for them.
 *
 * A write that returns 0 does not mean that the chip kept every bit. For example
 * bit 1 (VIEX_OI_ALRM) of GLOBAL_ALARM_ENABLE (0x32) has been seen to always read
 * back as 0, also with raw SPI frames, although the data sheet lists it as
 * read/write. Bit 11 of that register is reserved in the data sheet and also reads
 * back as 0. Read the register back if the code depends on a bit.
 *
 * @retval 0 on success
 * @retval -EINVAL for an invalid or refused register
 * @retval <0 SPI error
 */
int nafe13388_write_reg16(const struct device *dev, uint8_t reg, uint16_t val);

/**
 * @brief Read a 24-bit register (0x40..0x6F and 0x80..0xAF).
 *
 * The value is returned unsigned and not sign extended.
 *
 * @retval 0 on success
 * @retval -EINVAL for an invalid register or a NULL pointer
 * @retval <0 SPI error
 */
int nafe13388_read_reg24(const struct device *dev, uint8_t reg, uint32_t *val);

/**
 * @brief Write a 24-bit register (0x40..0x6F and 0x80..0xAF), only the lowest
 *        24 bits of @p val are used.
 *
 * @retval 0 on success
 * @retval -EINVAL for an invalid register
 * @retval <0 SPI error
 */
int nafe13388_write_reg24(const struct device *dev, uint8_t reg, uint32_t val);

/**
 * @brief Send a device command.
 *
 * NAFE13388_CLEAR_ALARM is always accepted, also during a read of the standard API,
 * and does not wait for the conversion. The other commands are refused with
 * -EBUSY while a reading is running (standard API or this API). NAFE13388_RESET
 * and NAFE13388_CLEAR_REG put the device back to its register defaults, the
 * driver then waits for CHIP_READY and brings the device back to the state
 * after the initialisation: SYS_CONFIG0 from the devicetree and only the
 * channels of the devicetree set up. Channels that were set up at runtime
 * have to be set up again, the data sheet requires every channel to be
 * reconfigured after a reset or a register clear. Gain and offset
 * coefficients, thresholds and GPIO registers are not restored by the driver.
 * NAFE13388_CLEAR_REG also sets the gain coefficients to 0x400000 and clears the
 * thresholds, so the driver follows it with a
 * RELOAD, which brings the factory gain and offset coefficients back from the NVM.
 * NAFE13388_RELOAD only reloads the NVM contents and keeps the user registers.
 *
 * @retval 0 on success
 * @retval -EINVAL for an unknown command
 * @retval -EBUSY if a reading is running
 * @retval -ETIMEDOUT if CHIP_READY was not seen in time
 * @retval <0 SPI error
 */
int nafe13388_command(const struct device *dev, enum nafe13388_command_id id);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_NAFE13388_H_ */
