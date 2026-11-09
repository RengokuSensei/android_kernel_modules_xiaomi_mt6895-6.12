# kernel_xiaomi_mt6895-6.12

> **Porting Status Overview:** See [STATUS.md](STATUS.md).
> **Real Device Milestone (Run 105 - November 2026):** **Full Linux 6.12 GKI kernel boots into complete Android 16 userspace on POCO X4 GT / Redmi K50i (`xaga` / `xagain`, Dimensity 8100)**! Survived **110 seconds** of continuous Android 16 userspace operation with `init`, `zygote64`, `system_server`, `DisplayManagerService`, `hwcomposer`, and `FBE` userdata decryption fully active on physical hardware.
> **DVFSRC & Low-Level Clock Alignment (Run 107):** Added `mtk-dvfsrc-start.ko` module, exposed runtime `dram_floor_opp` module parameter callback, and injected `ignore_loglevel` into kernel bootline to prevent log drops during early display and GPU clock transitions.
> **Packaging & Verification Suite:** Integrated `vrt-check.py` for vendor ramdisk table (VRT) header v4 verification into `build.sh`, added mathematical symbol closure solver (`select-modules.py`), gap auditor (`audit-modules.py`), and device tree overlay fixup tester (`check-dtb.sh`).
> **Offline Crash Diagnostics:** Integrated the XAGR printk mirror buffer patches (`docs/lk-log-patches/`) targeting the `0x7ffbf000` DRAM log_store region for recovering early-boot panics via `expdb`.
> **CSF Reset & Firmware Recovery Breakthrough (Run 105):** Eliminated the 17.3-second rwsem lock inversion deadlock between fault worker and reset worker via fast 10ms timeout bypass. Resolved the firmware reload trace buffer hang (`kmap_atomic` virtual memory freeze) via direct `interface->kernel_map` access with memory barriers. GPU soft-reset now succeeds in ~315ms!
> **Mali Valhall GPU Driver Port:** Arm Mali Valhall CSF driver suite (`mali_kbase_mt6895.ko`, `mali_prot_alloc.ko`, `mali_mgm.ko`) ported from MT6895 stock baseline to Linux 6.12 GKI kernel APIs (`vm_flags_set/clear`, `dma_resv` locking, Kbuild out-of-tree build rules) providing `/dev/mali0` for Android 16 SurfaceFlinger hardware acceleration.
> **Microtrust Beanpod TEE 520:** Ported to Linux 6.12 GKI, enabling hardware KeyMint and seamless `/data` encryption/decryption at boot.
> **Fingerprint Update:** Goodix GF3626ZS9 capacitive fingerprint driver ported to standard Linux 6.12 SPI subsystem APIs via PR #4.

---

## Current Condition

As of **Run 105** (November 2026), the porting progress on physical hardware (**POCO X4 GT / Redmi K50i / Redmi Note 11T Pro — `xaga`**, MediaTek Dimensity 8100 / `MT6895`) running **Android 16 (AresOS)** on Linux kernel **6.12 GKI**:

### 🟢 Fully Operational on Device
- **Core Kernel & Userspace Boot:** Linux 6.12 GKI boots completely into Android 16 userspace; `init`, `zygote64`, `system_server`, `DisplayManagerService`, and `hwcomposer` are fully running. Survived **110 seconds** of continuous Android 16 operation under full userspace load.
- **Hardware Cryptography & Security:** Microtrust Beanpod TEE 520 (`isee`) is active, handling KeyMint and seamlessly decrypting File-Based Encryption (FBE) on the `/data` partition during boot.
- **Display Subsystem:** MTK DRM v2, DSI, DSC, and dual L16 panels (`panel-l16`) initialize and display frames.
- **Storage Subsystem:** UFS 3.1 storage is stable with `mt6363_vufs12` and `mt6363_vufs18` always-on power regulators, eliminating all I/O errors.
- **Peripherals & Drivers:** Goodix capacitive fingerprint (`goodix_cap`) ported to 6.12 SPI APIs; touch controller (`nt36672c`), haptics, battery gauge (`bq28z610`), dual charge pumps (`ln8000`/`sc8551`), and platform power management are wired.
- **Mali CSF Infrastructure & Self-Healing Reset:** Arm Mali Valhall CSF driver suite (`mali_kbase_mt6895.ko`, `mali_mgm.ko`, `mali_prot_alloc.ko`) compiles clean, exposes `/dev/mali0`, loads CSF firmware into MCU address space (AS0), and successfully executes full GPU soft-reset (~315ms) and firmware reload recovery without deadlocks.
- **Platform Hardware Subsystem Parity:**
  - **MCUPM v2 & FHCTL:** GPU power domains and clock calibration initialize cleanly with probe deferral and IPI dependency ordering matching stock.
  - **GPUEB:** GPU Event Bus stride and recv_table 4-element alignment verified.
  - **DVFS & Thermal:** MediaTek `gpufreq_v2` and Gas Engine (`ged.ko`) actively monitor GPU thermal states.

