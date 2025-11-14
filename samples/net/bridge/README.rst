.. zephyr:code-sample:: bridge
   :name: bridge
   :relevant-api: ethernet net_if

   Test and debug Ethernet bridge

Overview
********

Example on testing/debugging Ethernet bridge

The source code for this sample application can be found at:
:zephyr_file:`samples/net/bridge`.

Building and Running
********************

Follow these steps to build the bridge sample application:

.. zephyr-app-commands::
   :zephyr-app: samples/net/bridge
   :board: <board to use>
   :conf: prj.conf
   :goals: build
   :compact:
