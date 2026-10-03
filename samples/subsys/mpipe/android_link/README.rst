.. zephyr:code-sample:: mpipe_android_link
   :name: mpipe Android link
   :relevant-api: mpipe

   Mirror an Android phone screen using the mpipe media pipeline framework.

Overview
********

This sample mirrors the screen of an Android phone onto a display connected to
the board. The phone runs an scrcpy-like client that captures its screen, encodes
it as a stream of MJPEG frames and sends those frames over a TCP connection.
Touch events detected on the local touch panel are converted to Android motion
events and sent back to the phone over a second (control) socket, so the mirrored
screen is also interactive.

The video path is built entirely on top of the :ref:`mpipe <mpipe>` media pipeline
framework. Instead of a hand-written receive/parse/decode/display loop, the sample
constructs the following pipeline:

.. code-block:: none

   socket_src -> jpeg_parser -> queue -> jpeg_decoder -> queue -> caps_filter -> disp_sink

* ``socket_src`` (mpipe ``net`` plugin) reads the raw byte stream from the video
  connection the application accepted and handed to it, into large chunks, and
  pushes each read as a view of its chunk.
* ``jpeg_parser`` (mpipe ``img`` plugin) cuts complete JPEG frames out of the
  byte stream at the JPEG end-of-image (``0xFFD9``) markers. A frame that lies
  inside one chunk reaches the decoder as a view of that chunk, without a copy.
* The two ``queue`` elements (mpipe ``base`` plugin) put the receiving, decoding
  and displaying on three threads. Each keeps at most a frame or two and drops
  its oldest one when the stage behind it falls behind, so the display always
  shows the newest frame and the phone is never throttled into buffering
  frames on its side.
* ``jpeg_decoder`` (mpipe ``img`` plugin) decodes each JPEG frame into raw RGB565
  pixels using the software decoder. A frame that does not decode is dropped.
* ``caps_filter`` (mpipe ``base`` plugin) pins the panel's format and geometry.
* ``disp_sink`` (mpipe ``disp`` plugin) renders the decoded frames to the Zephyr
  display device.

With ``CONFIG_ANDROID_LINK_AUDIO`` (the default) a second pipeline plays the
audio the phone sends on a separate connection, as raw PCM, 48 kHz, 16-bit,
stereo:

.. code-block:: none

   socket_src -> aud_raw_parser -> queue -> i2s_codec_sink

* ``aud_raw_parser`` (mpipe ``aud`` plugin) announces the format, which a raw
  stream cannot, and cuts the bytes into 10 ms blocks for the I2S sink.
* The ``queue`` holds 150 ms of blocks against the jitter of the link and runs
  the sink on its own thread, above the video decoder in priority.
* ``i2s_codec_sink`` (mpipe ``aud`` plugin) plays the blocks through the codec.

The two streams are not synchronized with each other, as in the phone's own
desktop client: each runs at the lowest latency it can, which keeps them within
about a tenth of a second of each other. The phone's volume setting is applied
before capture, so it is heard on the board; the phone itself stays silent while
mirroring unless the client is launched with ``audio_dup=true`` (Android 13 or
later).

The pipelines are driven by the mpipe player, one per pipeline. The application
sets up the network (Wi-Fi AP or Ethernet + DHCP server), listens on port 5000,
accepts the phone's video connection, then its audio connection, then its control
connection, hands the stream sockets to the pipelines and keeps the control one
for the touch events. When the phone disconnects it accepts the next client.

The phone's client must be launched with audio enabled and the raw codec, for
example ``audio=true audio_codec=raw`` on the scrcpy server command line, and
with ``audio=false`` when the sample is built without audio.

With ``CONFIG_ANDROID_LINK_MIC`` (the default where a DMIC exists) a third pipeline
sends the board's microphone to the phone as raw PCM, 16 kHz, 32-bit, two channels
(both slots of the PDM data line, summed by the phone), on a fourth connection the
phone opens after the control one::

   mic_src -> mic_caps_filter -> queue_mic -> mic_sink

The streams flow whenever the phone is connected; what each side does with them
is the demo mode, which the board sends as two control messages when the control
connection comes up and whenever it changes: type 101, the microphone mode, one
byte, ``0`` drops the board's microphone, ``1`` plays it on the phone's speaker,
``2`` feeds it to the applications recording from the phone's microphone; and type
102, the audio source, one byte, ``0`` for the phone's playback, ``1`` for the
phone's microphone with its own echo cancelling. The phone must be launched with
the microphone channel enabled, or the sample built with
``CONFIG_ANDROID_LINK_MIC=n``.