### 🟡 Active Focus (Run 105 → Run 106)
- **1. Mali Valhall GPU Shareability Fault (`GPU_SHAREABILITY_FAULT 0x88`):**
  - **Issue:** SurfaceFlinger / RenderEngine triggers `GPU_SHAREABILITY_FAULT` (exception `0x88`, AS1, address `0x98000000ff`) when mapping GPU buffers (`PTE SH=3 ATTR=0`).
  - **Current State:** The kernel survives the initial wedge because Run 105 fixed the 17.3s rwsem lock inversion deadlock and firmware reload trace buffer hang, allowing clean soft-reset and firmware reload. However, repeated faults trigger a reset loop until Android `watchdogd` / `init` restarts userspace after ~110 seconds.
  - **Run 106 Investigation:** Aligning page table entry shareability attributes (`SH=3` vs `SH=0`), verifying `AS_MEMATTR` register programming against Stock 5.10, confirming `TRANSCFG` page table walk configuration, and auditing MediaTek ACP/CCU bus protection registers.
- **2. Early USB Gadget Driver Stability (`android_set_disconnected`):**
  - **Issue:** Connecting or cycling USB cable during early kernel boot ($t \approx 2.9$s) causes a NULL pointer dereference in `android_set_disconnected` at offset `0x1b4` if the ConfigFS gadget state machine has not finalized.
  - **Run 106 Fix:** Add NULL pointer guard checks in the USB gadget disconnect handler.

---

## Overview

**Android 6.12 Out-of-Tree Kernel Modules Port Tree** for `xaga` (Redmi Note 11T Pro / POCO X4 GT / Redmi K50i, MediaTek Dimensity 8100 / `MT6895`):

- **Base Tree:** OPPO 6.12 MediaTek module tree (`kernel_device_modules-6.12`, Kleaf/mgk build model)
- **Porting Source:** Xiaomi 5.10 ESK kernel (`../baselines/kernel/kernel_xiaomi_mt6895`, `16.2-rebase`) & official 5.10 kernel (`xaga-s-oss`)
- **Kernel Version:** Linux 6.12 (GKI Common + MTK mgk rules)
- **Paired Kernel Source:** OPPO 6.12 Kernel Base (`../android_kernel_oddo_mt6895`, version 6.12.23)

---

## Directory Structure

- `kernel_device_modules-6.12/` — Main module tree (Kleaf/BUILD.bazel + Kbuild dual build paths)
- `scripts/` — Tooling suite: `vrt-check.py` (VRT validation), `select-modules.py` (symbol closure), `audit-modules.py` (gap audit), `gen-modules-load.py` (topological load order), `check-dtb.sh` (DTBO fixup validation), `live_logger.py`
- `docs/lk-log-patches/` — Complete XAGR ring buffer patches for early-boot LK log persistence
- `STATUS.md` — Porting status overview (progress, build steps, known gaps, next steps)
- `BRINGUP.md` — Bring-up guide (build recipes, power-on sequences, charging/display/touch alignment)
- `xaga-drm-restore.md` — MTK DRM dependency chain recovery guide
- `xaga-log-capture.md` — Log capture methods (XAGR ring buffer, expdb dumping, panic debugging)
- `README.md` — This file (repository overview, ported components, known gaps)

---

## Upstream References

Portions of this codebase originate from the following public repositories:

