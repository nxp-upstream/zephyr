# RFC: Device-side Cache Driver Model for SoC Memory-side Caches

## Problem Description

Zephyr’s current cache support targets CPU L1 caches (the “sys cache”) and effectively one cache instance per path. This is sufficient for most Cortex‑M internal caches, but cannot model SoC memory‑side caches (external/XIP/flash/cache controllers), nor multiple cache instances.

Real hardware needing this:

- NXP: CACHE64 (FlexSPI XIP), LPCAC/XCACHE + CACHE64 (multiple instances)
- Espressif: external flash/PSRAM caches
- Microchip: SAM Cache Controllers
- Infineon: AURIX external cache controllers
- Others with external memory accelerators/caches

Today, xcache/lpcac can fit the sys cache, but cache64 cannot be supported simultaneously as an additional cache instance.

## Proposed Change

Introduce a device-side cache driver model alongside the existing sys cache:

- Keep sys cache APIs as-is for CPU L1.
- Add a driver subsystem for SoC memory‑side caches under drivers/cache_device with a small, uniform API (enable/disable, all-ops, range-ops, optional info).
- Share a common cache info struct between sys/device cache.
- Add a small header-only DMA coherence helper that composes sys + device cache maintenance in the correct order, without changing existing DMA APIs.
- Describe device caches via Devicetree using a binding that includes shared cacheinfo properties.

This mirrors Linux: arch-owned CPU cache + “outer” cache drivers (e.g., PL310, LLCC) and a cacheinfo-style introspection.

## Linux Model and Zephyr Mapping

- Linux models CPU caches in arch code and “outer”/LLC caches as device drivers; exposes a standard cacheinfo view via DT/ACPI.
- Zephyr currently exposes sys cache via include/zephyr/cache.h or include/zephyr/arch/cache.h; no notion of a second cache instance not owned by the CPU.
- Many MCUs implement external memory caches (XIP/flash/PSRAM) separate from CPU L1.

Linux splits cache responsibilities into distinct layers:

