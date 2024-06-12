.. _fxls8974:

FXLS8974 Accelerometer/Magnetometer Sensor
##########################################

Overview
********

This sample application shows how to use the FXLS8974 driver.
The driver supports FXLS8974 accelerometer/magnetometer and
MMA8451Q, MMA8652FC, MMA8653FC accelerometers.

Building and Running
********************

This project outputs sensor data to the console. FXLS8974
sensor is present on the :ref:`frdm_k64f`, :ref:`frdm_k22f`,
:ref:`frdm_kw41z`, :ref:`hexiwear`, and :ref:`twr_ke18f` boards.
Accelerometer only devices are present on the :ref:`frdm_kl25z`,
:ref:`bbc_microbit`, and :ref:`reel_board` boards. It does not work on
QEMU.


Sample Output
=============

.. code-block:: console

   AX= -0.191537 AY=  0.067037 AZ=  9.902418 MX=  0.379000 MY=  0.271000 MZ= -0.056000 T= 22.080000
   AX= -0.162806 AY=  0.143652 AZ=  9.940725 MX=  0.391000 MY=  0.307000 MZ= -0.058000 T= 22.080000
   AX= -0.172383 AY=  0.134075 AZ=  9.969455 MX=  0.395000 MY=  0.287000 MZ= -0.017000 T= 22.080000
   AX= -0.210690 AY=  0.105344 AZ=  9.911994 MX=  0.407000 MY=  0.306000 MZ= -0.068000 T= 22.080000
   AX= -0.153229 AY=  0.124498 AZ=  9.950302 MX=  0.393000 MY=  0.301000 MZ= -0.021000 T= 22.080000
   AX= -0.153229 AY=  0.095768 AZ=  9.921571 MX=  0.398000 MY=  0.278000 MZ= -0.040000 T= 22.080000
   AX= -0.162806 AY=  0.105344 AZ=  9.902418 MX=  0.372000 MY=  0.300000 MZ= -0.046000 T= 22.080000

<repeats endlessly>
