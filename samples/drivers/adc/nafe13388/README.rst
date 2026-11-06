.. zephyr:code-sample:: adc_nafe13388
   :name: NAFE13388 analog front end
   :relevant-api: adc_interface nafe13388_interface

   Read the analog inputs of an NXP NAFE13388 with the ADC API and with the extension API
   of the driver.

Overview
********

This sample shows how to use the NXP NAFE13388 universal input analog front end with
Zephyr. It reads two inputs in three ways:

#. One channel, with the standard ADC API (:c:func:`adc_read_dt`). The raw value is converted
   to millivolts with :c:func:`adc_raw_to_millivolts_dt`.
#. Both channels in one sequence, with one call of :c:func:`adc_read`.
#. A continuous reading that calls a callback for every sequence. This uses the extension API
   of the driver, :c:func:`nafe13388_continuous_start` and :c:func:`nafe13388_continuous_stop`.
   The callback only stores the values and counts the sequences. The values are printed
   after the reading has stopped.

The three readings run once after boot. Then the shell commands below let you repeat them.

The driver accepts only differential high voltage inputs. In the devicetree overlay both
channels read the input AI1P or AI2P against the analog common input AICOM (see below).
With the gain of 1 and the internal reference of 5000 mV the full scale is
about +/- 5 V. The overlay sets the ADC data rate code 19 in the ``zephyr,acquisition-time``
property and selects the second stage filter SINC1 with ``nxp,sinc``. Without the filter (the
default) the data rate code does not slow the conversions: on the board a channel took about
220 us, which is faster than the SPI transfer of a sequence at 500 kHz, so the continuous reading
reported sequences as late. With code 19 and SINC1 one sequence of the two channels takes about
20 ms.

Requirements
************

* A :zephyr:board:`mimxrt1180_evk`, used with the Cortex-M33 core
  (``mimxrt1180_evk/mimxrt1189/cm33``).
* A `NAFE13388-UIM`_ expansion board plugged into the Arduino headers of the EVK.
* A 15 V DC power supply on the connector J7 of the UIM (UM12203 rev. 1.0, section 5.3).
* The jumper J91 of the UIM closed. It connects the AICOM input of the chip to the AICOM terminal
  of the board (schematic SCH-91005 rev. C2, sheet 2). UM12203, table 2, gives closed as the
  default, but the schematic marks the jumper as open by default, so check the jumper on your
  board. The jumper J90 (external -15 V supply) stays in its default position, closed.
* Voltage sources of less than 5 V for the inputs AI1P and AI2P, measured against AICOM.
  See the schematic of the UIM for the terminals.

.. _NAFE13388-UIM: https://www.nxp.com/design/design-center/development-boards-and-designs/NAFE13388-UIM

Wiring
******

The UIM is made for the Arduino connectors of the FRDM boards. The devicetree overlay of this
sample describes how its signals reach the pins of the EVK. The sources are the UIM schematic
(SCH-91005 rev. C2, sheet 4), the user manual of the EVK (UM12021 rev. 3.0, tables 32 to 35)
and the schematic of the EVK (SCH-50577 rev. C4, sheet 26).

.. list-table::
   :header-rows: 1

   * - Signal of the UIM
     - Arduino pin
     - Pad of the EVK
   * - SPI clock, MISO, MOSI, chip select
     - D13, D12, D11, D10
     - GPIO_SD_B1_01, 03, 02, 00 (LPSPI3)
   * - nRESET
     - D7
     - GPIO_AD_08 (GPIO4 pin 8)
   * - SYNC
     - D6
     - GPIO_AD_07 (GPIO4 pin 7)
   * - nDRDY
     - D4
     - GPIO_AD_12 (GPIO4 pin 12)
   * - nINT (not used by this sample)
     - D3
     - GPIO_AD_06 (GPIO4 pin 6)

The following points are assumptions. No document that was used for this sample states them,
so check them on your hardware:

* The SPI address pin of the chip is low, so ``nxp,dev-addr`` is 0. The text of the schematic
  shows GND on the same row as the pin SPI_ADDR.
* AICOM is the return of your voltage source. With J91 open, AICOM is not connected to anything
  on the board and the readings are not meaningful.
* The DRDY signal is active high at the chip, although the net is named nDRDY.
* The SPI clock of 500 kHz is the one that NXP uses in its example for this board. The limit
  of the digital isolators on the UIM is not known.

The EVK has no ``arduino_header`` node in its devicetree yet, so the sample describes the
UIM in an overlay. A shield for the UIM may replace this overlay later.

Building and Running
********************

Build the sample for the Cortex-M33 core of the EVK:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/adc/nafe13388
   :board: mimxrt1180_evk/mimxrt1189/cm33
   :goals: build flash
   :compact:

Open the console of the EVK (the debug USB port) at 115200 baud.

Shell commands
**************

``nafe read <0|1>``
   Read one channel with :c:func:`adc_read_dt`.

``nafe seq``
   Read both channels in one sequence with :c:func:`adc_read`.

``nafe cont [count]``
   Continuous reading with a callback, 1 to 32 sequences, 10 by default.

The device does one reading at a time. The shell runs one command after the other, and
``nafe cont`` stops its reading before it returns, so the commands never overlap. A reading that
is started from another thread while a continuous reading runs fails with ``-EBUSY``.

Sample Output
*************

The values depend on the voltages that you apply. These are example lines only:

.. code-block:: console

   NAFE13388 sample
   Single read:
     channel 0: raw 1677722 = 1000 mV
   Sequence read:
     channel 0: raw 1677722 = 1000 mV
     channel 1: raw 3355443 = 2000 mV
   Continuous read: 10 sequences, 0 late
     [0] channel 0: 1000 mV, channel 1: 2000 mV
     ...
     [9] channel 0: 1000 mV, channel 1: 2000 mV
   Try the shell commands: nafe read <0|1>, nafe seq, nafe cont [count]
