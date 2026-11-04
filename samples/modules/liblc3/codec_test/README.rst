.. SPDX-FileCopyrightText: Copyright 2026 NXP
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:code-sample:: lc3-codec-test
   :name: LC3 USB codec test
   :relevant-api: usbd_api uart_interface

   Exercise liblc3 encoding, decoding and PLC over USB CDC.

Scope
*****

This application ports the binary protocol of the MCUXpresso SDK internal
``edgefast_open_examples/lc3_test`` example to Zephyr. The supported target
is 1170EVKB with 2EL/NW612, using ``mimxrt1170_evk@B/mimxrt1176/cm7``.
The codec runs in software on CM7. Bluetooth and NW612 initialization are
not enabled; the module is not used by this USB test. LE Audio air transport
is outside this application's scope.

The default configuration enables standard LC3 only. ``lc3plus.conf``
explicitly enables liblc3's experimental LC3plus and HR modes. LTPF disable
is unsupported in both configurations. The liblc3 module is unmodified.

Build
*****

Use the configured Zephyr toolchain and Python environment. From the west
workspace root, set ``ZEPHYR_BASE`` to the checkout containing this sample.
In an isolated worktree, also pass ``-z`` so west uses its extensions.
For example, in PowerShell::

   $env:ZEPHYR_BASE = 'C:\_DDM\zephyrproject\.mcu-harness\worktrees\lc3-qualification-main-20261104\zephyr'
   $app = "$env:ZEPHYR_BASE/samples/modules/liblc3/codec_test"
   west -z $env:ZEPHYR_BASE build -b mimxrt1170_evk@B/mimxrt1176/cm7 $app -d C:/tmp/lc3-standard

To explicitly enable experimental modes, use a separate directory::

   west -z $env:ZEPHYR_BASE build -b mimxrt1170_evk@B/mimxrt1176/cm7 $app -d C:/tmp/lc3-plus-hr -- -DEXTRA_CONF_FILE=lc3plus.conf

These builds use the workspace's installed modules. Verify the liblc3
revision against the chosen Zephyr manifest. Development baselines were
Zephyr ``f19d5c93e18975a17b702ac22726392b8ab453e6`` (4.5.99) and
liblc3 ``48bbd3eacd36e99a57317a0a4867002e0b09e183``.

USB and Console
***************

Connect the board's USB1 device port, not just its debug probe USB port, to
the PC. Select the enumerated ``LC3 codec test`` CDC port for binary traffic.
UART1 remains the debug console. Do not redirect logs or shell traffic to
the binary CDC port. The host must assert DTR.

The main thread parses and processes commands. The CDC UART callback only
moves bytes. RX applies backpressure when its ring buffer is full; TX
retains bytes not accepted by ``uart_fifo_fill``. A response unable to make
progress for 30 seconds aborts the connection; close and reopen the port
before retrying. DTR changes and USB reset clear incomplete messages and
transport queues. Codec configuration and counters survive reconnects;
send RESET to clear them. This accommodates the original tools opening
separate connections for different commands.

Protocol
********

All packets have a one-byte type, a four-byte little-endian payload length,
then the payload. The maximum payload is 49152 bytes. Success responses
use ``request_type | 0x80``. Errors use ``0xff`` with a signed 32-bit
little-endian error code and diagnostic text. Unsupported modes and
LTPF-disable requests return ``-ENOTSUP`` (``-95`` on Zephyr); size-limit
errors return ``-ENOSPC`` (``-28``).

