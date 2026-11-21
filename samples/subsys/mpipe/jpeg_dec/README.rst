.. zephyr:code-sample:: mpipe-jpeg-decode
   :name: MJPEG file decode pipeline
   :relevant-api: mpipe_img mpipe_fs mpipe_disp mpipe_base mpipe_dt

   Decode Motion-JPEG from a file and display the frames, using a hardware
   decoder where the board has one and a software decoder otherwise.

Overview
********

This sample demonstrates decoding Motion-JPEG (MJPEG) from a file and displaying the decoded frames
using the Multimedia Pipeline subsystem.

Pipeline topology
=================

The pipeline has one of two slightly different shapes, depending on whether the board has a
hardware JPEG decoder: ``pipeline.dtsi`` describes the software one, and the overlay of a board
with a hardware decoder replaces the decoder node.

Pipeline A: HW JPEG decode
---------------------------

When ``zephyr,jpegdec`` is present, decoding is performed by a hardware-backed
``mpipe_vid_transform``. The HW decoder typically outputs NV12 (or another YUV
format), so a software ``mpipe_vid_convert`` stage converts the output to RGB565
for the display controller.

.. graphviz::

   digraph pipeline_a {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     file_src      [label="file_src\n(fs)"];
     jpeg_parser  [label="jpeg_parser\n(img)"];
     caps_filter   [label="caps_filter\n(base)"];
     hw_jpegdec   [label="HW jpegdec\n(vid_transform)"];
     vid_convert  [label="vid_convert\n(vid)"];
     display      [label="display\n(disp)"];
     file_src -> jpeg_parser -> caps_filter -> hw_jpegdec -> vid_convert -> display;
   }

Pipeline B: SW JPEG decode
---------------------------

When no ``zephyr,jpegdec`` is present, decoding falls back to the software
``mpipe_img_jpeg_decoder``. The SW decoder currently outputs RGB565, so no
``mpipe_vid_convert`` stage is needed.

.. graphviz::

   digraph pipeline_b {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     file_src      [label="file_src\n(fs)"];
     jpeg_parser  [label="jpeg_parser\n(img)"];
     caps_filter   [label="caps_filter\n(base)"];
     sw_jpegdec   [label="SW jpegdec\n(img)"];
     display      [label="display\n(disp)"];
     file_src -> jpeg_parser -> caps_filter -> sw_jpegdec -> display;
   }

Elements
--------

- ``mpipe_file_src`` reads chunks from the file named by the ``path`` property of its node.
- ``mpipe_img_jpeg_parser`` splits the MJPEG byte stream into individual JPEG frames.
- ``mpipe_caps_filter`` constrains the JPEG format (``width`` and ``height`` properties of its
  node).
- ``mpipe_img_jpeg_decoder`` (SW) or ``mpipe_vid_transform`` (HW) decodes JPEG frames.
- ``mpipe_vid_convert`` performs pixel-format conversion (NV12 -> RGB565) when using HW decode.
- ``mpipe_disp_sink`` renders decoded frames to the display.

Notes
-----

- The overlay of a board with a video transform device inserts an additional
  ``mpipe_vid_transform`` before the display sink, for example to rotate the frames (``rotation``
  property of its node).
- The caps_filter is placed between the parser and decoder to enforce a fixed
  JPEG frame format before decoding begins.

The pipeline is described in the devicetree: a ``zephyr,mpipe-pipeline`` node holds one child node
per element, and the ``sources`` property of an element names the element it receives its buffers
from. The pipeline is built at boot from that description, and the options of the framework and of
its elements are enabled by the nodes.

The pipeline of ``pipeline.dtsi`` is
``file_src -> jpeg_parser -> caps_filter -> jpeg_decoder -> disp_sink``, with the
software decoder. A board overlay changes it for its hardware: the
:zephyr:board:`mimxrt1170_evk` adds the PXP rotating the frames, the
:zephyr:board:`mimxrt700_evk` replaces the software decoder by the JPEG decoder
of the SoC followed by a pixel format converter.

The application has no code: the pipeline node has the ``autostart`` property.

Input
*****

The sample expects an MJPEG file at the path of the file source node (``/SD:/test.mjpeg`` in
``pipeline.dtsi``).

A test MJPEG file is **generated at build time** using GStreamer CLI.
The resolution is the one the caps filter node states, and the number of frames is the CMake
variable ``MJPEG_NUM_FRAMES`` (150 by default, ``-DMJPEG_NUM_FRAMES=<n>`` to change it).

The generated file is placed in the build directory and embedded into the binary
via ``mjpeg.inc``. On every boot the sample checks whether the file already exists
on the filesystem and, if so, compares its size against the embedded data: if the
sizes match, the file is assumed unchanged and is left as-is; otherwise it is
rewritten from the embedded copy. This avoids rewriting the file to the SD card on
every boot (which would significantly increase flash time), at the cost of not
detecting a same-size content change. No manual file preparation is required.

Requirements
************

The build host must have GStreamer installed (specifically the ``gst-launch-1.0``
tool and the ``good`` plugins which include ``videotestsrc`` and ``jpegenc``).

On Debian/Ubuntu::

  sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-good

On Fedora::

  sudo dnf install gstreamer1-plugins-good

On macOS (Homebrew)::

  brew install gstreamer

Building and Running
********************

Native simulator
================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/jpeg_dec
   :board: native_sim/native/64
   :goals: build
   :compact:

Run::

  ./build/zephyr/zephyr.exe

The test MJPEG file is generated during build and embedded into the binary.
It is written to the simulated SD card (``flash.bin``) on first run, and again
on later runs only if the file's size on the card no longer matches the
embedded one.

NXP RT EVK platforms
====================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/jpeg_dec
   :board: mimxrt700_evk/mimxrt798s/cm33/cpu0
   :goals: build
   :compact:

The test MJPEG file is generated during build and embedded into the binary.
It is written to the SD card automatically on first boot, and again on later
boots only if the file's size on the card no longer matches the embedded one.
Make sure the SD card is FAT formatted and writable.
