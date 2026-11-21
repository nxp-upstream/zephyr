.. zephyr:code-sample:: mpipe-jpeg-decode-multicore
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

- ``mpipe_file_src`` reads chunks from the file named by the ``path`` property of its node.
- ``mpipe_img_jpeg_parser`` splits the MJPEG byte stream into individual JPEG frames.
- ``mpipe_caps_filter`` constrains the JPEG format (``width`` and ``height`` properties of its
  node).
- ``mpipe_queue`` runs the elements after it on a thread of its own, so the file is read
  and parsed while the remote core decodes the previous frame.
- ``mpipe_vid_transform_client`` forwards each frame by address to the remote core and
  waits for the decoded one. Its negotiation hooks are served by the remote core as well.
- ``mpipe_disp_sink`` renders decoded frames to the display.

The remote core runs the ``remote`` application of this sample: an eRPC server that
implements the two interfaces the client speaks and decodes with the ``jpegdec`` module.
Both sides are generated from the same interface definitions:
:zephyr_file:`subsys/mpipe/erpc/mpipe_stream.erpc` for the buffers and the states, which
every client element uses, and :zephyr_file:`subsys/mpipe/erpc/vid/mpipe_vid_format.erpc` for
the video formats. The server uses neither this framework nor any Zephyr service but the
transport, as the firmware of a core that runs neither would. The client element opens the
decoder on the server and is given an instance, which goes with each of its calls: this is how
one server, on one endpoint and in one thread, can run several client elements.
:zephyr:code-sample:`mpipe-tee-decode-multicore` shows the other case, a remote core that runs
pipelines of its own.

The pipeline is described in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node
per element, and the ``sources`` property of an element names the element it receives its buffers
from. The pipeline is built at boot from that description, and the options of the framework and of
its elements are enabled by the nodes.

The pipeline of ``pipeline.dtsi`` is
``file_src -> jpeg_parser -> caps_filter -> queue -> client -> disp_sink``, where
the client element hands the frames to the decoder running on the other core
over eRPC. The image of the other core, in ``remote/``, holds that decoder.

The application has no code: the pipeline node has the ``autostart`` property.
The test MJPEG file is generated at build time with the GStreamer CLI, which
needs ``gst-launch-1.0`` on the host, at the frame size the caps filter node
states, and written at boot to the path the file source node plays.

Input
*****

The sample expects an MJPEG file at the path of the file source node (``/SD:/test.mjpeg`` in
``pipeline.dtsi``).

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
   :zephyr-app: samples/subsys/mpipe/jpeg_dec_multicore
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :west-args: --sysbuild
   :gen-args: -Djpeg_dec_multicore_SHIELD=rk055hdmipi4ma0
   :goals: build flash
   :compact:

Sysbuild builds the remote application for the M4 and the primary application loads it at
boot. The M4 console is on ``LPUART2``; it prints ``Remote started``. The M7 console shows
the pipeline reaching end of stream:

.. code-block:: console

   [00:00:00.000,000] <inf> main: SDCard mounted at /SD:
   [00:00:00.000,000] <inf> main: Pipeline linked.
   [00:00:xx.xxx,xxx] <inf> mpipe_player: Player #0: end of stream
