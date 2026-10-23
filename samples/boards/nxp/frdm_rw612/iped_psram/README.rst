.. zephyr:code-sample:: iped_psram_repro
   :name: IPED / pSRAM corruption repro (psram-iped.md Issue 2)

   Reproduce CPU accesses to pSRAM outside any IPED region being corrupted
   while IPED is active on a different FlexSPI port.

Overview
********

Minimal reproduction for Issue 2 in ``psram-iped.md``: with an IPED GCM
region provisioned on FlexSPI **A1** (``ext_flash``), a plain 32-bit
store/load round trip to the aps6404l pSRAM on FlexSPI **B1** does not come
back unchanged - even though the pSRAM address (``0x1c000000``, alias
``0x0c000000`` in the IPED region-register address space) is far above any
configured ``IPEDCTX*START/END`` region.

This app does not touch ``ext_flash`` at all. It only needs *some* IPED
region provisioned and enabled so ``IPEDCTRL.IPED_EN`` /
``AHBRD_EN`` / ``AHBGCMRD`` are left set the way BootROM/blhost leaves them
after provisioning - the same state MCUboot hands off to the application in
the customer's report.

Requirements
************

* FRDM-RW612 board (onboard aps6404l pSRAM on FlexSPI B1).
* An IPED region provisioned on ``ext_flash`` (FlexSPI A1). Reuse the
  provisioning script from the sibling sample - it only needs to run once:

  .. code-block:: console

     python3 ../iped/scripts/provision_and_test.py --board frdm_rw612 --provision-only

  This leaves ``IPEDCTRL`` with ``IPED_EN`` and the AHB read-side bits set,
  matching the "as MCUboot leaves it" row in ``psram-iped.md`` Table 2.

Building and Running
*********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/nxp/frdm_rw612/iped_psram
   :board: frdm_rw612
   :goals: build flash
   :compact:

Expected log
*************

.. code-block:: console

   [00:00:00.010,000] <inf> iped_psram_repro: IPEDCTRL at boot: 0x0000019a
   [00:00:00.010,000] <inf> iped_psram_repro: pSRAM test address: 0x1c000000, wrote 0xa5a5a5a5
   [00:00:00.010,000] <inf> iped_psram_repro: [baseline (no bits cleared)] readback=0x00458b02 -> CORRUPT
   [00:00:00.010,000] <inf> iped_psram_repro: [AHBGCMRD cleared] readback=0xa4858a02 -> CORRUPT
   [00:00:00.010,000] <inf> iped_psram_repro: [AHBGCMRD + AHBRD_EN cleared] readback=0xa5a5a5a5 -> OK
   [00:00:00.010,000] <inf> iped_psram_repro: [IPED_EN also cleared (control)] readback=0xa5a5a5a5 -> OK

The bug reproduces if any row before ``IPED_EN also cleared`` reports
``CORRUPT`` while the pSRAM address is outside every configured IPED
region. A varying readback across repeated runs at the same ``IPEDCTRL``
value (rather than a fixed wrong value) is the same signature reported in
``psram-iped.md`` and is consistent with the read being address-translated
as if it were a GCM decrypt.

Notes
*****

* This app never writes to ``ext_flash``, so it cannot corrupt the
  provisioned IPED region or any flash image slot.
* ``AHBWR_EN`` / ``AHBGCMWR`` (write side) are intentionally left untouched
  here - ``psram-iped.md`` section 4 already confirmed those must be clear
  for ``sys_heap_init()``-style unaligned writes to pSRAM, which is a
  separate, already-understood constraint from the read-side corruption this
  sample isolates.
* If the field names in ``fsl_flexspi.h`` differ for the FlexSPI IPEDCTRL
  bit masks on the SDK version in use, adjust the ``FLEXSPI_IPEDCTRL_*_MASK``
  references in ``src/main.c`` accordingly - the masks are read from the
  installed MCUX HAL header, not defined by this sample.
