# Sys/Device Cache Model and DMA Coherence in Zephyr

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

### Architecture

This RFC applies a pragmatic cache model with 2 layers. It preserves the existing, user-facing `sys_cache_*` APIs while introducing an internal device-cache layer. This somehow mirrors Linux: arch-owned CPU cache + “outer” cache drivers (e.g., PL310, LLCC) and a cacheinfo-style introspection.

```text
┌─────────────────────────────────────────┐
│  Layer App: User-Focused Public APIs     │  ← Primary interface for app developers()
├─────────────────────────────────────────┤
├─────────────────────────────────────────┤
│  Layer Driver: Infrastructure            │  ← Internal driver model (this series)
└─────────────────────────────────────────┘
```

- Layer App: `sys_cache_*` remains the app-facing API; with the router enabled these calls compose CPU + device-side caches transparently.
- Layer Driver: Minimal driver model (`cache_device.h`) describes per-instance device caches via Devicetree, powering the dispatcher and router.
  - Add a driver subsystem for SoC memory‑side caches under drivers/cache_device with a small, uniform API (enable/disable, all-ops, range-ops, optional info).
  - Share a common cache info struct between sys/device cache.
  - Describe device caches via Devicetree using a binding that includes shared cacheinfo properties.

### DMA Coherence

Add a small header-only DMA coherence helper that composes sys + device cache maintenance in the correct order, without changing existing DMA APIs.

## Detailed Design

### Layer App

This layer is the user‑facing API surface. It keeps the existing `sys_cache_*` contract and extends it to transparently cover device caches when enabled.

- **Scope**:
  - Existing `sys_cache_*` APIs remain the only public cache maintenance interface for applications.
  - When `CONFIG_CACHE_DEVICE_ROUTER=y`, `sys_cache_*` operations are internally routed to both CPU (arch/sys) caches and device caches.
- **Key behavior**:
  - **No API changes**: Function names, arguments, and return codes of `sys_cache_*` stay the same.
  - **Composition**:
    - Range and “all” operations compose CPU and device caches via a router:
      - Data: `sys_cache_data_*` → router → arch D‑cache + device‑cache dispatcher.
      - Instruction: `sys_cache_instr_*` → router → arch I‑cache + device‑cache dispatcher.
    - If either side returns `-EINVAL`, that error dominates.
    - If at least one side succeeds (`0`), the overall result is `0` unless a hard error was seen.
    - `-ENOSYS`/`-ENOTSUP` from one side are treated as non‑fatal when the other side succeeds.
  - **Fallback when router is disabled**:
    - With `CONFIG_CACHE_DEVICE_ROUTER=n`, `sys_cache_*` behave as today and only drive the CPU/sys cache implementation.
    - Platforms without device caches or that do not select `CACHE_DEVICE` see no behavior change.

This preserves source compatibility while enabling sys‑cache callers to benefit from device caches automatically on platforms that opt in.

### Layer Driver

This layer is the internal driver model and infrastructure. It introduces device‑cache drivers, a dispatcher, and the router that composes CPU + device caches.

#### Why a cache_device Driver API?

Introducing a small, generic `cache_device` driver API avoids pushing all device‑specific cache details into SoC glue or ad‑hoc helpers:

- **Per‑device abstraction**: Different IP blocks (NXP CACHE64, LPCAC/XCACHE, SAM cache controllers, future external PSRAM/flash caches) still need a per‑instance abstraction for:
  - Address windows and coverage.
  - Local errata and workarounds.
  - Power‑management hooks and sequencing.
- **Avoiding SoC‑specific glue**: Without `cache_device_driver_api` and the `cache_device_*` syscalls:
  - Each SoC would need its own private headers and helper functions that call vendor HALs directly.
  - Patterns like window checks, error mapping, and PM integration would be duplicated across SoCs.
  - The router (and any higher‑level logic) would need to know about concrete IP names and HALs rather than a generic interface.
- **Scalability and Devicetree integration**: A generic driver model allows:
  - Multiple instances described purely by Devicetree (`zephyr,cache-device` + per‑SoC compatibles).
  - The dispatcher to fan out based on DT‑defined windows, without hard‑coding IP knowledge.
  - Adding new SoCs or cache IPs by writing one driver instead of extending SoC‑specific C glue.

In principle, the router could call vendor HAL functions directly per SoC, but that would:

- Eliminate a reusable “outer cache” layer.
- Tie the design to SoC‑specific implementations instead of a DT‑described, per‑instance model.
- Make it harder to evolve or extend cache support across architectures and vendors.

Therefore, the `cache_device` API is kept as a small but essential Layer‑Driver contract, while remaining internal and not exposed as a general application API.

#### Driver Model Components

- **Device‑cache driver API (`include/zephyr/drivers/cache_device.h`)**:

  - Per‑device operations (all are syscalls, but considered system‑internal):
    - `enable`, `disable`
    - `flush_all`, `invalidate_all`, `flush_and_invalidate_all`
    - `flush_range`, `invalidate_range`, `flush_and_invalidate_range`
  - Optional per‑device info:
    - `int cache_device_get_info(const struct device *dev, struct cache_info *info)`
  - Error conventions:
    - `0`: success
    - `-EINVAL`: bad pointer, size `== 0`, invalid alignment
    - `-ENOSYS`: method not implemented by the driver
    - `-ENOTSUP`: operation not supported for this device (or no device applies, after composition)
    - `-ERANGE`: internal hint “range not in this device’s window”; used inside the dispatcher to skip this device, not exposed from global entrypoints
    - other negative `errno`: HW / HAL failures

- **Dispatcher (`drivers/cache_device/cache_device.c`)**:

  - Global APIs:
    - `cache_device_flush_range`, `cache_device_invalidate_range`, `cache_device_flush_and_invalidate_range`
    - `cache_device_enable_all`, `cache_device_disable_all`
    - Data/instruction helpers: `cache_device_data_*` and `cache_device_instr_*` (all and range)
  - Behavior:
    - Iterates all ready `zephyr,cache-device` instances from Devicetree.
    - For each instance, calls its vtable method if present.
    - `-ERANGE` from a device means “this address does not belong to this cache”; dispatcher continues with other devices.
    - If no device successfully handles the range, dispatcher returns the first non‑zero error it saw, normalized to `-ENOTSUP` if the only reason was “no device applies`.

- **Device drivers (example: `drivers/cache_device/cache_device_nxp_cache64.c`)**:

  - Implement the `cache_device_driver_api` vtable on top of vendor HALs (e.g., CACHE64, LPCAC).
  - Take window and cache properties from Devicetree:
    - Windows: only operate on ranges fully covered by declared `cache-windows`; otherwise return `-ERANGE` to let dispatcher try other devices.
    - Static description (line size, ways, sets, size, attributes) may be exposed via `get_info` if enabled.
  - Support multiple instances by using DT instances (`DEVICE_DT_INST_DEFINE`).

- **Router (`drivers/cache/cache_router.c`)**:

  - Internal hooks `z_sys_cache_*_router` for:
    - data/instruction enable/disable,
    - all‑ops,
    - range‑ops.
  - Each router hook:
    - Optionally calls arch/sys cache implementation.
    - Calls the appropriate device‑cache dispatcher helper (`cache_device_data_*` / `cache_device_instr_*`).
    - Composes return codes (as described in Layer App) into a single sys‑cache result.
  - Enabled via `CONFIG_CACHE_DEVICE_ROUTER`; when off, `sys_cache_*` call existing arch/sys hooks directly.

- **Kconfig and Devicetree plumbing**:

  - `CONFIG_CACHE_DEVICE`: enables the device‑cache subsystem and builds `drivers/cache_device`.
  - `CONFIG_CACHE_DEVICE_ROUTER`: enables router composition of sys + device caches.
  - `drivers/cache_device` and `drivers/cache` CMake/Kconfig glue wire these pieces into the Zephyr build.
  - Devicetree bindings (`zephyr,cache-device.yaml`, `nxp,cache64.yaml`) describe device caches (compatible, `reg`, windows, and basic characteristics), and per‑SoC DTS nodes (e.g., LPC55S3x `cache64`) instantiate concrete devices.

### DMA Coherence Helper

The DMA coherence helper is a small, optional layer that composes sys and device cache operations for DMA buffers. It does not change existing DMA driver APIs and is orthogonal to the app and driver layering.

- **Location**: `include/zephyr/dma/dma_coherence.h` (header‑only).
- **API surface**:
  - `enum dma_direction { DMA_TO_DEVICE, DMA_FROM_DEVICE, DMA_BIDIRECTIONAL };`
  - `static inline int dma_cache_prepare(void *addr, size_t size, enum dma_direction dir);`
  - `static inline int dma_cache_complete(void *addr, size_t size, enum dma_direction dir);`
- **Semantics and ordering**:
  - DMA_TO_DEVICE (CPU produced, device will read): clean (write back) sys first, then device.
  - DMA_FROM_DEVICE (device produced, CPU will read): invalidate device first, then sys.
  - DMA_BIDIRECTIONAL: clean on prepare; invalidate on complete.
- **Error and fallback behavior**:
  - Validates `size > 0`; returns `-EINVAL` otherwise.
  - If device cache is absent, device‑cache calls return `-ENOSYS`/`-ENOTSUP`; the helper treats them as non‑fatal when sys cache maintenance succeeds.
  - If sys cache ops are no‑ops, the helper still returns success as long as there is no hard error.
  - No extra Kconfig is required; platforms without device‑cache drivers simply see `-ENOSYS`/`-ENOTSUP` from the device side, which the helper masks when safe.
  - Direction `DMA_BIDIRECTIONAL` is supported but otherwise unremarkable; unsupported directions return `-ENOTSUP`.

### Bypassing Device-Cache Maintenance

Some feedback pointed out that certain memory-side caches (for example, CACHE64 on LPC55S3x) often do not need explicit runtime maintenance for common workloads, and that unnecessary clean/invalidate operations can hurt performance. The proposed model supports bypassing device-cache maintenance in a controlled way without changing the app‑facing `sys_cache_*` contract. There are two knobs: a coarse per-device on/off switch and a fine-grained per-device hint.

#### Coarse per‑device bypass

The simplest bypass leverages the existing Devicetree compatible used by the dispatcher:

- The dispatcher in `drivers/cache_device/cache_device.c` is built with `DT_DRV_COMPAT = zephyr_cache_device` and only iterates nodes whose `compatible` list includes `"zephyr,cache-device"`.
- If a device cache node (for example, LPC55S3x `cache64`) is described as:

  ```dts
  cache64: cache64@2E000 {
    compatible = "nxp,cache64", "zephyr,cache-device";
    /* ... */
  };
  ```

  then CACHE64 participates fully in the device‑cache model and receives maintenance via the dispatcher/router whenever `CONFIG_CACHE_DEVICE` and `CONFIG_CACHE_DEVICE_ROUTER` are enabled.

- If a board or SoC wants to completely bypass CACHE64 in the generic mechanism, it can simply omit the generic compatible:

  ```dts
  cache64: cache64@2E000 {
    compatible = "nxp,cache64"; /* no "zephyr,cache-device" */
    /* ... */
  };
  ```

  In this configuration:

  - The dispatcher never sees that device cache.
  - The router never calls it via `cache_device_*`.
  - `sys_cache_*` and the DMA coherence helpers only operate on CPU/sys caches.

This provides a coarse, per‑device on/off switch using existing mechanisms: boards that know a given device cache does not require runtime maintenance can drop `"zephyr,cache-device"` and effectively bypass it while keeping the rest of the design unchanged.

#### Fine‑grained bypass (dma-coherent hint)

For boards that want to keep a device cache in the generic topology (for example, to leave the option of enabling maintenance later) but still skip its maintenance for common DMA/data workflows, a finer‑grained hint is available via Devicetree.

The `zephyr,cache-device` binding defines an optional boolean property:

```yaml
dma-coherent:
  type: boolean
  description: >
    Hint that memory served by this cache is coherent for DMA/data paths
    and does not require explicit device-cache maintenance.
