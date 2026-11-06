.. zephyr:code-sample:: mpipe-dmic-fft-i2s
   :name: Microphone to codec through an FFT on HiFi4 with mpipe
   :relevant-api: mpipe_aud mpipe_base

   Play the microphone signal on the audio codec after a gain, an FFT, an inverse FFT and a
   second gain, all computed on the HiFi4 DSP.

Overview
********

This sample builds an mpipe pipeline on the Cortex-M33 core 0 of the MIMXRT700-EVK:

.. code-block:: none

   dmic_src -> caps_filter -> queue -> gain -> fft -> ifft -> out_gain -> i2s_sink

- ``dmic_src`` captures 32-bit PCM from the left and right on-board digital microphones
  (MICFIL), the two channels interleaved, by default at 16 kHz in blocks of 16 ms (256 samples
  per channel).
- ``caps_filter`` fixes the PCM format to the one the HiFi4 functions take.
- ``queue`` moves the rest of the pipeline to a second thread so that capture never waits for
  the HiFi4 or the codec. It holds two blocks and drops the oldest one when they fall behind.
- ``gain`` scales the samples of both channels on the HiFi4, by 8 by default.
- ``fft`` turns each channel of a block into its complex spectrum on the HiFi4: a 256-point
  radix-2 float FFT, with no window and no overlap, giving the 129 bins from DC to Nyquist
  (``MPIPE_MEDIA_AUDIO_FFT``). The spectrum of the right channel follows that of the left one.
- ``ifft`` turns each spectrum back into PCM on the HiFi4: the left channel to the left side of
  a 32-bit stereo block, the right channel to the right side.
- ``out_gain`` scales the samples again on the HiFi4, by the same factor as ``gain``: a second
  instance of the same function.
- ``i2s_sink`` plays the blocks on the WM8962 codec over SAI0.

Nothing changes the spectrum between ``fft`` and ``ifft``, so the codec plays the microphone
signal amplified twice, by 64 in all by default, and delayed by the pipeline. The two stages are
where a frequency-domain process, such as a filter or a noise suppressor, would go.

All the audio computation runs on the HiFi4 DSP: ``gain``, ``fft``, ``ifft`` and ``out_gain`` are
mpipe audio transform clients, and the Cortex-M33 only moves the data.

HiFi4 server
============

The Cortex-M33 boots the HiFi4 image with ``dsp_start()``, then talks to the eRPC server of the
HiFi4 over RPMsg-Lite on MU4. Both sides are generated with ``erpcgen`` from
:zephyr_file:`subsys/mpipe/aud/erpc/mpipe_aud_transform.erpc`. The HiFi4 boot code and the image
embedding come from the RTxxx AMP samples
(:zephyr_file:`samples/boards/nxp/adsp/rtxxx/common`). The selection of the HiFi4 board, used to
build the Zephyr HiFi4 image, is in
:zephyr_file:`samples/subsys/mpipe/audio_multicore/dmic_fft_i2s/remote-dsp-board-select.cmake`.

The HiFi4 image is the audio server shared by the audio multicore samples
(:zephyr_file:`samples/subsys/mpipe/audio_multicore/aud_remote`). The ``sysbuild.cmake`` of the
sample adds it and enables the functions the pipeline opens: ``gain``, ``fft`` and ``ifft`` here
(``CONFIG_AUD_REMOTE_FUNC_*``). At init, each client element opens the function it runs and gets a
handle that every later call carries, so the single ``aud_process_fn_rpc`` call runs the right
function with the right state. ``gain`` and ``out_gain`` both open the gain function and get two
handles. The four stages take all four instances the server holds.

The server owns the buffers. Each function instance has four input buffers and four output
buffers of its own, so two stages never use the same buffer: 2112 bytes each, the size of the
two spectra of a block. After opening its function, each client asks for the addresses of the
buffers of its instance once, then for each block each stage:

#. copies its input into a free HiFi4 input buffer and flushes it from its cache,
#. sends its handle and the input and output buffer addresses in an eRPC request,
#. invalidates the output buffer once the HiFi4 has written into it.

The output of each stage thus comes back to the Cortex-M33 before the next one sends it to the
HiFi4 again: one block costs four eRPC round trips and four copies. The eRPC calls are
serialized: the HiFi4 runs one at a time. Only handles, addresses and formats travel in the eRPC
messages.

Each function is an adapter (``func_*.c``) between the server and its computation. The
transforms (``fft.c``) are plain math on plain memory, built into the HiFi4 image only.

The HiFi4 has no console: the LPUART of the board console is the shell of the Cortex-M33. The
server keeps its log lines in a buffer, and the Cortex-M33 reads them over eRPC after each
control call (``CONFIG_MPIPE_AUD_REMOTE_LOG``), then prints them in log module
``mpipe_aud_remote``. The processing calls are not followed by a read. Lines logged while the
HiFi4 boots wait in the buffer until the first client binds.

Playback
========

The I2S driver frees each block it has sent back to the memory slab of its stream, so it cannot
take the HiFi4 output buffers directly. ``i2s_sink`` copies each block into its own slab of six
blocks with ``i2s_buf_write()``, then releases the HiFi4 buffer at once. SAI0 is the clock
controller and drives MCLK to the codec. MICFIL and SAI0 are clocked from the same audio PLL, so
capture and playback run at the same rate.