1. Arch CPU caches

   - Implemented in arch/* (e.g. arch/arm64/mm/cache.S, arch/x86/mm/cache.c).
   - Exposed only via internal helpers; generic subsystems never poke raw cache registers.
2. Outer / LLC / memory-side cache controllers

   - Example drivers:
     - drivers/cache/l2x0.c (ARM PL310)
     - drivers/soc/qcom/llcc-qcom.c (Qualcomm Last Level Cache Controller)
   - These are probed like ordinary devices via Devicetree or ACPI.
   - Provide enable/init, optional partitioning, and maintenance ops (clean/invalidate by way or whole).
3. Cache topology introspection

   - drivers/base/cacheinfo.c + include/linux/cacheinfo.h build a unified view from DT/ACPI/CPUID.
   - Sysfs exposes /sys/devices/system/cpu/cpuN/cache/indexM/{level,type,size,line_size,shared_cpu_map,…}.
   - DT bindings (Documentation/devicetree/bindings/cache/*.yaml) use:
     - cache-level
     - cache-unified
     - next-level-cache phandles to form hierarchy.
4. DMA coherence layer

   - dma_map_ops (arch-specific) implement: `dma_map_*()`, `dma_unmap_*()`, `dma_sync_*()`.
   - They sequence cache maintenance (clean before device reads; invalidate after device writes) and add address translation / bounce buffering when needed.
   - Drivers never perform manual cache maintenance if they use the DMA API.
5. Memory-type / policy programming

   - Done via page attributes: x86 PAT/MTRR, ARM MAIR + page tables, ioremap_* variants.
   - Region “policy” is rarely exposed as a generic cache driver API; device-local configuration (e.g. QSPI/XIP controllers) sets any special caching mode.
6. Error conventions

   - Return 0 on success.
   - -EINVAL for bad arguments.
   - -EOPNOTSUPP (or -ENOTSUPP / -ENOTSUP) for unsupported operations.
   - Propagate underlying -errno from bus/register failures.

How Zephyr’s design maps:

| Linux Concept              | Zephyr Element                                  | Notes                                              |
| -------------------------- | ----------------------------------------------- | -------------------------------------------------- |
| Arch CPU cache ops         | sys cache (cache.h/ arch/cache.h)               | Remains arch-owned; unchanged.                     |
| Outer/LLC device cache     | New cache_device driver model                   | Multiple instances; per-device syscalls.           |
| cacheinfo hierarchy        | Shared struct cache_info + DT cacheinfo.yaml    | No hierarchy linking yet (future extension).       |
| DMA mapping coherence      | Header-only dma_coherence helper (optional)     | Composes sys + device cache maintenance; no IOMMU. |
| Policy via page attributes | Deferred / driver-local (no generic policy API) | Keeps API minimal; avoids premature abstraction.   |
| Error semantics            | 0 / -EINVAL / -ENOTSUP / -ENOSYS / -ERANGE      | Mirrors Linux patterns where applicable.           |

Key alignment decisions:

- Separation of CPU caches vs memory-side caches, not multiplexed through one opaque API.
- Device cache info comes from DT; introspection does not require runtime probing logic beyond driver init.
- DMA coherence helper copies Linux sequencing rules (clean inner → outer, invalidate outer → inner) without importing full dma_map_ops complexity.

Non-goals (explicitly not copied from Linux):

- Sysfs hierarchy export.
- Page table / memory-type manipulation.
- Complex partition/quality-of-service cache allocation APIs.

## Detailed Design

### APIs

- include/zephyr/drivers/cache_device.h

  - Per-device operations (all with `__syscall`):
    - enable/disable
    - flush_all / invalidate_all / flush_and_invalidate_all
    - flush_range / invalidate_range / flush_and_invalidate_range
  - Optional info query:
    - `int cache_device_get_info(const struct device *dev, struct cache_info *info)`
  - Return codes (all functions):
    - 0: success
    - -ENOSYS: driver method not implemented
    - -ENOTSUP: operation not supported
    - -EINVAL: invalid arguments (NULL pointer, size == 0, invalid alignment)
    - -ERANGE: may be used by drivers internally to signal “range falls outside this device window”; public entrypoints SHOULD normalize to -ENOTSUP when no device applies
    - other -errno: hardware/low-level failure
- include/zephyr/cache_info.h (new)

  - Shared cacheinfo used by both sys and device cache:

    ```c
    struct cache_info {
        uint8_t  cache_type;   /* instruction | data | unified */
        uint8_t  cache_level;  /* 1, 2, ... */
        uint16_t reserved;
        uint32_t line_size;    /* bytes */
        uint32_t ways;         /* 0 if unknown */
        uint32_t sets;         /* 0 if unknown */
        uint32_t size;         /* bytes, 0 if unknown */
        uint32_t attributes;   /* bitmask: WRITE_THROUGH, WRITE_BACK, READ_ALLOCATE, WRITE_ALLOCATE */
    };
    /* CACHE_INFO_TYPE_* and CACHE_INFO_ATTR_* bit definitions */
    ```

- include/zephyr/cache.h (sys cache)

  - Provides inline wrappers to query sys cache info (no policy, no drivers mandated):

    ```c
    /* Provided in header like other sys cache APIs; forwards to weak hooks if present */
    int sys_cache_data_get_info(struct cache_info *info);
    int sys_cache_instr_get_info(struct cache_info *info);
    ```
- Behavior:

  - Returns -EINVAL on NULL
  - Returns -ENOTSUP if the platform does not provide information
  - If an external sys-cache provider exists, it can define weak hooks:
    - `int cache_data_get_info(struct cache_info *info) __attribute__((weak));`
    - `int cache_instr_get_info(struct cache_info *info) __attribute__((weak));`

  ### Advantages of struct cache_info in cache.h

  Exposing `struct cache_info` via `include/zephyr/cache.h` brings several practical benefits:

  - Unified Introspection: One common structure describes both CPU (sys) and device caches, avoiding parallel, incompatible descriptions.
  - Devicetree Alignment: Fields map directly to the shared `cacheinfo.yaml` schema, ensuring what DT declares is exactly what APIs return.
  - Portability: Eliminates ad‑hoc, arch‑specific structs in favor of a stable, cross‑arch contract that works on ARM, RISC‑V, x86, etc.
  - Extensibility: Reserved fields and the `attributes` bitmask allow adding capabilities without breaking ABI or callers.
  - Simpler APIs: Generic getters (`sys_cache_*_get_info`) fill the same struct as device drivers, reducing boilerplate and duplication across subsystems.
  - Tooling & Debug: A single struct enables common logging, shell dumps, and tests to validate cache topology and parameters.
  - Multi‑instance Ready: The same struct can describe L1/L2/external caches, enabling future aggregation or hierarchy views without new types.
  - Performance Tuning: Callers can adapt to `line_size`, `ways`, or `size` at runtime (e.g., align DMA buffers, choose invalidate granularity).
  - Backward Compatible: This is additive; existing cache maintenance APIs remain unchanged.

  Example (using reported line size to align a DMA buffer):

  ```c
  struct cache_info ci;
  if (sys_cache_data_get_info(&ci) == 0 && ci.line_size) {
    size_t align = ci.line_size;
    void *buf = aligned_alloc(align, round_up(len, align));
    /* Use buf for DMA, knowing invalidations/flushes are aligned */
  }
  ```

### Kconfig: Information API Gating

Two independent configuration symbols gate cache information exposure:

- CONFIG_SYS_CACHE_INFO (default n): opt‑in; platforms enabling must implement cache_data_get_info()/cache_instr_get_info(). No weak stubs; absence causes link failure (intentional).
- CONFIG_DEVICE_CACHE_INFO (unchanged): governs cache_device_get_info(); if disabled, callers receive -ENOTSUP.

Behavior summary:

- Disabled symbol → caller gets `-ENOTSUP` (no ABI break).
- Enabled without provider (sys) or driver method (device) → `-ENOTSUP`.
- Enabled with provider/driver → `0` and filled `struct cache_info`.

Rationale:

- Keeps headers free of weak declarations for device caches while allowing arch/SoC override for sys caches via weak stubs in a .c file.
- Allows platforms to ship without info support (legacy behavior) while newer devices/architectures can opt in incrementally.
- Separates concerns: a board may want device cache info but not sys cache info (or vice versa).

- include/zephyr/dma/dma_coherence.h (new, header-only helper)

  - Simple directional helpers composing sys + device cache ops, without altering existing DMA APIs:

    ```c
    enum dma_direction { DMA_TO_DEVICE, DMA_FROM_DEVICE, DMA_BIDIRECTIONAL };

    static inline int dma_cache_prepare(void *addr, size_t size, enum dma_direction dir);
    static inline int dma_cache_complete(void *addr, size_t size, enum dma_direction dir);
    ```
- Semantics and ordering:

  - DMA_TO_DEVICE (CPU produced, device will read): clean sys, then clean device cache
  - DMA_FROM_DEVICE (device produced, CPU will read): invalidate device, then invalidate sys cache
  - BIDIRECTIONAL: clean on prepare; invalidate on complete
- Graceful fallback:

  - If device cache is absent, device-cache calls return -ENOSYS/-ENOTSUP; helper treats them as non-fatal.
  - ERANGE should not surface from global APIs; global composition normalizes “no applicable device” to -ENOTSUP.
  - If sys cache ops are no-ops, helper still succeeds.
- No new Kconfig needed: device-cache calls are compiled normally; platforms without cache_device drivers simply return -ENOSYS/-ENOTSUP.

Example usage:

```c
/* TX */
dma_cache_prepare(tx_buf, len, DMA_TO_DEVICE);
start_dma_tx(...);
dma_cache_complete(tx_buf, len, DMA_TO_DEVICE); /* usually a no-op */

