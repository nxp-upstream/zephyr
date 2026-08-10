.. _trgmux_adc:

ADC Hardware Trigger via Signal Router
#######################################

Overview
********

This sample demonstrates how to route a periodic timer output to an ADC
trigger input using a signal-routing peripheral.  The C source is
**platform-agnostic**: it only calls the standard Zephyr MUX, Counter, and ADC
APIs.  All per-board wiring — which routing peripheral, which ADC, which timer,
and the concrete cell values — lives in the board overlay.

The general flow is:

1. Apply the DT-defined signal route with :c:func:`mux_state_apply`.
2. Start a periodic timer with :c:func:`counter_set_top_value` and
   :c:func:`counter_start`.
3. In a loop, call :c:func:`adc_read_dt` (the ADC is armed and waits for the
   hardware trigger to complete the conversion).
4. Convert the raw result to millivolts and print it.

Supported Boards
****************

+------------------+------------------+------------------+------------------+
| Board            | Routing peripheral | ADC            | Timer            |
+==================+==================+==================+==================+
| frdm_mcxe247     | TRGMUX           | ADC12 (adc0)     | LPIT ch0         |
+------------------+------------------+------------------+------------------+
| frdm_mcxa156     | INPUTMUX         | LPADC (lpadc0)   | CTimer0 match-0  |
+------------------+------------------+------------------+------------------+

Hardware Setup
**************

frdm_mcxe247
   Connect a 0–3.3 V signal to **ADC0_SE1** (PTA1, Arduino header A1 / J4
   pin 10) to observe a non-zero reading.

frdm_mcxa156
   Connect a 0–3.3 V signal to **ADC0_A0** (P2_0, Arduino header A0 / J4
   pin 8) to observe a non-zero reading.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/mux/trgmux_adc
   :board: frdm_mcxe247
   :goals: build flash
   :compact:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/mux/trgmux_adc
   :board: frdm_mcxa156
   :goals: build flash
   :compact:

Sample Output
*************

.. code-block:: console

   Signal route applied
   Timer started at 100 Hz (top=800000 ticks)
   ADC reading[0]: 1648 mV
   ADC reading[1]: 1649 mV
   ADC reading[2]: 1647 mV
   ...

Adding a New Board
******************

To port the sample to another board that has a signal-routing peripheral
and an ADC with hardware-trigger support:

1. Create ``boards/<board>.overlay`` under this sample directory.
2. Enable the routing peripheral (e.g. ``&trgmux``, ``&inputmux0``,
   ``&xbar1``) with ``status = "okay"``.
3. Enable the ADC with ``enable-hardware-trigger`` and add a ``channel@N``
   child node.
4. Enable or reference the timer device.
5. Add the routing node::

      adc_hw_trigger_route: adc-hw-trigger-route {
          mux-states = <&<router> <cells…>>;
      };

6. Add to ``/zephyr,user``::

      zephyr,user {
          io-channels    = <&<adc> <ch>>;
          trigger-timer  = <&<timer_node>>;
      };

7. Add the board to the ``platform_allow`` list in ``sample.yaml``.

Consult the SoC reference manual and the HAL header
``fsl_trgmux.h`` / ``fsl_inputmux_connections.h`` for the correct cell
values for your target.
