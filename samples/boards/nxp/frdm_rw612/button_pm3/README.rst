.. zephyr:code-sample:: rw612_button_pm3
   :name: RW61x button for PM3
   :relevant-api: gpio_interface

   Use PM3 mode with a button and LED

Overview
********

This sample specifically tests PM3 mode of the RW61x.  PM3 mode reduces power by
powering off most registers.  Driver functionality must be restored after waking
from PM3 mode.

:zephyr:code-sample:`button` runs on the :zephyr:board:`rd_rw612_bga` board,
and the button on that board is connected to a wake pin on the SOC.  But on the
:zephyr:board:`frdm_rw612` board, the button is not connected to a wake pin
without hardware modification, and by default this button will not wake the SOC
from PM3 mode.

Instead, this sample uses the RTC to wake the SOC after a few seconds.  After
initialization, the sample enters a loop, and works like this:
* Prints a message it is entering PM3 and turns off the LED
* Sleeps long enough to enter PM3 mode
* RTC wakes from PM3, LED turns on, and message prints
* Waits for button pressed.  When pressed, message prints and loop repeats

Requirements
************

This application uses :zephyr:board:`frdm_rw612` for the demo.

Building, Flashing and Running
******************************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/frdm_rw612/button_pm3
   :board: frdm_rw612
   :goals: build flash
   :compact:


Sample Output
=================
.. code-block:: console

   *** Booting Zephyr OS build vX.Y.Z ***
   Set up button at gpio@0 pin 11
   Set up LED at gpio@0 pin 12
   Entering PM3 mode, button will not be responsive
   Exited PM3 mode, press button
   Button pressed at 1043810286
   Entering PM3 mode, button will not be responsive
