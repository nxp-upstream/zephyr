.. _nvmem_api:

Non-Volatile Memory (NVMEM)
###########################

The NVMEM driver model provides a uniform way to access non-volatile memory
devices such as EEPROMs, OTP/eFuse, FRAM, and battery-backed RAM. Consumers
use the NVMEM provider driver API directly.

The API is intentionally small and provider-centric: drivers expose basic
read/write operations and report their total size. Providers are responsible
for enforcing device semantics (e.g., read-only policy, 1→0 programming for
OTP/eFuse). Optionally, providers can expose a configuration descriptor for
tooling and diagnostics.

.. contents::
    :local:
    :depth: 2


NVMEM Provider API
******************

Zephyr's NVMEM provider API is implemented by device drivers for specific
non-volatile memories (e.g., AT24/AT25 EEPROMs, NXP OCOTP eFuse). Consumers
interact with these providers via a small set of functions:

- :c:func:`nvmem_read` – Read bytes from the provider address space
- :c:func:`nvmem_write` – Write bytes to the provider (if permitted)
- :c:func:`nvmem_get_size` – Get the total size (in bytes)
- :c:func:`nvmem_get_info` – Optionally obtain a provider descriptor
   (:c:struct:`nvmem_info`) including storage type and current read-only state

Typical usage in a consumer:

1. Obtain the provider device, for example via devicetree:

   .. code-block:: c

      const struct device *nvmem = DEVICE_DT_GET(DT_NODELABEL(mac_eeprom));
      if (!device_is_ready(nvmem)) {
          return -ENODEV;
      }

2. Read or write using byte offsets in the provider address space:

   .. code-block:: c

      uint8_t mac[6];
      int err = nvmem_read(nvmem, /* offset */ 0xFA, mac, sizeof(mac));
      if (err) {
          /* handle error */
      }

      /* Optional write, if provider allows */
      err = nvmem_write(nvmem, 0x100, some_data, some_len);
      if (err == -EROFS) {
          /* provider is read-only (e.g., OTP default) */
      }

3. Optionally query provider configuration (when available) for display or
   validation purposes:

   .. code-block:: c

      const struct nvmem_info *info = nvmem_get_info(nvmem);
      size_t size = nvmem_get_size(nvmem);
      if (info) {
          printk("nvmem: type=%d, read_only=%d, size=%zu\n",
                 (int)info->type, info->read_only, size);
      }

Provider responsibilities
=========================

- Enforce device semantics in their :c:func:`nvmem_read`/:c:func:`nvmem_write`
  implementations (e.g., OTP 1→0 only; return ``-EROFS`` when writes are disabled)
- Handle alignment/stride/granularity according to hardware rules
- Optionally expose :c:struct:`nvmem_info` via :c:func:`nvmem_get_info`
   for tooling and diagnostics (e.g., storage :c:enum:`nvmem_type`, runtime read-only)


Devicetree
**********

Providers can declare a fixed layout of named “cells” using the
``fixed-layout`` binding. This allows consumers to reference fields (like a
MAC address) by name without hardcoding offsets.

Fixed layout for cells
======================

Example devicetree fragment declaring a provider with a named MAC-address cell:

.. code-block:: devicetree

   mac_eeprom: mac_eeprom@2 {
           compatible = "atmel,at24";        /* provider example */
           reg = <0x2>;                       /* I2C address (provider-specific) */
           status = "okay";

           nvmem-layout {
                   compatible = "fixed-layout";
                   #address-cells = <1>;
                   #size-cells = <1>;

                   mac_address: mac_address@fa {
                           reg = <0xFA 0x06>;  /* offset, size in bytes */
                           #nvmem-cell-cells = <0>;
                   };
           };
   };

Consumers can then obtain the cell at build time using helper macros from
``<zephyr/drivers/nvmem.h>``:

.. code-block:: c

   #include <zephyr/drivers/nvmem.h>

   const struct nvmem_cell mac = NVMEM_CELL_INIT(DT_NODELABEL(mac_address));

   if (nvmem_cell_is_ready(&mac)) {
       uint8_t buf[6];
       int err = nvmem_read(mac.dev, mac.offset, buf, mac.size);
       /* handle err */
   }

Other useful helpers include:

- :c:macro:`NVMEM_CELL_GET_BY_NAME(node, name)` and ``_OR`` variants
- :c:macro:`NVMEM_CELL_GET_BY_IDX(node, idx)` and ``_OR`` variants
- :c:macro:`NVMEM_CELL_INST_GET_BY_NAME(inst, name)` and ``_IDX`` variants

NXP OCOTP (eFuse) example
=========================

The NXP OCOTP provider accesses eFuse via a ROM API table and supports a fixed
layout for named cells. Example:

.. code-block:: devicetree

   ocotp: ocotp@0 {
           compatible = "nxp,ocotp";
           rom-api-tree-addr = <0x00200120>;  /* SoC-specific */
           status = "okay";

           nvmem-layout {
                   compatible = "fixed-layout";
                   #address-cells = <1>;
                   #size-cells = <1>;

                   mac0: mac0@134 {          /* byte offset 0x134, size 6 */
                           reg = <0x134 0x6>;
                           #nvmem-cell-cells = <0>;
                   };
           };
   };

Note: Providers define how offsets are interpreted; OCOTP treats ``reg``
addresses as byte offsets into the eFuse space and extracts the requested
bytes from the containing 32-bit words.


Configuration Options
*********************

Related configuration options:

- :kconfig:option:`CONFIG_NVMEM_MODEL` – Enable the NVMEM driver model
- :kconfig:option:`CONFIG_NVMEM_MODEL_API` – Export the NVMEM API
- :kconfig:option:`CONFIG_NVMEM_INIT_PRIORITY` – Provider init priority
- :kconfig:option:`CONFIG_NVMEM_LOG_LEVEL` – Logging verbosity for NVMEM drivers
- :kconfig:option:`CONFIG_NVMEM_AT24` / :kconfig:option:`CONFIG_NVMEM_AT25` – Enable AT24/AT25 providers
- :kconfig:option:`CONFIG_NVMEM_NXP_OCOTP` – Enable NXP OCOTP eFuse provider
- :kconfig:option:`CONFIG_NVMEM_NXP_OCOTP_WRITE_ENABLE` – Allow OCOTP programming (default: off)


Notes and guidance
******************

- Consumers should use the provider API directly (:c:func:`nvmem_read` /
  :c:func:`nvmem_write`) and, when using fixed layouts, the :c:struct:`nvmem_cell`
  helpers provided in ``<zephyr/drivers/nvmem.h>``.
- Providers are the source of truth for policy: read-only behavior, lock/Write
  Protect enforcement, shadow handling, and SoC-specific sequences are fully
  handled inside each provider's driver.
- For OTP/eFuse, writes are typically disabled by default. Attempting a write
  returns ``-EROFS`` unless the platform explicitly enables programming.


API Reference
*************

.. doxygengroup:: nvmem_driver_interface
   :project: Zephyr
   :content-only:
