NXP VREF PM regression tests
###########################

This suite compiles the production driver against RAM-backed registers and runs
on ``qemu_cortex_m3``. Two independent devices exercise mode/trim replay, held
and unused outputs, reference counts, timeout cleanup, invalid arguments and
interrupt-context resume.

The modeled ready bit is controlled by the test. This does not emulate analog
settling, clock gating or a physical power domain. Run the out-of-tree
``zephyr-device-pm-test/tests/vref`` layers on MCXN/MCXA hardware for that evidence.

Build and run through Twister::

   west twister -T tests/drivers/regulator/nxp_vref_pm -p qemu_cortex_m3
