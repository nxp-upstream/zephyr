.. zephyr:code-sample:: mp-jpeg-decode
   :name: MJPEG file decode pipeline

Description
***********

This sample demonstrates decoding Motion-JPEG (MJPEG) from a file and displaying the decoded frames
using the MP subsystem.

Pipeline topology
=================

The sample builds one of two slightly different pipelines depending on whether a
hardware JPEG decoder is available (``zephyr,jpegdec`` chosen node).

Common front-end
----------------

.. graphviz::

   digraph frontend {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     filesrc      [label="filesrc\n(zfs)"];
     jpeg_parser  [label="jpeg_parser\n(zjpeg)"];
     filesrc -> jpeg_parser;
   }

- ``mp_zfilesrc`` reads chunks from the file specified by :kconfig:option:`CONFIG_FILE_INPUT_PATH`.
- ``mp_zjpeg_parser`` splits MJPEG into individual JPEG frames.

Pipeline A: HW JPEG decode (typical NV12 output)
-------------------------------------------------

When ``zephyr,jpegdec`` is present, decoding is performed by a hardware-backed
``mp_zvid_transform``. On many SoCs, the decoded output is typically NV12 (or
another YUV format) while many display controllers expect an RGB format.

Therefore the pipeline usually includes both a capsfilter (to force a fixed
decoded format) and a software ``videoconvert`` stage to convert NV12 to RGB565
for the display.

.. graphviz::

   digraph pipeline_a {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     filesrc      [label="filesrc\n(zfs)"];
     jpeg_parser  [label="jpeg_parser\n(zjpeg)"];
     hw_jpegdec   [label="HW jpegdec\n(zvid_transform)"];
     capsfilter   [label="capsfilter\n(core)"];
     videoconvert [label="videoconvert\n(zvid)"];
     opt_transform [label="optional\ntransform"];
     display      [label="display\n(zdisp)"];
     filesrc -> jpeg_parser -> hw_jpegdec -> capsfilter -> videoconvert -> opt_transform -> display;
   }

- ``mp_caps_filter`` forces decoded caps (width/height/pixel format) from Kconfig:
  :kconfig:option:`CONFIG_JPEG_IMAGE_WIDTH`, :kconfig:option:`CONFIG_JPEG_IMAGE_HEIGHT`,
  :kconfig:option:`CONFIG_JPEG_PIX_FMT` (default: ``NV12``).
- ``mp_zvid_convert`` performs software pixel-format conversion.
  Currently supported conversions include:

  - NV12 → RGB565
  - XRGB32 ↔ ARGB32 (alpha/padding handled explicitly)

Pipeline B: SW JPEG decode (RGB565 output)
------------------------------------------

When no ``zephyr,jpegdec`` is present, decoding falls back to the software
``mp_zjpeg_decoder``. The SW decoder currently outputs RGB565, so no
``mp_zvid_convert`` stage is needed.

.. graphviz::

   digraph pipeline_b {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     filesrc      [label="filesrc\n(zfs)"];
     jpeg_parser  [label="jpeg_parser\n(zjpeg)"];
     sw_jpegdec   [label="SW jpegdec\n(zjpeg, RGB565)"];
     capsfilter   [label="capsfilter\n(core)"];
     opt_transform [label="optional\ntransform"];
     display      [label="display\n(zdisp)"];
     filesrc -> jpeg_parser -> sw_jpegdec -> capsfilter -> opt_transform -> display;
   }

Notes
-----

- If ``zephyr,videotrans`` is available, an additional ``mp_zvid_transform`` may be inserted
  after conversion (e.g. for rotation).
- The capsfilter is used to keep the pipeline simple on embedded targets by forcing a fixed
  output format (instead of relying on preroll-based discovery).

Input
*****

The sample expects an MJPEG file at the path specified by
:kconfig:option:`CONFIG_FILE_INPUT_PATH` (default ``/SD:/test.mjpeg``).

A small test file is **embedded into the binary** at build time (via ``mjpeg.inc``).
On first boot the sample checks whether the file already exists on the filesystem;
if not, it writes the embedded data automatically.  No manual file preparation is
required.

On ``native_sim/native/64``, the SD card is emulated by a flash-backed FAT volume
stored in the ``flash.bin`` file.  The embedded test file is written into this
volume on startup, so the user does not need to populate or erase ``flash.bin``
manually.

Building and Running
********************

Native simulator
================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mp/jpeg_dec
   :board: native_sim/native/64
   :goals: build
   :compact:

Run::

  ./build/zephyr/zephyr.exe --flash=flash.bin

The embedded ``test.mjpeg`` is written to the simulated SD card on first run.
Subsequent runs reuse the same ``flash.bin`` without any extra steps.

NXP RT EVK platforms
====================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mp/jpeg_dec
   :board: mimxrt700_evk/mimxrt798s/cm33/cpu0
   :goals: build
   :compact:

The embedded ``test.mjpeg`` is written to the SD card on first boot.
Make sure the SD card is FAT formatted and writable.