The modes name where the board's microphone goes. The board starts in ``mute``,
since a phone lying next to it would otherwise hear its own speaker through the
board's microphone. A short press of the user button (SW7 on the RT1170 EVK)
cycles the four modes in the order of the table; a press held longer than 0.7 s calls the phone's voice assistant instead, as a
headset button does, since the assistant's wake word cannot hear the board: in
``headset`` mode the assistant then listens to the board's microphone and its answer
plays on the board. A press held for 3 s ends the demo: the phone's client, which
otherwise reconnects whenever the board comes back, exits and restores the phone's
settings. Each action is shown for two seconds as a text band at the top of the
panel, drawn by the overlay element; while no phone streams, the panel says so. The
shell commands ``mode [mute|speaker|headset|intercom]``, ``assist`` and ``phone quit``
do the same from a terminal, and are the only controls on a board without a button:

============ ========================= ================================= ===============
Mode         Phone sends to the board  Board's microphone on the phone   LEDs
============ ========================= ================================= ===============
``mute``     its playback              muted                             none
``speaker``  its playback              played on the phone's speaker     green
``headset``  its playback              the phone's microphone, for calls red
``intercom`` its microphone            played on the phone's speaker     green and red
============ ========================= ================================= ===============

In ``headset`` the board behaves like a wired headset of the phone: the phone's
sound comes out of the board and the board's microphone replaces the phone's for
the applications that open it without choosing a device. The Camera and cellular
calls take it, the Google Recorder does not. The other party of a cellular call is
never heard on the board, since Android keeps call audio out of any playback
capture. In ``intercom`` the person at the board and the person holding the phone
talk both ways, and the phone's media is not heard on the board meanwhile. With a
loudspeaker on the board, the phone user's voice is picked up by the board's
microphone and comes back as an echo; headphones avoid it.

On the phone, ``speaker`` and ``intercom`` play the board's microphone through the
alarm volume, since the phone's own audio capture takes every media track for the
board; raise the alarm volume if it is quiet.

Headphones on the board's jack must be plain stereo (3-pole plug). A 4-pole headset
plug leaves the common return floating and the two channels cancel: voices vanish
while wide stereo music plays.

The client is the scrcpy 4.1 server of the demo repository (branch ``android-4.1``),
which adds a connection to a remote host, an MJPEG codec, a keep-alive message and
the microphone channel; its ``server/scripts/build_launch_server.sh`` builds it,
pushes it to the phone and launches it with the options this sample expects. The
liveness probe the sample sends every second is control message type 100, which
that server accepts and ignores.

Requirements
************

- A board with a display shield and a touch controller. This sample targets the
  :zephyr:board:`mimxrt1170_evk` with the ``rk055hdmipi4ma0`` MIPI display shield.
- Networking through either on-board Ethernet (default) or a supported Wi-Fi
  module (via the ``overlay-wifi.conf`` overlay).
- An Android phone running a compatible scrcpy-like client that streams MJPEG
  over TCP to port 5000 and consumes touch control events.

The sample can also be built and run on :zephyr:board:`native_sim` for host-side
development, where the display, touch and networking are provided by the host
(SDL window, SDL pointer and TAP Ethernet).

Building and Running
********************

Ethernet (default)
===================

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/android_link
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :shield: rk055hdmipi4ma0
   :goals: build flash
   :compact:

Wi-Fi
=====

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/android_link
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :shield: rk055hdmipi4ma0
   :gen-args: -DEXTRA_CONF_FILE=overlay-wifi.conf
   :goals: build flash
   :compact:

When using Wi-Fi, the board starts a soft-AP with SSID ``zephyr-link`` and
passphrase ``zephyrdemo`` and runs a DHCP server on ``192.0.2.1``. Connect the
phone to that network and point the client at ``192.0.2.1:5000``.

native_sim (host simulation)
============================

The sample can also be built and run on ``native_sim``, where the video path
runs entirely on the host. The MIPI panel is replaced by an SDL window, the
GT911 touch controller by the SDL pointer input, and the on-board Ethernet by
the host TAP networking driver.

First set up a host TAP interface (once per boot) using the ``net-setup.sh``
helper from the ``net-tools`` project (part of the Zephyr workspace, under
``tools/net-tools``). This creates the ``zeth`` interface with the host side at
``192.0.2.2`` and the board side at ``192.0.2.1``:

.. code-block:: console

   cd ../tools/net-tools
   sudo ./net-setup.sh --config zeth.conf

Leave that interface up while the sample runs; without it every outbound packet
from the simulated interface fails with ``send failure status -1``.

Then build and run:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/android_link
   :board: native_sim
   :goals: build run
   :compact:


An SDL window opens and displays the decoded frames. The application listens on
``192.0.2.1:5000``; point a compatible scrcpy-like MJPEG-over-TCP client at that
address (reachable through the host TAP interface). Mouse clicks and drags in
the SDL window are reported as Android touch events over the control socket.