/* RX */
start_dma_rx(...);
dma_cache_complete(rx_buf, len, DMA_FROM_DEVICE); /* invalidate to ensure fresh data */
```

### Devicetree Bindings

- dts/bindings/cacheinfo.yaml (shared schema)

  - Properties:
    - cache-type: "instruction" | "data" | "unified"
    - cache-level: int
    - cache-line-size: int (bytes)
    - cache-ways: int
    - cache-sets: int
    - cache-size: int (bytes)
    - cache-attributes: string-array enum ["write-through", "write-back", "read-allocate", "write-allocate"]
- dts/bindings/cache_device/zephyr,cache-device.yaml

  - includes: [base.yaml, cacheinfo.yaml]
  - properties:

    - reg: MMIO register range(s)
    - dma-coherent: boolean (optional; if true, skip device-cache maintenance in DMA helper)
  - Example:

    ```dts
    cache0: cache@40000000 {
      compatible = "zephyr,cache-device";
      reg = <0x40000000 0x1000>;
      cache-type = "unified";
      cache-level = <2>;
      cache-line-size = <64>;
      cache-ways = <4>;
      cache-sets = <128>;
      cache-size = <32768>;
      cache-attributes = ["write-back","read-allocate","write-allocate"];
      dma-coherent; /* optional */
    };
    ```

### Driver Guidance

- Implement the driver API in cache_device.h:
  - All-ops and range-ops as applicable. Return -ENOTSUP for unsupported ops or ranges.
  - Optional get_info() to populate struct cache_info (from DT or hardware).
- Multi-instance support:
  - Use devicetree instance data to create multiple devices.
  - Range ops should validate that [addr, addr+size-1] intersects the device’s managed window; drivers may detect out-of-window internally as -ERANGE, but public entrypoints SHOULD return -ENOTSUP when no device applies.
- Keep policy programming (e.g., per-region policies like NXP CACHE64) device-local; no generic region policy API is introduced at this time.

### DMA Coherence

- Direction semantics:
  - DMA_TO_DEVICE: CPU wrote buffer; device will read → clean (write back) caches
  - DMA_FROM_DEVICE: device wrote buffer; CPU will read → invalidate caches
- Ordering:
  - Clean: sys (inner) first, then device (outer)
  - Invalidate: device (outer) first, then sys (inner)
- Helper is optional: existing code using sys cache or non-cacheable regions remains valid.

### Error Semantics (range ops)

- 0: success
- -EINVAL: invalid arguments (NULL addr, size == 0, impossible alignment)
- -ENOTSUP: operation or range not supported (e.g., cache absent, address window not covered). Global entrypoints SHOULD return -ENOTSUP when no device applies.
- -ERANGE: driver-internal optional code for “range outside this device window”; SHOULD NOT be surfaced by global APIs.
- -ENOSYS: driver did not implement the method
- other -errno: hardware failure or vendor HAL error propagation

### Compatibility and Migration

- No change to existing sys cache ABI.
- Device cache subsystem is additive; boards without device caches are unaffected.
- Drivers may adopt dma_coherence.h helper to get correct sequencing automatically; existing .nocache region approaches remain valid.

## Dependencies

- Existing cache management (CONFIG_CACHE_MANAGEMENT)
- Devicetree binding infrastructure
- Optional: DMA drivers to exercise the helper

## Alternatives Considered

- Extending sys cache to multiplex multiple caches: conflates CPU and device caches; rejected for clarity.
- A single global cache API backending to either CPU or device caches: prevents multi-instance/mixed use cases; rejected.
