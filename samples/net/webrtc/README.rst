.. zephyr:code-sample:: webrtc
   :name: WebRTC client
   :relevant-api: video_interface display_interface i2s_interface

   Make a two-way audio and video WebRTC call between a Zephyr board and Chrome.

Overview
********

This sample is a WebRTC client demo for Zephyr. On an NXP MIMXRT1170-EVKB it makes a full
bidirectional audio and video call with Google Chrome running on a desktop:

* Uplink video: the board camera (OV5640) to Chrome, as H.264.
* Uplink audio: the board microphones (DMIC) to Chrome, as PCMU (G.711).
* Downlink video: the Chrome camera to the board display (RK055), as H.264.
* Downlink audio: the Chrome microphone to the board headphone output (WM8962), as PCMU.

Video is 320x240 at 5 frames per second, H.264 Constrained Baseline, the profile WebRTC requires.
Audio is 8 kHz PCMU in 20 ms packets. Everything (WebRTC stack, DTLS-SRTP, H.264 encoding and
decoding, audio and display) runs on the 1 GHz Cortex-M7 core, without video hardware.

Rationale
=========

The main goal of the demo is to show that a Zephyr board can run a WebRTC client that interoperates
with standard WebRTC peers. To prove compatibility, the demo makes a video call, with audio, to a
generic WebRTC client: the Chrome browser running on a separate device.

Media quality is sub-optimal: The board is relatively low-performance (the demo uses a single
Cortex-M7 core running at 1 GHz), and it encodes and decodes H.264 video in software. Video is
therefore limited to 320x240 at 5 frames per second, and audio to PCMU. Latency has not been
measured, but the call feels conversational.

Next steps:

* Replace PCMU with Opus for the audio RTP session.
* Port the media path to mpipe.

The main advantage of Opus will be better quality with less bitrate: Right now the
PCMU streams are narrowband (8 kHz) mono and using 64 kbps. We should be able to do
wideband audio (16 kHz) mono at 16-24 kbps with Opus. Note that the DMIC capture and
the WM8962 playback already run at 16 kHz. The cost is CPU, which needs to be
measured against the spare capacity in the Cortex-M7 core.

The mpipe port would have several advantages:

* Less application code. The hand-wired media path (camera capture, color conversion, codec
  threads, display, DMIC capture and SAI playback, and bounded queues) is most of the application;
  ``src/media_rt1170_zephyr.c`` alone is about 1,200 lines. With mpipe it becomes a pipeline of
  elements, and the application keeps only the signaling and the libpeer glue.
* Portability. RT1170-specific pieces, like the one-shot eDMA playback, would sit behind generic
  plugins, so another board would need configuration rather than a new media file.
* Reusable parts. Codec elements (MiniH264, h264swdec, and libopus if the audio session moves to
  Opus) and a libpeer RTP source and sink element would be useful well beyond this demo.

The main disadvantage is that mpipe is still a moving target.

Architecture
============

.. image:: doc/architecture.svg
   :alt: Architecture of the demo: SDP over HTTP, then direct media between Chrome and the board
   :width: 100%

* Signaling: the board is the SDP offerer. It sends its offer to a small HTTP signaling server on
  the desktop (``tests/physical_browser_call.py``), which passes it to the browser page and returns
  Chrome's answer. The server carries only the SDP.
* Media: ICE, DTLS-SRTP, RTP and RTCP then flow directly between Chrome and the board, through
  libpeer on top of Zephyr's sockets and Mbed TLS.
* Uplink video: OV5640 capture (video API), BGRX to I420 conversion, MiniH264 encoder, libpeer
  H.264 packetization.
* Downlink video: libpeer FU-A reassembly, h264swdec decoder, I420 to RGB565 conversion, display
  API. Packet loss triggers an RTCP PLI, and decoding resumes at the next IDR picture.
* Uplink audio: DMIC capture at 16 kHz, 8 kHz PCMU encoding, libpeer.
* Downlink audio: PCMU to PCM, played through the SAI to the WM8962 codec from a dedicated
  playback thread.

Dependencies
============

The sample uses three external modules, all listed in ``west.yml``:

* `libpeer <https://github.com/chemag/libpeer/tree/zephyrtc>`_ (MIT): the WebRTC stack. It also
  builds libsrtp from its ``third_party/libsrtp`` submodule.
* `MiniH264 <https://github.com/chemag/minih264/tree/zephyr-minimal>`_ (CC0-1.0): the H.264
  encoder, with its Zephyr glue in ``modules/minih264``.
