.. zephyr:code-sample:: mpipe_android_link
   :name: mpipe Android link
   :relevant-api: mpipe

   Mirror and control an Android phone, with its audio both ways, on the mpipe media pipeline
   framework.

Overview
********

.. admonition:: Demo video

   Watch the demo in action: https://www.youtube.com/watch?v=dPgdsLoCbCg

This sample links an Android phone to a board running Zephyr. The phone's screen shows on
the board's panel and the board's touch panel controls the phone. The phone's sound plays on
the board, the board's microphone can play on the phone's speaker or become the phone's
microphone, so that the board works as a wired headset for the phone's calls and voice
assistant, or as an intercom between the person at the board and the person holding the
phone. The phone connects over the board's own Wi-Fi access point or over Ethernet.

The demo has two parts:

- this sample, on the board, which owns a TCP listening port and runs three mpipe pipelines;
- a prebuilt application on the phone, ``scrcpy-server``, derived from scrcpy. It captures
  the screen, encodes it as MJPEG, captures the phone's playback or microphone, plays or
  injects the board's microphone, and executes the touch events. It runs as a shell process
  over ``adb``: nothing is installed on the phone. It ships with the sample as
  :file:`android_app/scrcpy-server` and :file:`scripts/launch_android_app.sh` pushes and
  launches it.

The sample originates from the `i.MX RT1170 Android phone screen mirroring demo`_ of the NXP
Application Code Hub (`video of the original demo`_), rebuilt on mpipe with the audio both
ways, the modes, the Wi-Fi access point and many fixes and improvements.

.. _i.MX RT1170 Android phone screen mirroring demo:
   https://github.com/nxp-appcodehub/dm-i-mx-rt1170-android-phone-screen-mirroring
.. _video of the original demo: https://www.youtube.com/watch?v=c6CALZsSDW8

The pipelines
=============

The video path:

.. code-block:: none

   socket_src -> jpeg_parser -> caps_filter -> queue -> jpeg_decoder -> queue -> overlay
              -> disp_sink

* ``socket_src`` (mpipe ``net`` plugin) reads the byte stream from the video connection the
  application accepted and handed to it, into large chunks, and pushes each read as a view of
  its chunk.
* ``jpeg_parser`` (mpipe ``img`` plugin) cuts complete JPEG frames out of the stream at the
  end-of-image markers. A frame that lies inside one chunk reaches the decoder as a view of
  that chunk, without a copy.
* ``caps_filter`` (mpipe ``base`` plugin) pins the frame size the phone sends,
  ``CONFIG_ANDROID_LINK_FRAME_WIDTH`` by ``CONFIG_ANDROID_LINK_FRAME_HEIGHT``, which default to
  the panel's size; the decoder and the display negotiate the pixel format from there.
* The two ``queue`` elements (mpipe ``base`` plugin) put the receiving, decoding and
  displaying on three threads. Each holds one frame and drops its oldest one when the stage
  behind it falls behind, so the panel always shows the newest frame and the phone is never
  throttled into buffering frames on its side.
* ``jpeg_decoder`` (mpipe ``img`` plugin) decodes each frame in software. A frame that does
  not decode is dropped.
* ``overlay`` (mpipe ``vid`` plugin) draws the demo's messages as a band at the top of the
  frames for a moment.
* ``disp_sink`` (mpipe ``disp`` plugin) writes the frames to the display device.

With ``CONFIG_ANDROID_LINK_AUDIO`` (the default) a second pipeline plays the audio the phone
sends on a connection of its own, as raw PCM, 48 kHz, 16-bit, stereo:

.. code-block:: none

   socket_src -> aud_raw_parser -> queue -> i2s_codec_sink

* ``aud_raw_parser`` (mpipe ``aud`` plugin) announces the format, which a raw stream cannot,
  and cuts the bytes into 10 ms blocks.
* The ``queue`` holds 150 ms of blocks against the jitter of the link and runs the sink on
  its own thread, above the video decoder in priority.
* ``i2s_codec_sink`` (mpipe ``aud`` plugin) plays the blocks through the board's codec.

With ``CONFIG_ANDROID_LINK_MIC`` (the default where a digital microphone exists) a third
pipeline sends the board's microphone to the phone as raw PCM, 16 kHz, 32-bit, two channels
(both slots of the PDM data line, summed by the phone), on a fourth connection:

