.. zephyr:code-sample:: mp-jpeg-decode
   :name: MJPEG file decode pipeline

Description
***********

This sample demonstrates decoding Motion-JPEG (MJPEG) from a file and displaying the decoded frames
using libMP.

Pipeline topology
=================

The sample builds one of two slightly different pipelines depending on whether a
hardware JPEG decoder is available (``zephyr,jpegdec`` chosen node).

Common front-end
----------------

::

    +----------+   +-------------+
    | filesrc  |-->| jpeg_parser |
    | (zfs)    |   | (zjpeg)     |
    +----------+   +-------------+

- ``mp_zfilesrc`` reads chunks from the file specified by :kconfig:option:`CONFIG_FILE_INPUT_PATH`.
- ``mp_zjpeg_parser`` splits MJPEG into individual JPEG frames.

Pipeline A: HW JPEG decode (typical NV12 output)
-----------------------------------------------

When ``zephyr,jpegdec`` is present, decoding is performed by a hardware-backed
``mp_zvid_transform``. On many SoCs, the decoded output is typically NV12 (or
another YUV format) while many display controllers expect an RGB format.

Therefore the pipeline usually includes both a capsfilter (to force a fixed
decoded format) and a software ``videoconvert`` stage to convert NV12 to RGB565
for the display.

::

    +----------+   +-------------+   +----------------------------+   +------------+   +--------------+   +--------------------+   +-----------+
    | filesrc  |-->| jpeg_parser |-->| HW jpegdec (zvid_transform)|-->| capsfilter |-->| videoconvert |-->| optional transform |-->| display   |
    | (zfs)    |   | (zjpeg)     |   |  (zephyr,jpegdec)          |   | (libmp)    |   | (zvid)       |   | (zvid_transform)   |   | (zdisp)   |
    +----------+   +-------------+   +----------------------------+   +------------+   +--------------+   +--------------------+   +-----------+

- ``mp_caps_filter`` forces decoded caps (width/height/pixel format) from Kconfig:
  :kconfig:option:`CONFIG_JPEG_IMAGE_WIDTH`, :kconfig:option:`CONFIG_JPEG_IMAGE_HEIGHT`,
  :kconfig:option:`CONFIG_JPEG_PIX_FMT` (default: ``NV12``).
- ``mp_zvid_convert`` performs software pixel-format conversion.
  Currently supported conversions include:

  - NV12 -> RGB565
  - XRGB32 <-> ARGB32 (alpha/padding handled explicitly)

Pipeline B: SW JPEG decode (RGB565 output)
-----------------------------------------

When no ``zephyr,jpegdec`` is present, decoding falls back to the software
``mp_zjpeg_decoder``. The SW decoder currently outputs RGB565, so no
``mp_zvid_convert`` stage is needed.

::

    +----------+   +-------------+   +------------------------+   +------------+   +--------------------+   +-----------+
    | filesrc  |-->| jpeg_parser |-->| SW jpegdec (zjpeg_dec) |-->| capsfilter |-->| optional transform |-->| display   |
    | (zfs)    |   | (zjpeg)     |   | (RGB565)               |   | (libmp)    |   | (zvid_transform)   |   | (zdisp)   |
    +----------+   +-------------+   +------------------------+   +------------+   +--------------------+   +-----------+

Notes
-----

- If ``zephyr,videotrans`` is available, an additional ``mp_zvid_transform`` may be inserted
  after conversion (e.g. for rotation).
- The capsfilter is used to keep the pipeline simple on embedded targets by forcing a fixed
  output format (instead of relying on preroll-based discovery).

Input
*****

The sample expects an MJPEG file at::

  /SD:/test.mjpeg

On ``native_sim/native/64``, the sample uses a flash-backed FAT volume stored in ``flash.bin`` file
as a emulated SDCard and embed an MJPEG test file into it.

Building and Running
********************

Native simulator
================

Build::

  west build -b native_sim/native/64 samples/subsys/mp/jpeg_dec

Run::

  ./build/zephyr/zephyr.exe --flash=flash.bin --flash_erase

Then populate ``flash.bin`` with ``/SD:/test.mjpeg``.

NXP RT EVK platforms
====================

Build example (RT700 CPU0)::

  west build -b mimxrt700_evk/mimxrt798s/cm33/cpu0 samples/subsys/mp/jpeg_dec

Make sure the SD card is FAT formatted, writable, and contains ``test.mjpeg`` at the root.