```

When a device-cache node sets `dma-coherent;`, its driver may implement the following policy:

- In the device‑cache vtable implementations (`flush_range`, `invalidate_range`, `flush_and_invalidate_range`, and/or the corresponding `*_all` ops), check the `dma-coherent` flag.
- If the flag is set, return `-ENOTSUP` from those maintenance methods instead of calling the vendor HAL.
- The dispatcher interprets `-ENOTSUP` as “this device does not participate in this operation” and continues composing results as usual.
- Router logic already treats `-ENOTSUP` from the device side as non‑fatal when the CPU/sys cache succeeds, so overall `sys_cache_*` still returns success based on the CPU side.

With this pattern, device caches (including CACHE64) remain `zephyr,cache-device` instances, but board integrators have a documented, per-device way to disable their maintenance for selected workflows, trading correctness guarantees for performance when they know it is safe.

### Cache Information

The cache information pieces are deliberately orthogonal. They provide introspection but are not required for cache maintenance correctness and can be evolved or removed without impacting the app/driver layering.

- **Shared struct (`include/zephyr/cache_info.h`)**:

  - Defines a common `struct cache_info` for both sys caches and device caches:
    - Type flags: instruction, data, unified.
    - Level: L1/L2/…
    - Line size, ways, sets, total size.
    - Attributes bitmask (write‑through, write‑back, read‑allocate, write‑allocate).
  - Intended as a stable, cross‑arch description that also maps directly to Devicetree fields when present.
- **Sys‑cache info APIs (`include/zephyr/cache.h`, `include/zephyr/drivers/cache.h`)**:

  - Optional Kconfig: `CONFIG_SYS_CACHE_INFO` (default `n`).
  - When enabled, platforms may implement:
    - `int cache_data_get_info(struct cache_info *info);`
    - `int cache_instr_get_info(struct cache_info *info);`
  - Public wrappers:
    - `int sys_cache_data_get_info(struct cache_info *info);`
    - `int sys_cache_instr_get_info(struct cache_info *info);`
  - Behavior:
    - `-EINVAL` if `info == NULL`.
    - `-ENOTSUP` if the platform does not provide information or the symbol is disabled.
    - `0` when `info` is filled.
  - When `CONFIG_SYS_CACHE_INFO` is disabled, callers see `-ENOTSUP`; the rest of the sys‑cache maintenance API is unchanged.
- **Device‑cache info (`include/zephyr/drivers/cache_device.h`)**:

  - Optional Kconfig: `CONFIG_DEVICE_CACHE_INFO`.
  - When enabled and a driver implements `get_info`:
    - `int cache_device_get_info(const struct device *dev, struct cache_info *info);`
    - Fills the same `struct cache_info` as sys‑cache info, typically from Devicetree or hardware.
  - When disabled:
    - Calls return `-ENOTSUP` even if drivers have an implementation.
  - When enabled but driver lacks `get_info`:
    - Returns `-ENOSYS`.
- **Devicetree schema alignment (`dts/bindings/cacheinfo.yaml`, `dts/bindings/cache_device/zephyr,cache-device.yaml`)**:

  - Shared `cacheinfo.yaml` defines properties that map 1:1 to `struct cache_info`:
    - `cache-type`, `cache-level`, `cache-line-size`, `cache-ways`, `cache-sets`, `cache-size`, `cache-attributes`.
  - The `zephyr,cache-device` binding and SoC‑specific bindings (e.g. `nxp,cache64.yaml`) include this schema so DT can describe caches consistently even if runtime info is disabled.
  - This keeps DT description and optional runtime introspection aligned but not mutually dependent.
- **Status and deprecation considerations**:

  - All cache information APIs and Kconfig switches are strictly additive:
    - Existing platforms and applications are not forced to enable them.
    - Callers must handle `-ENOTSUP` gracefully.
  - Because this layer is not required for cache maintenance correctness, it can be evolved or deprecated independently of the Layer App and Layer Driver designs.

## Cache Working Scenario

This section walks through how the cache layers behave in typical usage. The goal is to make the routing, driver model, and DMA helper behavior concrete for application, driver, and SoC authors.

### Cache Use Scenarios and Workflow

#### 1. Pure sys cache platform (no device cache)

Configuration:

- `CONFIG_CACHE_MANAGEMENT=y`
- `CONFIG_ARCH_CACHE=y`
- `CONFIG_CACHE_DEVICE=n`
- `CONFIG_CACHE_DEVICE_ROUTER=n`

Workflow:

1. Application calls `sys_cache_data_flush_range(addr, size)` or `sys_cache_instr_invd_all()`.
2. The inline wrappers in `include/zephyr/cache.h` call directly into the existing arch/sys cache hooks (e.g., `arch_dcache_flush_range`, `arch_icache_invd_all`).
3. Device‑cache dispatcher and router are not built or used; behavior is identical to current Zephyr.

Effect:

- No change for existing boards without device caches.
- The new subsystem is completely transparent.

#### 2. Sys cache + single device cache (router enabled)

Configuration (e.g., LPC55S36 with CACHE64 enabled):

- `CONFIG_CACHE_MANAGEMENT=y`
- `CONFIG_EXTERNAL_CACHE=y`
- `CONFIG_CACHE_DEVICE=y`
- `CONFIG_CACHE_DEVICE_ROUTER=y`
- Devicetree: one `zephyr,cache-device` instance (e.g., NXP CACHE64) marked `status = "okay"`.

Workflow for a data range flush (`sys_cache_data_flush_range(addr, size)`):

1. Application (or subsystem) calls `sys_cache_data_flush_range(addr, size)`.
2. Because `CONFIG_CACHE_DEVICE_ROUTER=y`, the wrapper calls `z_sys_cache_data_flush_range_router(addr, size)`.
3. The router performs two actions:

- Validates arguments and calls the sys implementation (e.g., `arch_dcache_flush_range`).
- Calls `cache_device_data_flush_range(addr, size)`, which dispatches to the single CACHE64 device.

4. The CACHE64 driver runs its range logic:

- Checks that `[addr, addr + size - 1]` is fully inside one of its configured `cache-windows`.
- If yes, calls the vendor HAL (e.g., `CACHE64_CleanCacheByRange`) and returns `0`.
- If not, returns `-ERANGE` to signal “not my window”. The dispatcher sees that no device handled the range and normalizes the result to `-ENOTSUP`.

5. The router then composes return codes from sys and device sides:

- If either side returns `-EINVAL`, that error is returned.
- If at least one side returns `0` and no hard error exists, the combined result is `0`.
- `-ENOSYS`/`-ENOTSUP` from one side are ignored if the other side succeeded.

Effect:

- Callers only use `sys_cache_*`; both CPU and CACHE64 stay coherent when ranges are inside CACHE64 windows.
- For addresses outside CACHE64 coverage, behavior falls back to pure sys cache, with `-ENOTSUP` indicating that no device cache applied if the sys cache itself also does not act.

#### 3. Sys cache + multiple device caches

Configuration (hypothetical SoC with two device caches):

- `CONFIG_CACHE_MANAGEMENT=y`
- `CONFIG_CACHE_DEVICE=y`
- `CONFIG_CACHE_DEVICE_ROUTER=y`
- Devicetree: multiple `zephyr,cache-device` instances (e.g., external flash cache and external PSRAM cache) with disjoint `cache-windows`.

Workflow for a data range invalidate (`sys_cache_data_invd_range(addr, size)`):

1. Caller invokes `sys_cache_data_invd_range(addr, size)`.
2. `z_sys_cache_data_invd_range_router()` is used (router enabled).
3. Router:

- Calls `arch_dcache_invd_range(addr, size)` as usual.
- Calls `cache_device_data_invalidate_range(addr, size)`.

4. Dispatcher walks all ready `zephyr,cache-device` instances:

- For each device:
  - Calls the per‑device `invalidate_range` method.
  - If the device returns `-ERANGE`, this means “this range is not in my window”; dispatcher continues to the next device.
  - If the device returns `0`, it counts as successfully handled.

5. After all devices:

- If at least one device returned `0`, dispatcher returns `0`.
- If no device handled the range, dispatcher returns `-ENOTSUP` (or first non‑`-ERANGE` error).

6. The router merges the sys‑cache result and dispatcher result as in the previous scenario.

Effect:

- A single `sys_cache_*` call fans out to the appropriate subset of device caches based on address coverage.
- Per‑device windowing logic resides solely in each driver; global behavior is normalized by the dispatcher and router.

#### 4. Device‑cache only call sites (system/internal)

Internal or low‑level code may occasionally use device‑cache APIs directly (e.g., board bring‑up, debug helpers):

1. Code calls `cache_device_flush_range(addr, size)`.
2. Dispatcher behaves as described above, independent of sys cache.
3. This remains a system‑internal interface; applications are expected to stick to `sys_cache_*`.

This is primarily useful for diagnostics or SoC‑specific flows that should not affect the app‑facing contract.

### DMA Coherence Use Scenarios and Workflow

#### 1. DMA on a platform without device caches

Configuration:

- `CONFIG_CACHE_MANAGEMENT=y`
- `CONFIG_CACHE_DEVICE=n` (or no device‑cache driver built)

Workflow (TX path):

1. Driver prepares a transmit buffer `tx_buf`.
2. Before starting DMA, it calls:

- `dma_cache_prepare(tx_buf, len, DMA_TO_DEVICE);`

3. Inside `dma_cache_prepare`:

- Validates `len > 0`.
- Because there is no device cache, only `sys_cache_data_flush_range(tx_buf, len)` is called.

4. Driver programs and starts the DMA transfer.
5. Optionally, after completion it may call

- `dma_cache_complete(tx_buf, len, DMA_TO_DEVICE);` (usually a no‑op for TO_DEVICE).

Effect:

- Behavior matches traditional “flush D‑cache before TX DMA” patterns.
- Helper is a thin wrapper over `sys_cache_*` on such platforms.

#### 2. DMA on a platform with sys cache + device cache

Configuration (e.g., XIP via CACHE64):

- `CONFIG_CACHE_MANAGEMENT=y`
- `CONFIG_CACHE_DEVICE=y`
- DMA buffer resides in a region covered by a device cache window.

Workflow (RX path):

1. Driver sets up a receive buffer `rx_buf` in external memory covered by a device cache.
2. It programs the DMA controller and starts the transfer.
3. After DMA completes and before the CPU reads from `rx_buf`, driver calls:

- `dma_cache_complete(rx_buf, len, DMA_FROM_DEVICE);`

4. Inside `dma_cache_complete`:

- Validates `len > 0`.
- If router is disabled, it directly calls:
  - `cache_device_data_invalidate_range(rx_buf, len)`; dispatcher sends this to the correct device cache (based on windows).
  - `sys_cache_data_invd_range(rx_buf, len)` to ensure the CPU D‑cache has no stale data.
- If router is enabled and maintains both sys and device caches via `sys_cache_*`, the helper may only need to call the sys‑cache path, depending on configuration. In that case, device cache maintenance is already covered by the router and `sys_cache_*`.

5. Any `-ENOSYS`/`-ENOTSUP` from the device side is ignored as long as sys‑cache invalidation succeeds.

Effect:

- Correct invalidate ordering (device first, then sys) is enforced by the helper.
- Drivers do not need to know whether a particular board has a device cache; the helper composes sys/device maintenance based on what is present.

#### 3. Bidirectional DMA buffer

Configuration:

- Same as above, but the buffer is used for both TX and RX over its lifetime.

Workflow:

1. Before first use as TX, driver calls:

- `dma_cache_prepare(buf, len, DMA_BIDIRECTIONAL);`
- This cleans sys (and, if appropriate, device) caches.

2. After a TX DMA completes, driver may skip `dma_cache_complete` for TO_DEVICE or still call it with `DMA_BIDIRECTIONAL` (it will be a no‑op or simple status check).
3. Before or after programming RX DMA into the same buffer, driver starts the transfer.
4. After RX completes, driver calls:

- `dma_cache_complete(buf, len, DMA_BIDIRECTIONAL);`
- This invalidates device cache first (if present and applicable), then sys cache.

Effect:

- A single pair of helper calls covers both directions, keeping the ordering rules intact.
- Error handling remains consistent: `-EINVAL` for invalid inputs, `-ENOTSUP` only when the direction is unsupported or no cache handling is possible at all.

#### 4. DMA on non‑cacheable or coherent regions

Configuration:

- Memory marked as non‑cacheable or hardware‑coherent (no caching in the relevant path).

Workflow:

1. Driver still calls `dma_cache_prepare` / `dma_cache_complete` for consistency.
2. Under the hood, sys and/or device cache operations are either no‑ops or return `-ENOTSUP`.
3. The helper merges these into overall success as long as no hard error occurs.

Effect:

- Call sites do not need per‑region logic; the helper naturally degenerates to a no‑op.
- This simplifies portable driver code across SoCs with different cache topologies.

## Reference: Linux Model and Zephyr Mapping

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

| Linux Concept              | Zephyr Element                           | Notes                                    |
| -------------------------- | ---------------------------------------- | ---------------------------------------- |
| Arch CPU cache ops         | sys cache (cache.h/ arch/cache.h)        | Remains arch-owned; unchanged.           |
| Outer/LLC device cache     | New cache_device driver model            | Multiple instances; per-device syscalls. |
| cacheinfo hierarchy        | Shared struct cache_info + DT cacheinfo.yaml | No hierarchy linking yet (future extension). |
| DMA mapping coherence      | Header-only dma_coherence helper (optional) | Composes sys + device cache maintenance; no IOMMU. |
| Policy via page attributes | Deferred / driver-local (no generic policy API) | Keeps API minimal; avoids premature abstraction. |
| Error semantics            | 0 / -EINVAL / -ENOTSUP / -ENOSYS / -ERANGE | Mirrors Linux patterns where applicable. |

Key alignment decisions:

- Separation of CPU caches vs memory-side caches, not multiplexed through one opaque API.
- Device cache info comes from DT; introspection does not require runtime probing logic beyond driver init.
- DMA coherence helper copies Linux sequencing rules (clean inner → outer, invalidate outer → inner) without importing full dma_map_ops complexity.

Non-goals (explicitly not copied from Linux):

- Sysfs hierarchy export.
- Page table / memory-type manipulation.
- Complex partition/quality-of-service cache allocation APIs.
