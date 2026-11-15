.. zephyr:code-sample:: mpipe-fs-dt
   :name: File copy pipeline in the devicetree
   :relevant-api: mpipe_dt

   Copy a file to another one, with a pipeline described in the devicetree.

Overview
********

This sample is the :zephyr:code-sample:`mpipe-fs` sample with its pipeline described
in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node per
element, and the ``sources`` property of an element names the element it
receives its buffers from. The pipeline is built at boot from that
description, and the options of the framework and of its elements are enabled
by the nodes.

The pipeline of ``pipeline.dtsi`` is ``file_src -> file_sink``. The FAT volume
is mounted at boot by a ``zephyr,fstab`` node.

The only code of the application writes the file to copy, before the pipeline
is built and played.

Building and Running
********************

native_sim
==========

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/fs_dt
   :board: native_sim/native/64
   :goals: build
   :compact:

Run, erasing the flash-backed FAT volume first::

  ./build/zephyr/zephyr.exe --flash_erase