* `h264swdec <https://github.com/chemag/h264swdec>`_ (Apache-2.0): the H.264 decoder, a standalone
  build of the Android H264SwDec decoder, with its Zephyr glue in
  ``modules/h264swdec``.

Requirements
************

Hardware:

* NXP MIMXRT1170-EVKB (board revision B).
* The 5 V power adapter in J43. Seat the plug fully: a loose plug causes resets under load.
* An OV5640 camera module (``nxp_btb44_ov5640`` shield) in the camera connector J2.
* A RK055HDMIPI4MA0 MIPI display panel (``rk055hdmipi4ma0`` shield).
* Headphones in J101. The microphones are on the board.
* A USB cable in J86, for the MCU-Link debug probe and the serial console.
* Ethernet: a cable in J32, on the same LAN as the desktop, with DHCP.
* Wi-Fi (optional): an Embedded Artists 2EL M.2 module (Murata LBES5PL2EL, NXP IW612) in J54,
  and an access point on the same LAN as the desktop, with DHCP.

Desktop:

* Linux with Google Chrome and Python 3.11 or newer.
* `NXP LinkServer <https://www.nxp.com/linkserver>`_, to flash the board.
* A firewall that accepts TCP connections to port 8080 from the board.

Setting up
**********

Create a west workspace from the ``zephyrtc`` branch, then install the Python requirements, the
Zephyr SDK and the IW612 Wi-Fi firmware from the NXP binary blobs:

.. code-block:: console

   west init -m https://github.com/chemag/zephyr --mr zephyrtc zephyrtc-workspace
   cd zephyrtc-workspace
   west update
   west packages pip --install
   west sdk install
   west blobs fetch hal_nxp --allow-regex 'nw61x/[^/]*$'
   pip install -r zephyr/samples/net/webrtc/requirements.txt

See the `Getting Started Guide
<https://docs.zephyrproject.org/latest/develop/getting_started/index.html>`_ for details.

Configuring
***********

The build helper reads a private settings file. Copy the example, make it readable only by you
and edit it:

.. code-block:: console

   mkdir -p ~/.config/zephyrtc
   cp zephyr/samples/net/webrtc/config/rt1170_demo.toml.example \
      ~/.config/zephyrtc/rt1170.toml
   chmod 600 ~/.config/zephyrtc/rt1170.toml

Set at least:

* ``network``: ``"ethernet"`` or ``"wifi"``.
* ``signaling_url``: the address of the desktop on the LAN, for example
  ``"http://192.168.1.10:8080/offer"``.
* ``wifi_ssid`` and ``wifi_password``, for Wi-Fi only.

``run_ms`` sets the call length on the board, and ``display_rotation`` and ``display_scale`` fit
the remote video to the panel. The helper refuses a settings file that other users can read,
and writes the generated Kconfig fragment, which holds the Wi-Fi credentials, next to it.
Nothing from this file goes into the source tree.

Building and flashing
*********************

Build with the helper. It selects the board, the shields, the overlays for the configured network
and the settings, and builds into ``build/webrtc-ethernet`` or
``build/webrtc-wifi``:

.. code-block:: console

   python3 zephyr/samples/net/webrtc/tools/rt1170_demo.py

For Ethernet, the helper runs the equivalent of:

.. code-block:: console

   settings=$HOME/.config/zephyrtc/rt1170.generated.conf
   west build -p always -b mimxrt1170_evk@B/mimxrt1176/cm7 \
      --shield nxp_btb44_ov5640 --shield rk055hdmipi4ma0 \
      -d build/webrtc-ethernet zephyr/samples/net/webrtc -- \
      -DEXTRA_CONF_FILE="overlay-mimxrt1170-av.conf;$settings"

For Wi-Fi, add ``overlay-mimxrt1170-wifi.conf`` to ``EXTRA_CONF_FILE`` and
``-DEXTRA_DTC_OVERLAY_FILE=boards/mimxrt1170_evk_mimxrt1176_cm7_B_wifi.overlay``.

Flash with LinkServer. If more than one board is connected, select the probe by its serial
number, as listed by ``LinkServer probes``:

.. code-block:: console

   west flash -d build/webrtc-ethernet -r linkserver --probe <serial>

The board starts its call as soon as it boots, so leave it for the next step: it has to boot
after the browser side is ready.

Running
*******

1. Open the serial console of the MCU-Link (115200 baud, 8N1).