.. code-block:: none

   dmic_src -> caps_filter -> aud_gain -> queue -> socket_sink

* ``dmic_src`` (mpipe ``aud`` plugin) captures 10 ms blocks from the digital microphone.
* ``caps_filter`` pins the format the phone expects out of what the microphone offers.
* ``aud_gain`` (mpipe ``aud`` plugin) lifts the microphone's soft signal by
  ``CONFIG_ANDROID_LINK_MIC_GAIN`` percent (200, +6 dB, by default) in place.
* The ``queue`` keeps a slow link from holding the capture, dropping the oldest block.
* ``socket_sink`` (mpipe ``net`` plugin) writes the blocks to the connection.

The streams are not synchronized with each other, as in the phone's own desktop client: each
runs at the lowest latency it can, which keeps video and audio within about a tenth of a
second of each other. The phone's volume is applied before capture, so it is heard on the
board, and the phone itself stays silent while mirroring.

The pipelines are driven by the mpipe player, one per pipeline. The application sets up the
network (Wi-Fi access point, or Ethernet with a DHCP server), listens on
``CONFIG_ANDROID_LINK_PORT``, accepts the phone's video connection, then its audio connection,
then its control connection, then its microphone connection, hands the stream sockets to the
pipelines and keeps the control one for the touch events and the demo's messages. When the
phone leaves it accepts the next client.

The modes
=========

The streams flow whenever the phone is connected; what each side does with them is the demo
mode. The phone's audio connection carries one source at a time, its playback or its
microphone, and the board's microphone has one destination at a time on the phone:

============ ========================= ================================= ===============
Mode         Phone sends to the board  Board's microphone on the phone   LEDs
============ ========================= ================================= ===============
``mute``     its playback              muted                             none
``speaker``  its playback              played on the phone's speaker     green
``headset``  its playback              the phone's microphone, for calls red
``intercom`` its microphone            played on the phone's speaker     green and red
============ ========================= ================================= ===============

The screen and the touch work the same in every mode. What each mode is for:

``mute``
   A mirror with sound: the phone's screen, touch and sound on the board, the board's
   microphone off. This is a head unit or a kiosk showing the phone's applications, and the
   start mode, since a phone lying next to the board would otherwise hear its own speaker
   through the board's microphone.

``speaker``
   The mirror, plus the board's microphone played on the phone's speaker: the phone holder
   hears what happens around the board, as a baby monitor or a room monitor does, with the
   phone's sound still on the board.

``headset``
   The board is a wired headset of the phone: the phone's sound comes out of the board and the
   board's microphone replaces the phone's for the applications that open it without choosing
   a device. Hands-free calls from across the room, a conference speaker, the voice assistant
   from the board (a long press of the button calls it): cellular calls, VoIP calls
   (Messenger, WhatsApp) and the assistant take the microphone, the Google Recorder does not.
   The other party of a VoIP call is heard on the board like any other sound; the other party
   of a cellular call never is, since Android keeps call audio out of any playback capture.

``intercom``
   The phone's microphone on the board and the board's microphone on the phone's speaker: the
   person at the board and the person holding the phone talk both ways, as an entry phone or
   a door bell does, with the board at the door and the phone with its owner. The phone's
   playback is not sent meanwhile, since the audio connection carries the microphone instead,
   so the phone's media is not heard on the board in this mode.

The user button (SW7 on the RT1170 EVK) drives the demo:

- a short press cycles the four modes in the order of the table;
- a press held longer than 0.7 s calls the phone's voice assistant, as a headset button does,
  since the assistant's wake word cannot hear the board. In ``headset`` the assistant then
  listens to the board's microphone and its answer plays on the board;
- a press held for 3 s ends the demo: the phone's application, which otherwise reconnects whenever
  the board comes back, exits and restores the phone's settings.

Each action is shown for two seconds as a text band at the top of the panel; while no phone
streams, the panel says so. The shell commands do the same from a terminal, and are the only
controls on a board without a button:

.. code-block:: console

   uart:~$ mode headset
   uart:~$ assist
   uart:~$ phone quit

The board sends the mode to the phone as two control messages when the control connection
comes up and whenever it changes: type 101, the microphone mode (``0`` dropped, ``1`` played on
the speaker, ``2`` injected as the phone's microphone), and type 102, the audio source (``0``
the phone's playback, ``1`` its microphone with the phone's echo cancelling). The liveness
probe the board sends every 20 s while the control connection is idle is type 100, which the
application ignores.