- **[XagaForge/android_kernel_xiaomi_mt6895](https://github.com/XagaForge/android_kernel_xiaomi_mt6895)** (Community GKI 5.10 kernel for xaga) — Source for board DTS and Xiaomi drivers (`panel-l16`, touch, charger, haptics OOT drivers).
- **[ramabondanp/alps-kernel_device_modules-6.12](https://github.com/ramabondanp/alps-kernel_device_modules-6.12)** (MTK ALPS 6.12 OOT module tree) — Reference for MTK platform modules, DRM dependency chains, and `BUILD.bazel`/`mgk_64.bzl` mappings.

Other base sources: OPPO 6.12 kernel and module trees (`android_kernel_oddo_mt6895`), official Xiaomi 5.10 kernel base, and LineageOS xaga source.

---

## Building

This tree can be built locally using `./build.sh` or automatically in the cloud via **GitHub Actions** (`.github/workflows/ci-build.yml`).

### One-Click Build & Packaging: `./build.sh`

In a complete workspace containing the paired kernel (`../android_kernel_oddo_mt6895`), `build.sh` executes the full compile and packaging flow:

1. **Config:** `gki_defconfig` + `mgk_64_k612_defconfig` + `vendor/xaga.config`
2. **Kernel Image:** `Image` + `Image.gz` (gzip)
3. **In-tree Modules:** Generates/refreshes `Module.symvers`
4. **Out-of-Tree Modules:** Compiles **200+ `.ko` modules** (`make M=`)
5. **DTS Compilation:** `mt6895.dtb` (SoC base) + `xaga.dtbo` / `xaga_global.dtbo` (verified via `fdtoverlay`)
6. **Images Packed:**
   - `images/out/boot_new.img` — `magiskboot -n`: `Image.gz` kernel + KernelSU
   - `images/out/vendor_boot_new.img` — `mkbootimg`: official vendor ramdisk populated with 6.12 `.ko` modules (stripped via `llvm-strip --strip-debug` to ~25MB) + rebuilt `modules.load`/`modules.dep`
   - `images/out/dtbo_new.img` — DTOv1 format

```bash
./build.sh                        # Default: Full compilation + packaging (outputs to images/out/)
./build.sh --modules-only         # Compile kernel modules only (200+ .ko files), no boot packaging
./build.sh --no-clean             # Incremental compilation (does not clean previous objects)
./build.sh --skip=BOOT,VENDOR     # Compile but skip specific image packaging steps
./build.sh --help
```

### Environment Overrides:
- `K`: Path to base 6.12 kernel tree (`android_kernel_oddo_mt6895`)
- `M`: Path to modules tree (`./kernel_device_modules-6.12`)
- `LLVM_PREFIX`: Path to AOSP Clang toolchain (e.g., `$HOME/clang`, **AOSP clang-r536225** recommended)
- `JOBS`: Number of parallel compile jobs (defaults to `nproc`)

---

## Ported Components (from 5.10 Baselines)

All drivers are registered in Kleaf's `kernel/kleaf/mgk_64.bzl` and `BUILD.bazel`:

| Module | Description |
|---|---|
| **goodix_cap** | Goodix GF3626ZS9 capacitive fingerprint driver (ported to Linux 6.12 SPI APIs) |
| **simtray** | SIM tray status detection |
| **hwid** | `/sys/hwid` hardware board identification |
| **xiaomi_touch + double_click** | Xiaomi touch framework + double-tap-to-wake |
| **NVT36672C** | Novatek SPI touchscreen (adapted for Linux 6.12) |
| **aw8697 haptic** | Awinic linear haptic motor |
| **leds-ktz8863a + panel-l16 (x2)** | Backlight controller + dual L16 DSC display panels with ESD recovery |
| **ln8000 / sc8551 / sc8561 / bq28z610** | Dual charge pump ICs + battery fuel gauge (`bms` power supply) |
| **pd_cp_manager + Charging Framework** | Complete `mtk_charger`/`mtk_pd_*` suite (`mtk-master-charger` alignment) |
| **6x Camera Sensors** | `s5khm2`, `s5k4h7`, `ov16a1`, `s5kgw1`, `gc02m1`, `ov02b10` (`src-v4l2`) |
| **KTD2687 Flashlight** | Dual-LED camera flash driver + core flashlight subsystem wiring |
| **MTK Pump Express** | `pep`/`pep20`/`pep40`/`pep45`/`pep50`/`pep50p` fast charging protocols |
| **MTK Platform Modules (123 ko)** | Complete platform module suite synchronized from ALPS tree |
| **MTK DRM Subsystem (60+ modules)** | `mediatek_v2` (80+ files), `mml`, `gpufreq v2`, `slbc`, `system_heap`, `mmdvfs`, `vmm` (0 undefined symbols) |
| **Microtrust Beanpod TEE 520 (`isee`)** | Beanpod KeyMint & Userdata Decryption driver ported to Linux 6.12 GKI |
| **Mali Valhall GPU (`mali_kbase_mt6895`, `mali_prot_alloc`, `mali_mgm`)** | Dimensity 8100 Mali-G610 MC6 CSF driver ported to Linux 6.12 GKI with MT6895 DVFS, power management, and Android 16 HAL compatibility (`r32p1-01eac0`) |

### Mali Valhall CSF GPU Driver Architecture (Linux 6.12 GKI)
- **CSF Backend:** Direct Command Stream Frontend (`MALI_USE_CSF=1`) hardware scheduler support for Valhall architecture (Mali-G610 MC6 / Dimensity 8100).
- **VMA & Memory Subsystem:** Converted memory allocations to Linux 6.12 `vm_flags_set()` and `vm_flags_clear()`, with `dma_resv_lock()` wrapped around DMA-BUF attachments. Migrated VMA unmapped area lookups in `thirdparty/mali_kbase_mmap.c` to native `current->mm->get_unmapped_area()` under Linux 6.1+ Maple Tree (`mm_mt`).
- **Kernel MM & Driver Core Modernization:** Adapted user page pinning (`pin_user_pages_remote`, `pin_user_pages`), eliminated removed `__GFP_ATOMIC` flag references, handled per-process RSS tracking for 6.2+ `percpu_counter rss_stat`, safely guarded removed `register_shrinker()` APIs, hardened `kthread_create()` against format-security warnings, updated `dev_pm_opp_set_regulators()` to NULL-terminated arrays with token tracking, adapted `dma_set_max_seg_size()` for Linux 6.6+ `void` return semantics, and provided `strlcpy` compatibility helper for Linux 6.8+ GKI.
- **Power Management & DVFS:** Linked with MediaTek `gpufreq_v2` using `GPU_PWR_ON` and `GPU_PWR_OFF` state transitions, asynchronous power callbacks, and MTCMOS/SPM integration.
- **Allocation & Dependency Order:** Automated `modules.dep` sequencing ensuring `mali_prot_alloc.ko`, `mali_mgm.ko`, `mtk_gpufreq_wrapper.ko`, `mtk_gpufreq_mt6895.ko`, `gpu_bm.ko`, and `ged.ko` initialize before `mali_kbase_mt6895.ko` to create `/dev/mali0`.

---

## Hardware ⟷ Kernel Driver ⟷ Firmware Map (Dimensity 8100 / MT6895)

On MediaTek platforms, the GPU does not operate in isolation—it is part of a tightly coupled co-processor ecosystem managed via firmware mailboxes, interconnect QoS, and real-time power arbiters.

| Hardware Block | Physical Hardware Role | Kernel Modules / Drivers | Firmware / Interface |
| :--- | :--- | :--- | :--- |
| **Arm Mali-G610 MC6** | GPU 3D rendering, compute engines, job queues, shader cores | `mali_kbase_mt6895.ko`<br>`mali_mgm.ko`<br>`mali_prot_alloc.ko` | Direct MMIO registers + IRQ lines mapped in `xaga-mt6895.dtsi` |
| **SSPM** *(Smart System Power Manager)* | Dedicated **Cortex-M4** hardware co-processor managing real-time low-power states, DRAM bandwidth limits, and GPU frequency bounding | `tinysys-scmi.ko`<br>`sspm_v3.ko`<br>`mtk_tinysys_ipi.ko` | **`sspm.img`**<br>Communicates via ARM SCMI mailbox and shared SYSRAM buffer |
| **GPUEB** *(GPU Event Bus)* | Hardware performance counter bus monitoring GPU frame execution time, load bursts, and thermal output | `mtk_gpueb.ko`<br>`ged.ko` (`src/ged_eb.c`) | Direct interconnect bus registers |
| **DVFSRC & MT6315 PMIC** | Hardware Dynamic Voltage/Frequency Scaling Resource Collector + Buck Regulator for `V_GPU` power rails | `mtk-dvfsrc*.ko`<br>`mtk_gpufreq_mt6895.ko`<br>`mtk_gpu_hal.ko` | Controls `VGPU_DVFS` and `VGPU_SRAM` power domains |
| **EMI / SMI** *(Ext. Memory Interface & Smart Multimedia Interface)* | LPDDR5 memory controller, QoS bus arbitration, memory bandwidth throttling | `mtk_qos.ko`<br>`mtk_gpu_qos.ko`<br>`gpu_bm.c` | Shares bandwidth tables with SSPM over SCMI (`qos_ipi_to_sspm_scmi_command`) |
| **MTK IOMMU & EMI MPU** | Address translation (MMU-500) and hardware memory firewall protecting GPU buffers and DRM secure memory | `mtk_iommu.ko`<br>`emimpu.ko`<br>`system_heap.ko` | Hardware Translation Lookaside Buffers (TLB) |
| **Gas Engine / Orchestration** | MediaTek proprietary user-space $\leftrightarrow$ kernel arbitration layer (FPSGO, frame fence sync, thermal back-off) | `ged.ko` | `/dev/ged` interface exposed to Android `SurfaceFlinger` and `libGLES_mali.so` |

### Deterministic Initialization Sequence

```mermaid
graph TD
    A["1. Hardware Mailbox & Clocks (tinysys-scmi.ko)"] --> B["2. SSPM Co-Processor Buffer (sspm_v3.ko)"]
    B --> C["3. Bus Bandwidth QoS Engine (mtk_qos.ko)"]
    C --> D["4. GPU QoS Arbitrator (mtk_gpu_qos.ko)"]
    D --> E["5. MediaTek Gas Engine Driver (ged.ko)"]
    E --> F["6. Arm Mali GPU Driver (mali_kbase_mt6895.ko)"]
    F --> G["7. /dev/mali0 created -> Android UI (SurfaceFlinger)"]
```

---

## Project Status

### ✅ Completed

| Item | Details |
|------|---------|
| **200+ Kernel Modules** | All OOT modules compile clean (`-Werror`), 0 undefined symbols |
| **CI/CD Pipeline** | GitHub Actions: automated cloud compilation on every push |
| **Full Kernel Packaging** | CI supports full `Image.gz` + flashable `boot.img`/`vendor_boot.img`/`dtbo.img` packaging |
| **Fingerprint Driver** | Goodix GF3626ZS9 ported to Linux 6.12 standard SPI APIs (PR #4, merged) |
| **Display DRM Handoff** | Early boot display pipeline fix — MTCMOS power-on ordering + DSI clock preparation |
| **Vendor Firmware Staging** | `vendor_firmware/` directory structure + `build.sh` auto-staging for `gz`/`tee`/`sspm`/`mcupm`/`pi_img` |
| **Recovery Boot** | Verified on real hardware: boots into recovery with working display |
| **UFS Power Stability** | `mt6363_vufs12` & `mt6363_vufs18` configured `regulator-always-on`, eliminating all I/O errors |
| **Full Android 16 Userspace Boot** | Verified on physical device: boots Android 16, decrypts FBE `/data`, runs `zygote64`, `system_server`, `DisplayManagerService`, and `hwcomposer` |
| **Mali Valhall Driver Port** | `mali_kbase_mt6895.ko`, `mali_prot_alloc.ko`, `mali_mgm.ko` ported to Linux 6.12 GKI with MT6895 platform wiring |
| **Android 16 VABC Ramdisk** | Upgraded base boot asset from Android 12 GSI to native Android 16 with `snapuserd` (Closes #7) |
| **Camera Sensor Build Rules** | 6 xaga camera sensor drivers registered in Kbuild & Bazel build systems (Closes #6) |
| **Documentation** | Fully translated to English (`STATUS.md`, `BRINGUP.md`, `xaga-drm-restore.md`, `xaga-log-capture.md`) |
| **110s Device Uptime Milestone (Run 105)** | Verified on physical hardware: survived 110s continuous Android 16 uptime under active RenderEngine load |
| **DVFSRC & Clock Alignment (Run 107)** | Added `mtk-dvfsrc-start.ko`, exposed runtime `dram_floor_opp` module parameter, and injected `ignore_loglevel` |
| **Packaging & Verification Suite** | Integrated `vrt-check.py` into `build.sh` (validates header v4 VRT offsets & legacy LZ4 streams); added `select-modules.py`, `audit-modules.py`, and `check-dtb.sh` |
| **XAGR Early LK Crash Persistence** | Preserved full patchset (`0001`–`0005`) in `docs/lk-log-patches/` for `0x7ffbf000` DRAM log_store recovery via `expdb` |
| **CSF Reset Deadlock Resolution (Run 105)** | Eliminated 17.3s rwsem lock inversion between fault worker and reset worker via 10ms timeout bypass |
| **Firmware Reload Recovery (Run 105)** | Fixed `kmap_atomic` virtual memory freeze in trace buffer reload via direct mapping & `dmb(ish)` memory barriers |
| **GPUEB & FHCTL Parity (Run 102-103)** | Resolved GPUEB 4-element stride mismatch and clock FHCTL probe deferral & memory caching |
| **Native ACE-Lite Coherency (Run 86)** | Configured native `COHERENCY_ACE_LITE` (mode 0) and ACP MMIO sequences matching Dimensity 8100 hardware |

### 🔧 Remaining Gaps

| # | Gap | Severity | Notes |
|---|-----|----------|-------|
| 1 | **GPU Shareability Fault (0x88)** | Critical | SurfaceFlinger buffer mapping triggers `GPU_SHAREABILITY_FAULT` (AS1, PTE SH=3); resolving PTE/MEMATTR/coherency |
| 2 | **Early USB Gadget Null Guard** | High | NULL pointer dereference in `android_set_disconnected` during cable attachment at $t \approx 2.9$s |
| 3 | **SurfaceFlinger 3D Composition** | In Progress | Blocked on shareability fault resolution; once solved, UI composition will transition from software/HWC fallback to full Mali 3D HWUI |
| 4 | **Fingerprint TEE daemon** | Non-blocking | `goodix_cap` kernel driver is ported; needs matching TEE user-space daemon + real hardware testing |
| 5 | **Touchscreen Firmware** | Needs device | `nt36672c` firmware binary must be placed in `/vendor/firmware` partition |
| 6 | **Audio & Camera Subsystems** | Phase 3 | Register MT6368/MT6363 audio codec and test camera sensors with V4L2 stack |

### 🗺️ Project Roadmap & Next Steps

#### Phase 1: GPU Shareability Fault Elimination (Current - Run 106)
- **PTE & Memory Attributes:** Audit page table entry shareability bits (`SH=3` vs `SH=0`) and memory attribute indices (`ATTR=0`) mapped by RenderEngine.
- **AS_MEMATTR & TRANSCFG:** Verify `AS_MEMATTR` register programming and Outer-Shareable PTW configuration against Stock 5.10.
- **Bus Protection & Snoop Disparity:** Validate `L2_MMU_CONFIG` snoop disparity settings and verify MFG bus protect lines before issuing resets.
- **Early USB Guard:** Implement NULL pointer safety checks in `android_set_disconnected` in the USB gadget driver to prevent early-boot panics.

#### Phase 2: SurfaceFlinger 3D HWUI Acceleration & Display Smoothing
- Achieve persistent, crash-free SurfaceFlinger composition with zero GPU resets.
- Verify SkiaVk / SkiaGL pipeline execution and hardware buffer flips.
- Enable MediaTek Gas Engine (`ged.ko`) frame pacing and FPSGO dynamic frequency scaling.

#### Phase 3: Multimedia & Peripheral Validation
- **Audio Bringup:** ALSA SoC machine driver, MT6368/MT6363 codec integration, and audio routing.
- **Camera Bringup:** Sensor probing, V4L2 camera framework, and ISP clocking for all 6 camera sensors.
- **Wireless Connectivity:** RIL, Wi-Fi 6, and Bluetooth 5.3 stack validation.

---

## License

Kernel and modules are licensed under the GNU General Public License (GPL) as declared in their respective source files.