2. Start the browser side on the desktop. It starts the signaling server on port 8080, launches
   Chrome and prints ``READY_FOR_RT1170`` when Chrome waits for the offer:

   .. code-block:: console

      cd zephyr/samples/net/webrtc
      python3 tests/physical_browser_call.py --output-dir /tmp/webrtc-call \
         --headed --real-media --timeout 150 --connected-seconds 100

   Without ``--real-media``, Chrome sends its built-in test pattern and tone instead of the
   desktop camera and microphone. Keep ``--connected-seconds`` shorter than ``run_ms``.

3. Switch the board off and on with SW5. A power cycle, rather than a reset, is needed after
   flashing, and the Wi-Fi module only initializes after one. The board connects to the network,
   sends its offer and starts the call. The script prints ``BROWSER_CONNECTED``, then
   ``MEDIA_PATHS_ACTIVE`` once audio and video flow in both directions. The board's video appears
   in the Chrome window, and Chrome's video on the panel.

The script checks the call statistics while it runs, fails if any media path stalls, and writes
its logs and statistics to the output directory.

Media on the Multimedia Pipeline subsystem
*******************************************

``overlay-mpipe.conf`` replaces the media code of the sample by pipelines of the
:ref:`Multimedia Pipeline <mpipe>` subsystem, one per leg of the call, each with its own thread:

.. code-block:: none

   camera      vid_src -> caps_filter -> vid_convert -> vid_h264_encoder -> app_sink
   display     app_src -> vid_h264_decoder -> vid_transform -> caps_filter -> vid_overlay
               -> disp_sink
   microphone  aud_dmic_src -> caps_filter -> app_sink
   headphones  app_src -> aud_raw_parser -> queue -> aud_i2s_codec_sink

The application (``src/media_mpipe.c``) only moves the encoded frames and the G.711 packets between
the application elements and libpeer. On a board without a video transform device the display leg
converts in software (``vid_convert``) and shows the frames at their own size.

With this media the board sends the camera at its own rate, 30 frames per second, with a key frame
when the peer asks for one and every ``CONFIG_ZEPHYRTC_VIDEO_GOP`` frames. libpeer sends the
capture time of every frame and RTCP sender reports for both streams, from one clock, so the peer
can align the audio with the video.

This media needs changes in two modules that the manifest does not point to yet: the branch
``zephyrtc-mpipe`` of ``modules/lib/libpeer`` (timestamps from the caller, RTCP reports, a receive
loop that drains every pending datagram) and the branch ``zephyrtc-mpipe`` of
``modules/lib/minih264`` (a DSP path for the motion search).

On the MIMXRT1170-EVKB
======================

Add ``overlay-mpipe.conf`` and ``overlay-mimxrt1170-mpipe.conf`` after the other configuration
files. For Wi-Fi:

.. code-block:: console

   conf="overlay-mimxrt1170-av.conf;overlay-mimxrt1170-wifi.conf"
   conf="$conf;overlay-mpipe.conf;overlay-mimxrt1170-mpipe.conf"
   conf="$conf;$HOME/.config/zephyrtc/rt1170.generated.conf"
   west build -p always -b mimxrt1170_evk@B/mimxrt1176/cm7 \
      --shield nxp_btb44_ov5640 --shield rk055hdmipi4ma0 \
      -d build/webrtc-mpipe zephyr/samples/net/webrtc -- \
      -DEXTRA_CONF_FILE="$conf" \
      -DEXTRA_DTC_OVERLAY_FILE=boards/mimxrt1170_evk_mimxrt1176_cm7_B_wifi.overlay

The board file runs the code from a serial flash and puts the data in the SDRAM, which is where
most of the time of the media went. ``overlay-mimxrt1170-mpipe.conf`` therefore runs the codecs,
the pixel format conversions, SRTP, libpeer and the IP stack from the instruction TCM
(``CONFIG_ZEPHYRTC_CODE_RELOCATE``) and puts the memory of the two codecs in the on-chip RAM
(``CONFIG_ZEPHYRTC_CODEC_DATA_RELOCATE``). The PXP converts the decoded frames to RGB565, turns
them and scales them in one pass, and a plane of the LCDIFv2 scans the result out without a copy.

The picture of the peer is scaled by ``CONFIG_ZEPHYRTC_DISPLAY_SCALE``. The default of the overlay
is two, 480x640 on the 720x1280 panel. Three fills the width of the panel, but the plane then
loads the SDRAM enough to double the load of the processor: the camera frames, which the processor
reads from the same SDRAM, do not fit in the on-chip RAM.

A call of the board with itself
===============================

``overlay-self-call.conf`` makes two endpoints of the image call each other through the loopback of
the IP stack: the camera and the microphones go through the encoders, RTP, SRTP, the decoders, to
the panel and the headphones. Nothing but signaling and the network driver is left out, so this is
the way to see and to measure the media on a board that has no network. Every second it logs the
frames sent and shown, the time from the capture of a frame to the display sink, and the load of
the processor:

.. code-block:: console

   west build -p always -b mimxrt1170_evk@B/mimxrt1176/cm7 \
      --shield nxp_btb44_ov5640 --shield rk055hdmipi4ma0 \
      -d build/webrtc-self zephyr/samples/net/webrtc -- \
      -DEXTRA_CONF_FILE="overlay-self-call.conf;overlay-mpipe.conf;overlay-mimxrt1170-mpipe.conf"

.. code-block:: console

   event=self_call video_sent=209 video_shown=209 fps=30 latency_ms=25/27/31
      audio_sent=351 audio_played=350 cpu=36

Measured this way on the MIMXRT1170-EVKB, 320x240, on the Cortex-M7:

.. list-table::
   :header-rows: 1

   * - Configuration
     - Frames per second
     - Processor load
   * - Code in the flash, data in the SDRAM (encode and decode alone, in one thread)
     - 15
     -
   * - Codecs in the TCM, their memory in the on-chip RAM
     - 15
     - 95%
   * - SRTP, libpeer and the IP stack in the TCM as well
     - 15
     - 65%
   * - DSP path of the encoder, deblocking filter on
     - 30
     - 35 to 41% (picture not scaled)
   * - PXP in the display leg, picture scaled by two
     - 30
     - 43 to 51%
   * - PXP in the display leg, picture scaled by three
     - 29 to 30
     - 76 to 89%

The first row is the frame rate of the ``h264_enc_dec`` sample of the subsystem, which encodes and
decodes in one thread. The loads of the second and third rows come from program counter samples
taken through the debug probe, the other ones from the run time statistics of the kernel. At a
scale of two a frame takes 27 ms on average from its capture to the display sink.

On the two cores
================

A sysbuild of the sample adds an image for the Cortex-M4 (``remote/``), which decodes the video of
the peer: a transform client takes the place of the decoder in the display leg, and the second
core serves a pipeline that holds the same decoder element.

.. code-block:: console

   conf="overlay-self-call.conf;overlay-mpipe.conf"
   conf="$conf;overlay-mimxrt1170-mpipe.conf;overlay-mimxrt1170-multicore.conf"
   west build -p always -b mimxrt1170_evk@B/mimxrt1176/cm7 --sysbuild \
      -d build/webrtc-multicore zephyr/samples/net/webrtc -- \
      -Dwebrtc_SHIELD="nxp_btb44_ov5640;rk055hdmipi4ma0" \
      -Dwebrtc_EXTRA_CONF_FILE="$conf" \
      -Dwebrtc_EXTRA_DTC_OVERLAY_FILE=boards/mimxrt1170_evk_mimxrt1176_cm7_B_multicore.overlay

This is an option, not the default: the second core is slow at this work. A decode call takes
20.8 ms where the first core decodes the same frame in 3 ms, because the Cortex-M4 has a small
write-through cache and neither the pictures of the decoder nor the frame it gives back fit in its
data TCM. With the PXP and the display after it, the display leg then takes a little more than a
frame period. In the call of the board with itself at a scale of two, it shows 29 frames per
second: about one frame per second is dropped in front of the decoder and asked again as a key
frame, and a frame takes 120 ms on average (80 to 155 ms) from its capture to the display sink,
where the first core alone needs 27 ms. The first core gains little: its load was 41% against 51%
in one pair of measurements and 39% against 41% in another. At a scale of three the variant no
longer keeps up (18 frames per second).

Testing
*******

The sample's twister tests run two calls between in-process peers on ``native_sim`` (one with
HTTP signaling), build a ``native_sim`` video image, and build the RT1170 images for Ethernet,
Wi-Fi, audio only and video only:

.. code-block:: console

   west twister -T zephyr/samples/net/webrtc

Troubleshooting
***************

* The board resets or stops under load: check that the J43 power plug is fully seated.
* The board never reaches the desktop: check ``signaling_url`` and that the desktop firewall
  accepts TCP connections to port 8080 from the board.
* Chrome stays in ``checking``: the board and the desktop must reach each other directly on the
  LAN. Check that the firewall accepts UDP traffic from the board.
* The board hangs after flashing, or Wi-Fi does not come up: switch the board off and on with
  SW5.

Limitations
***********

* Same-LAN calls only: no STUN or TURN servers, IPv6, ICE restart or renegotiation.
* The HTTP signaling is unauthenticated and meant for a controlled network.
* 320x240 video at 5 frames per second, and PCMU audio.