Requirements
************

Hardware
========

- A board with a display, a touch panel and a network interface. The sample is developed on
  the :zephyr:board:`mimxrt1170_evk` (MIMXRT1170-EVKB, rev B) with the
  :ref:`rk055hdmipi4ma0 <rk055hdmipi4ma0>` 5.5" MIPI display shield; the audio uses the
  EVKB's WM8962 codec, headphone jack and digital microphone.
- One of the two links:

  - Wi-Fi: the IW612-MURATA-2EL-M.2 module in the board's M.2 slot. The board is the access
    point; the phone needs nothing else.
  - Ethernet: an RJ45 cable and a USB-C to Ethernet adapter for the phone (the BENFEI and
    UGREEN Revodok 7-in-1 adapters are known to work), or a switch between the phone's
    adapter and the board. The link needs about 11 Mbit/s, so 100 Mbit/s Ethernet is enough.

- An Android phone running Android 11 or later with USB debugging enabled. Google Pixel
  phones are the reference; some Samsung builds lose their internet access while the demo
  holds the board's network.
- A USB cable from the phone to the host for ``adb``, and a micro-USB cable from the board's
  debug port to the host for the console.
- Plain stereo headphones (3-pole plug) on the board's jack. A 4-pole headset plug leaves
  the common return floating and the two channels cancel: voices vanish while wide stereo
  music plays.

Software
========

- Zephyr and its SDK (see :ref:`getting_started`), with the ``jpegdec`` module of the
  workspace.
- For Wi-Fi, the NXP Wi-Fi firmware: ``west blobs fetch hal_nxp``.
- ``adb`` on the host: the Android SDK platform tools, which Android Studio installs
  (``~/Android/Sdk/platform-tools``), or the ``android-tools`` package of the distribution.
- The prebuilt phone application, :file:`android_app/scrcpy-server` in the sample.

Hardware setup
**************

On the MIMXRT1170-EVKB:

- Plug the RK055HDMIPI4MA0 panel into the MIPI DSI connector J48.
- Wi-Fi: insert the IW612 module into the M.2 slot J23. Ethernet: connect the cable to the
  1G Ethernet port (the board's default interface) and the other end to the phone's adapter.
- Power the board through J43 and connect the debug micro-USB J86 to the host: it carries the
  console (115200 baud) and the LinkServer debug probe.
- Headphones on the audio jack; the digital microphone is on the board.

Building and running
********************

Build for Ethernet (the default):

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/android_link
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :shield: rk055hdmipi4ma0
   :goals: build flash
   :compact:

or for Wi-Fi, with the overlay that brings in the IW612 driver and the soft-AP:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/android_link
   :board: mimxrt1170_evk/mimxrt1176/cm7
   :shield: rk055hdmipi4ma0
   :gen-args: -DEXTRA_CONF_FILE=overlay-wifi.conf
   :goals: build flash
   :compact:

``west flash`` uses LinkServer by default; ``west flash --runner jlink`` uses an external
J-Link. After flashing, power-cycle the board: the Wi-Fi module does not come up after a
reset through the debugger, and the access point with it.

These builds log only the session events. To measure, add ``overlay-stats.conf``
(``-DEXTRA_CONF_FILE="overlay-wifi.conf;overlay-stats.conf"``): it turns on the per-second
statistics of each stage (``CONFIG_MPIPE_STAGE_STATS``: receive rate, frame sizes, decode and
display times), the kernel and network shells and the thread accounting, at the cost of a log
line per second per stage. Leave it out for a demonstration.

The sample's options, set in Kconfig or on the build line, and what the phone's application must
be launched with to match (see `Launching the phone's application`_):

.. list-table::
   :header-rows: 1

   * - Option
     - Default
     - On the phone
   * - ``CONFIG_ANDROID_LINK_PORT``
     - ``5000``
     - ``REMOTE_PORT``
   * - ``CONFIG_ANDROID_LINK_WIFI_SSID``, ``CONFIG_ANDROID_LINK_WIFI_PSK`` (Wi-Fi builds)
     - ``zephyr-link``, ``zephyrdemo``
     - ``WIFI_SSID``, ``WIFI_PSK``; an empty passphrase makes an open network
   * - ``CONFIG_ANDROID_LINK_FRAME_WIDTH``, ``CONFIG_ANDROID_LINK_FRAME_HEIGHT``
     - the panel's size
     - ``WIDTH``, ``HEIGHT``
   * - ``CONFIG_ANDROID_LINK_AUDIO``
     - on
     - ``AUDIO``
   * - ``CONFIG_ANDROID_LINK_MIC``
     - on where a digital microphone exists
     - ``MIC``
   * - ``CONFIG_NET_CONFIG_MY_IPV4_ADDR`` (Ethernet), ``CONFIG_NXP_WIFI_SOFTAP_IP_ADDRESS``
       (Wi-Fi, in ``overlay-wifi.conf``)
     - ``192.0.2.1``
     - ``REMOTE_HOST``

For example, a board on another network and port:

.. code-block:: console

   west build -p -b mimxrt1170_evk/mimxrt1176/cm7 --shield rk055hdmipi4ma0 \
       samples/subsys/mpipe/android_link -- -DEXTRA_CONF_FILE=overlay-wifi.conf \
       -DCONFIG_ANDROID_LINK_WIFI_SSID=\"other-ap\" -DCONFIG_ANDROID_LINK_WIFI_PSK=\"otherpass\" \
       -DCONFIG_ANDROID_LINK_PORT=5001

Launching the phone's application
=================================

Connect the phone over USB with USB debugging enabled, authorize the host when the phone
asks, and run:

.. code-block:: console

   samples/subsys/mpipe/android_link/scripts/launch_android_app.sh

The script stops an instance still running on the phone, pushes the file and launches it against
the board with every default below. On Wi-Fi the application then joins the board's network by
itself and connects; after a board reset or a Wi-Fi drop it reconnects on its own, so a
relaunch is only needed after a phone reboot, after the demo was ended from the board, or to
install a new version. Variables override the defaults; each must match the board's build:

.. list-table::
   :header-rows: 1

   * - Variable
     - Default
     - Must match
   * - ``SERVER_JAR``
     - the sample's :file:`android_app/scrcpy-server`
     - the prebuilt application
   * - ``REMOTE_HOST``
     - ``192.0.2.1``
     - the board's address
   * - ``REMOTE_PORT``
     - ``5000``
     - ``CONFIG_ANDROID_LINK_PORT``
   * - ``WIDTH``, ``HEIGHT``
     - ``720``, ``1280``
     - ``CONFIG_ANDROID_LINK_FRAME_WIDTH``, ``CONFIG_ANDROID_LINK_FRAME_HEIGHT``
   * - ``AUDIO``
     - ``true``
     - ``CONFIG_ANDROID_LINK_AUDIO``
   * - ``MIC``
     - ``true``
     - ``CONFIG_ANDROID_LINK_MIC``
   * - ``AUDIO_SOURCE``
     - ``playback``
     - the source to start with; the board's modes switch it at run time
   * - ``WIFI_SSID``, ``WIFI_PSK``
     - ``zephyr-link``, ``zephyrdemo``
     - ``CONFIG_ANDROID_LINK_WIFI_SSID``, ``CONFIG_ANDROID_LINK_WIFI_PSK``; set ``WIFI_SSID``
       empty on Ethernet, so the application joins no network
   * - ``MAX_FPS``
     - ``15``
     - the sender's frame rate cap
   * - ``JPEG_QUALITY``
     - ``70``
     - the frames' JPEG quality; a lighter frame decodes faster
   * - ``KEEP_SAVED_WIFI``
     - ``false``
     - ``true`` keeps saved entries of the board's network on the phone, which Android would
       otherwise join as an ordinary network and lose its internet
   * - ``ADB``
     - ``adb`` in the path, else the SDK's
     - the ``adb`` to use

For example, on Ethernet, or for a board built without the microphone:

.. code-block:: console

   WIFI_SSID= scripts/launch_android_app.sh
   MIC=false scripts/launch_android_app.sh

Using the demo
==============

On Wi-Fi the board starts an access point with the configured name and passphrase and runs a
DHCP server. The phone does not need to join that network by hand: the application asks Android
for it with a local-only network request and holds it for as long as it runs. Android shows
the network as "Connected via Shell", keeps mobile data as the default route so the phone
keeps its internet access, and needs no change of settings. After a drop (board reset, Wi-Fi
toggled) the application requests the network again and the session resumes by itself. If the
phone is on another Wi-Fi network at that moment, Android asks first with a "Connect to
device" notification; accepting it switches the phone to the board. On Ethernet the phone's
adapter gets its address from the board's DHCP server.

The ``Connection to /192.0.2.1:5000 failed ... retrying`` lines in the application's log
(``adb logcat -s scrcpy:*``) are normal while the phone waits for the board. Once connected,
the phone's screen shows on the panel within a second, with the phone's touch feedback
("show touches") on; the panel's touch controls the phone, several fingers at once.

The phone's volume keys set the level on the board: the media volume outside a call, the call
volume during one. ``speaker`` and ``intercom`` play the board's microphone on the phone
through the call volume, which the keys adjust while it plays. With the phone next to the
board in those modes, the board's microphone picks the phone's speaker up and the sound
feeds back; keep the phone away from the board, in another room for an intercom.

To end the demo, hold the button for 3 s or type ``phone quit``: the application exits, releases the
board's network and restores the phone's settings. A phone reboot or ``adb shell pkill -f
app_process`` does the same.

native_sim with GStreamer
=========================

The sample also runs on :zephyr:board:`native_sim`, where the panel is an SDL window, the
touch panel the SDL pointer, the network a TAP interface of the host, the codec a file and the
microphone an emulated one. Any sender of the same streams can then play the phone's part:
GStreamer's test sources do.

First set up the host's TAP interface, once per boot, with the ``net-setup.sh`` helper of the
``net-tools`` project (in the Zephyr workspace under ``tools/net-tools``). It creates ``zeth``
with the host side at ``192.0.2.2`` and the sample side at ``192.0.2.1``:

.. code-block:: console

   sudo ../tools/net-tools/net-setup.sh --config ../tools/net-tools/zeth.conf start

Then build and run (the 64-bit variant, since the host's SDL2 is 64-bit; ``native_sim.conf``
puts the shell on the terminal):

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/android_link
   :board: native_sim/native/64
   :gen-args: -DEXTRA_CONF_FILE=native_sim.conf
   :goals: build run
   :compact:

:file:`run_native_sim.sh` in the sample does the same, with options to run under gdb or with
the address sanitizer.

The sample accepts the phone's four connections in order: video, audio, control, microphone.
From another terminal, open them a second apart, all to ``192.0.2.1:5000``:

.. code-block:: console

   gst-launch-1.0 videotestsrc is-live=true pattern=ball \
       ! video/x-raw,width=720,height=1280,framerate=15/1 ! jpegenc quality=70 \
       ! tcpclientsink host=192.0.2.1 port=5000
   gst-launch-1.0 audiotestsrc is-live=true wave=sine freq=440 \
       ! audio/x-raw,rate=48000,channels=2,format=S16LE,layout=interleaved \
       ! tcpclientsink host=192.0.2.1 port=5000
   nc 192.0.2.1 5000 > control.bin
   nc 192.0.2.1 5000 > mic.pcm

The bouncing ball shows in the SDL window at about 15 fps, clicks and drags in it are sent as
touch events on the control connection (``control.bin`` receives the two mode messages, then
a keep-alive byte every 20 s while the connection is idle), the sine wave lands in
:file:`build/android_link_audio_tx.pcm` (192 KB/s of 48 kHz stereo; play it with
``aplay -f S16_LE -r 48000 -c 2``) and ``mic.pcm`` receives the emulated microphone, 128 KB/s.
``mode headset`` in the shell paints the band on the window and writes the messages to
``control.bin``; ``assist`` writes the two key messages. The video and the audio sources match
the frame size and the formats the sample expects; change ``width`` and ``height`` with
``CONFIG_ANDROID_LINK_FRAME_WIDTH`` and ``CONFIG_ANDROID_LINK_FRAME_HEIGHT``. Under heavy load
native_sim's clock lags the host, so a real-time sender delivers more than a second of audio
per simulated second: the surplus is dropped, which is a simulation artefact.

The same senders drive a board over Ethernet: point them at the board's address from a host
on its network.

Porting to another board
************************

The sample takes its devices from the devicetree and its sizes from Kconfig; a board needs:

- a ``zephyr,display`` chosen node with ``width`` and ``height`` properties, driven in
  RGB565 (``display_set_pixel_format``); the frame size defaults to the panel's;
- a ``zephyr,touch`` chosen node for an input driver reporting touch points (the GT911 on
  the shield, the SDL pointer on native_sim); without one the mirror is not interactive;
- for the audio, an ``i2s-codec-tx`` alias to the I2S output and an audio codec node
  (``CONFIG_ANDROID_LINK_AUDIO`` depends on ``CONFIG_MPIPE_AUD``);
- for the microphone, a ``dmic_dev`` node label on a DMIC controller
  (``CONFIG_ANDROID_LINK_MIC`` depends on ``CONFIG_AUDIO_DMIC``);
- optionally ``sw0`` for the button and ``led0`` (green) and ``led1`` (red) for the mode LEDs;
  a board without them uses the shell commands;
- a network interface: Ethernet works as is, with the address in ``prj.conf``; a Wi-Fi build
  needs the driver's options and the soft-AP's address in an overlay like
  :file:`overlay-wifi.conf`, which is written for the IW612 on the EVKB.

:file:`boards/native_sim_native_64.overlay` and
:file:`boards/mimxrt1170_evk_mimxrt1176_cm7.overlay` are the two examples. The RT1170 build
also relocates the networking stack into ITCM (``CONFIG_NET_STACK_CODE_RELOCATE``) and sets
the SDRAM up through the DCD, in :file:`boards/mimxrt1170_evk_mimxrt1176_cm7.conf`; neither
is required elsewhere.

Questions and answers
*********************

**How long does the connection take?**
   A few seconds after the application is launched or the board comes back: the phone joins the
   network, gets an address and opens the four connections.

**The phone does not connect.**
   On Wi-Fi, check that the access point is up (``wifi ap stations`` in the board's shell, or
   the ``Turning on AP Mode`` line in the console); after a flash, power-cycle the board. On
   the phone, ``adb logcat -s scrcpy:*`` shows what the application is doing. If the phone was on
   another Wi-Fi network, accept the "Connect to device" notification. A saved entry for the
   board's network makes Android join it as an ordinary network and lose its internet; the
   launch script forgets such entries, unless ``KEEP_SAVED_WIFI=true``.

**Why does the phone's part run through adb?**
   Injecting touch events and capturing the screen and the audio need the shell's privileges,
   which no installed application has. scrcpy works the same way.

**The connection dropped.**
   The application reconnects by itself after a board reset or a Wi-Fi drop; the board accepts the
   next client once the previous one is gone (a Wi-Fi drop is noticed within a few seconds by
   the TCP keep-alive). A relaunch is only needed after a phone reboot or after the demo was
   ended from the board.

**How do I end the demo?**
   Hold the button for 3 s, type ``phone quit``, or reboot the phone. The application restores the
   phone's settings on exit.

**Can Wi-Fi and Ethernet be used at the same time?**
   No. The build selects one; Ethernet is the default, Wi-Fi comes with
   :file:`overlay-wifi.conf`.

**The phone lost its internet while connected.**
   The application holds the board's network with a local-only request, so mobile data stays the
   default route. Some phone builds (Samsung) still route through the board; there is no
   known workaround on the board side.

**The other party of a call is not heard on the board.**
   Cellular calls are kept out of any playback capture by Android; VoIP calls are heard. In
   ``headset`` mode the call volume applies, and Android keeps one call volume per output:
   if it is turned down for the output the call uses, press volume up during the call.

**I hear my own voice back, or a whistle, in the speaker modes.**
   The board's microphone picks the phone's speaker up. Move the phone away from the board;
   an intercom works from another room.

**The voices vanish in the headphones while music plays.**
   A 4-pole headset plug on the stereo jack: use plain 3-pole headphones.

**Does the sample scale the screen?**
   No. The phone scales its screen to the frame size the sample is built for, the panel's by
   default, and keeps the natural (portrait) orientation whatever the phone shows.

Expected console output
***********************

.. code-block:: console

   [00:00:00.512,000] <inf> display: Display device: display@...
   [00:00:02.028,000] <inf> main: Turning on AP Mode
   [00:00:02.912,000] <inf> main: Video pipeline linked.
   [00:00:02.913,000] <inf> main: Audio pipeline linked.
   [00:00:02.914,000] <inf> main: Microphone pipeline linked.
   [00:00:02.915,000] <inf> main: Waiting for a client on port 5000
   ...
   [00:00:21.330,000] <inf> touch: TCP control socket is connected

The video shows on the panel from the first frame received; with ``overlay-stats.conf`` each
stage then logs its statistics once a second.
