.. _lcm_rgb_5inch:

NXP LCM RGB 5" 800x480 DPI Panel
################################

Overview
********

This shield configures the NXP DCIF DPI output for the 5-inch, 800x480
RGB888 panel used as ``DEMO_PANEL_LCM_RGB_5INCH`` in the MCUXpresso SDK's
``display_support.c`` for the MIMXRT2660-EVK. The panel connects to the
board's dedicated LCM connector; power-enable and backlight-enable are
driven through the board's on-board PCAL6524 and PCA9555 I2C GPIO
expanders.

Requirements
************

This shield can only be used with a board that wires its DCIF DPI signals
and the two I2C GPIO expanders to the LCM connector, such as
:ref:`mimxrt2660_evk`.

Programming
***********

Set ``--shield lcm_rgb_5inch`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/display
   :board: mimxrt2660_evk/mimxrt2663/cm85
   :shield: lcm_rgb_5inch
   :goals: build

.. include:: ../../../nxp/common/board-footer.rst.inc