.. list-table:: Commands
   :header-rows: 1

   * - Type
     - Command
     - Payload / response
   * - 0x01
     - Encoder setup
     - dt_us, codec rate, PCM rate, bitrate: 4 x int32;
       optional hrmode, format, disable_ltpf: 3 x uint8.
       Response: PCM samples and coded bytes per frame: 2 x int32.
   * - 0x02
     - Encode frame
     - Exactly one PCM frame / one coded frame.
   * - 0x03
     - Encode batch
     - uint32 count followed by exactly count PCM frames / coded frames.
   * - 0x10
     - Decoder setup
     - dt_us, codec rate, PCM rate: 3 x int32;
       optional hrmode, format: 2 x uint8.
       Response: PCM samples per frame: int32.
   * - 0x11
     - Decode frame
     - One coded frame / one PCM frame.
   * - 0x12
     - Decode batch
     - uint32 count and equally sized coded frames / PCM frames.
   * - 0x13
     - PLC
     - Empty (one frame), or uint32 count / PCM frames.
   * - 0x20
     - Info
     - Empty / capabilities, configuration and counters as text.
   * - 0x21
     - Reset
     - Empty / empty.
   * - 0x22
     - Query
     - uint8 hrmode, then dt_us, codec rate, PCM rate, bitrate,
       channel count, coded bytes: 6 x int32 / 7 x int32 helper results.

Batch counts must be 1 through 100; request and response must fit the
payload limit. Unknown commands and malformed lengths are rejected.
After rejecting an oversized header, the parser discards its declared
payload before accepting another header. If that payload was not sent,
close and reopen the port to reset framing: the protocol has no sync marker.

PCM formats are S16 (0, two bytes), S24 (1, sign-extended 24-bit samples in
little-endian int32 containers), S24_3LE (2, three packed little-endian
bytes), and FLOAT (3, four-byte float samples). Setup responses and PCM
lengths use the actual PCM rate, including supported oversampling. A zero
PCM rate uses the codec rate. Query retains the SDK stateless-helper
contract: sample counts, delay and context sizes use the codec rate;
the PCM-rate field is reserved. Query channel counts are limited to
0 through 8; codec processing is mono.

PC Regression
*************

Reuse the original, unmodified tools from::

   C:\_DDM\sdk-next\mcuxsdk\examples_int\edgefast_open_examples\lc3_test\tools

Use a Python environment with ``pyserial``. From that directory, replace
``COMx`` with the binary CDC port::

   python lc3_cli.py info --port COMx
   python test_all.py --port COMx --only std --phases combo plc info plc_cli plc_fill resample negative --verify

For experimental firmware, the same phases may use ``--only all`` and
include ``query``. Do not select ``ltpf`` on either firmware. Do not use
the default ``--phases all``: it includes LTPF-disable tests. On default
firmware, ``--only std`` does not filter the query phase's hardcoded HR
requests; omit ``query`` or check standard queries through
``Lc3CdcClient.query`` explicitly.

The current SDK CLI maps 44100 Hz file metadata to a 48000 Hz codec;
firmware setup does not accept 44100 Hz directly. The scripts exercise
sine waves, lengths and optional SNR thresholds. They are not an official
qualification runner or normative Bluetooth evidence. Formal qualification
requires engineer-selected tools/versions, vectors, acceptance criteria
and evidence. The module's ``-ffast-math`` flags are unchanged and have
not been certified by this port.

Host Tests
**********

On Linux, set ``APP`` to this directory and ``LIBLC3`` to the unmodified
west liblc3 checkout. No board or USB connection is needed::

   cmake -S "$APP" -B /tmp/lc3-host -G Ninja -DLC3_TEST_HOST=ON -DLIBLC3_DIR="$LIBLC3" -DCMAKE_BUILD_TYPE=Debug
   cmake --build /tmp/lc3-host
   ctest --test-dir /tmp/lc3-host --output-on-failure

Use a separate directory with ``-DLC3_TEST_HOST_EXTENSIONS=ON`` to test
optional modes. Assertions remain active in release builds. Coverage
includes PCM formats, resampled lengths, PLC, mode rejection, fragmented
and coalesced packets, batches, invalid lengths, unknown commands and
oversized-payload recovery. The test executable's ``--stdio`` option
exposes the binary protocol through stdin/stdout for client tests;
it is not firmware or USB emulation.

An initial UBSan run reported signed left-shift overflow in unmodified
liblc3 ``src/fastmath.h``. ASan/UBSan checks were rerun with only shift
checking disabled. This is a validation limitation, not a codec fix.
Host tests and successful builds do not establish USB runtime or formal
qualification success.