Configuration
*************

The audio format and the gain are sysbuild options, set once for both images so that the
Cortex-M33 and the HiFi4 always agree: the Cortex-M33 builds them in as ``DMIC_FFT_I2S_<name>``
(``dmic_fft_i2s.cmake``), and ``sysbuild.cmake`` sets the matching ``CONFIG_AUD_REMOTE_*``
options of the HiFi4 image:

- ``SB_CONFIG_DMIC_FFT_I2S_SAMPLE_RATE``: capture and playback rate in Hz (default 16000, the
  only rate the MICFIL driver captures at).
- ``SB_CONFIG_DMIC_FFT_I2S_FRAME_INTERVAL_US``: duration of one block in microseconds (default
  16000). The samples per block are the FFT length and must be a power of two.
- ``SB_CONFIG_DMIC_FFT_I2S_GAIN_PERCENT``: gain of ``gain`` and of ``out_gain``, in percent
  (default 800). The signal is scaled by this factor twice.

The defaults are in ``sysbuild.conf``; a value given on the command line, such as
``-DSB_CONFIG_DMIC_FFT_I2S_GAIN_PERCENT=400``, takes precedence. The sample must be built with
``--sysbuild``.

The HiFi4 image is chosen with the ``DMIC_FFT_I2S_REMOTE`` choice. Either Zephyr builds the image
for the HiFi4, or it embeds binaries compiled elsewhere; it never runs a DSP compiler of its own:

- ``SB_CONFIG_DMIC_FFT_I2S_REMOTE_PREBUILT`` (default): embed a HiFi4 server compiled outside of
  Zephyr. ``SB_CONFIG_DMIC_FFT_I2S_REMOTE_BIN_DIR`` must be set to a directory with the three raw
  section images below; sysbuild stops with an error when it is empty, when a file is missing or
  when a file does not fit its region:

  - ``aud_dsp_server.reset.bin``: reset vector, loaded at ``0x24020000`` (ITCM), 1 KiB at most.
  - ``aud_dsp_server.text.bin``: code, loaded at ``0x20200000``, 1 MiB at most.
  - ``aud_dsp_server.data.bin``: data, loaded at ``0x20300000``, 1 MiB at most.

  Nothing else of the directory is read, and sysbuild does not check what the binaries contain.
  The server must be the eRPC server of
  :zephyr_file:`subsys/mpipe/aud/erpc/mpipe_aud_transform.erpc` (interfaces ``MpipeAudTransform``
  and ``MpipeAudLog``), generated from the same file as the client of this build. It must serve the
  ``gain``, ``fft`` and ``ifft`` functions, with four instances at least. It must be the RPMsg-Lite
  remote of the 16 KiB shared memory at ``0x20584000`` over MU4, with 1008 bytes of payload per
  message. Its sample rate, block duration and gain must be the ones of the three
  ``SB_CONFIG_DMIC_FFT_I2S_*`` options above.
- ``SB_CONFIG_DMIC_FFT_I2S_REMOTE_ZEPHYR``: build
  :zephyr_file:`samples/subsys/mpipe/audio_multicore/aud_remote` as sysbuild image ``remote``.

The codec output volume is set by ``CONFIG_MPIPE_AUD_I2S_CODEC_SINK_OUTPUT_VOLUME``, about
-15 dB by default.

Requirements
************

- MIMXRT700-EVK, with headphones on the audio jack. On a speaker close to the microphones, the
  loop may howl: lower the gain or the codec volume. A loud sound clips: each gain clamps to the
  32-bit range.
- ``erpcgen`` in ``PATH``.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/audio_multicore/dmic_fft_i2s
   :board: mimxrt700_evk/mimxrt798s/cm33_cpu0
   :west-args: --sysbuild
   :goals: build flash
   :compact:

To embed precompiled binaries from a given directory, or the Zephyr HiFi4 image instead:

.. code-block:: console

   west build -b mimxrt700_evk/mimxrt798s/cm33_cpu0 --sysbuild \
      samples/subsys/mpipe/audio_multicore/dmic_fft_i2s -- \
      -DSB_CONFIG_DMIC_FFT_I2S_REMOTE_BIN_DIR=\"<directory of the three binaries>\"

   west build -b mimxrt700_evk/mimxrt798s/cm33_cpu0 --sysbuild \
      samples/subsys/mpipe/audio_multicore/dmic_fft_i2s -- \
      -DSB_CONFIG_DMIC_FFT_I2S_REMOTE_ZEPHYR=y

Sample Output
=============

.. code-block:: console

   <inf> mpipe_aud_remote: [DSP] Audio server on mimxrt700_evk/mimxrt798s/hifi4
   <inf> mpipe_aud_remote: [DSP] Opened gain as handle 1
   <inf> main: HiFi4 ready
   <inf> mpipe_aud_remote: [DSP] Opened fft as handle 2
   <inf> mpipe_aud_remote: [DSP] Opened ifft as handle 3
   <inf> mpipe_aud_remote: [DSP] Opened gain as handle 4
   <inf> main: Gain, FFT, IFFT and gain computed on HiFi4
   <inf> main: Loopback started
   <inf> mpipe_aud_dmic_src: Capture started now
   <inf> mpipe_player: Player #0 state: PLAYING

Speak into the microphones: the voice comes out of the headphones.
