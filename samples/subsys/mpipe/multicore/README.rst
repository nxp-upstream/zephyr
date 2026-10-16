.. zephyr:code-sample:: mpipe-multicore
   :name: Multi-core MJPEG file decode pipeline
   :relevant-api: mpipe_vid_transform_clients mpipe_erpc mpipe_img mpipe_fs mpipe_disp mpipe_base

   Decode Motion-JPEG from a file on a second core and display the frames.

Overview
********

This sample runs a Multimedia Pipeline across the two cores of a SoC. The primary core reads
an MJPEG file, splits it into JPEG frames and displays the decoded frames; the decoding itself
runs on the secondary core, reached through a video transform client element over eRPC and
the Zephyr IPC service.

Pipeline topology
=================

.. graphviz::

   digraph pipeline {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     file_src     [label="file_src\n(fs)"];
     jpeg_parser  [label="jpeg_parser\n(img)"];
     caps_filter  [label="caps_filter\n(base)"];
     queue        [label="queue\n(base)"];
     jpeg_dec     [label="vid_transform_client\n(vid, eRPC)"];
     display      [label="display\n(disp)"];
     file_src -> jpeg_parser -> caps_filter -> queue -> jpeg_dec -> display;
   }

Elements
--------

- ``mpipe_file_src`` reads chunks from the file specified by
  :kconfig:option:`CONFIG_FILE_INPUT_PATH`.
- ``mpipe_img_jpeg_parser`` splits the MJPEG byte stream into individual JPEG frames.
- ``mpipe_caps_filter`` constrains the JPEG format (width/height from Kconfig:
  :kconfig:option:`CONFIG_JPEG_IMAGE_WIDTH`, :kconfig:option:`CONFIG_JPEG_IMAGE_HEIGHT`).
- ``mpipe_queue`` runs the elements after it on a thread of its own, so the file is read
  and parsed while the remote core decodes the previous frame.
- ``mpipe_vid_transform_client`` forwards each frame by address to the remote core and
  waits for the decoded one. Its negotiation hooks are served by the remote core as well.
- ``mpipe_disp_sink`` renders decoded frames to the display.

The remote core runs the ``remote`` application of this sample: an eRPC server that
implements the video transform interface and decodes with the ``jpegdec`` module. Both
sides are generated from the same interface definition,
:zephyr_file:`subsys/mpipe/vid/erpc/mpipe_vid_transform.erpc`.

Input
*****

The sample expects an MJPEG file at the path specified by
:kconfig:option:`CONFIG_FILE_INPUT_PATH` (default ``/SD:/test.mjpeg``).

A test MJPEG file is **generated at build time** using GStreamer CLI, embedded into the
binary and written to the filesystem on first boot, or again when the file on the card no
longer has the embedded file's size. No manual file preparation is required.

Requirements
************

The sample supports :zephyr:board:`mimxrt1170_evk` with the ``rk055hdmipi4ma0`` display
shield and a FAT-formatted, writable SD card. The M7 is the primary core, the M4 the
remote one.

The build host needs:

- GStreamer (the ``gst-launch-1.0`` tool and the ``good`` plugins which include
  ``videotestsrc`` and ``jpegenc``). On Debian/Ubuntu::

    sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-good

- ``erpcgen``, the eRPC interface compiler, on the ``PATH``. It is built from the
  ``erpcgen`` directory of the ``erpc`` module (``modules/lib/erpc``).

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/multicore
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :west-args: --sysbuild
   :gen-args: -Dmulticore_SHIELD=rk055hdmipi4ma0
   :goals: build flash
   :compact:

Sysbuild builds the remote application for the M4 and the primary application loads it at
boot. The M4 console is on ``LPUART2``; it prints ``Remote started``. The M7 console shows
the pipeline reaching end of stream:

.. code-block:: console

   [00:00:00.000,000] <inf> main: SDCard mounted at /SD:
   [00:00:00.000,000] <inf> main: Pipeline linked.
   [00:00:xx.xxx,xxx] <inf> mpipe_player: Player #0: end of stream
