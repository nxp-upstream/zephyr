.. zephyr:code-sample:: mpipe-fs
   :name: Multimedia Pipeline filesystem sample
   :relevant-api: mpipe_fs mpipe_dt

   Copy a file through a two-element Multimedia Pipeline, the smallest pipeline
   there is.

Overview
********

.. graphviz::

   digraph pipeline {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     file_src  [label="file_src"];
     file_sink [label="file_sink"];
     file_src -> file_sink;
   }

The pipeline reads an input file from a mounted FAT filesystem and writes it to an output file.

The sample builds for ``native_sim/native/64``.

The pipeline is described in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node
per element, and the ``sources`` property of an element names the element it receives its buffers
from. The pipeline is built at boot from that description, and the options of the framework and of
its elements are enabled by the nodes.

The pipeline of ``pipeline.dtsi`` is ``file_src -> file_sink``. The FAT volume
is mounted at boot by a ``zephyr,fstab`` node.

The only code of the application writes the file to copy, before the pipeline
is built and played.

Notes
*****

- On ``native_sim``, the sample uses a ``zephyr,flash-disk`` named ``SD`` backed
  by ``flash.bin``. Use ``--flash_erase`` (or delete ``flash.bin``) to start from
  a clean filesystem.

Building and Running
********************

Native simulator (flash-backed FAT volume)
==========================================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/fs
   :board: native_sim/native/64
   :goals: build
   :compact:

Run (erase flash content first)::

  ./build/zephyr/zephyr.exe --flash_erase

After the run completes, the output file will be present in the FAT volume.

To reuse the same flash image across runs, omit ``--flash_erase``.
