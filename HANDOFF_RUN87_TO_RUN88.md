# Handoff & Complete Master Reference: Run 87 to Run 88
**Device:** Xiaomi Redmi Note 11T Pro / POCO X4 GT / Redmi K50i (`xaga`)  
**Platform:** MediaTek Dimensity 8100 (`mt6895`), Mali-G610 MC6 Valhall CSF  
**Target Kernel:** Linux 6.12 GKI (`6.12.23-4k-g20b279ed9c0b`)  
**Target ROM:** crDroid 12.12 / Android 14/16  
**Active Slot:** Slot A (`current-slot: a`)  
**Fastboot Serial:** `V465HMAEMVSGP7CE`  
**Date Generated:** October 23, 2026

---

## 1. Absolute Golden Rules (User Constraints)
1. **NEVER run `fastboot reboot`!**  
   The user strictly insists on **manual cold boot** (holding the physical Power button on the device). Running `fastboot reboot` is forbidden.
2. **NO CI Polling:**  
   Never poll GitHub Actions via scripts or sleep loops. The user monitors CI builds manually and triggers actions.
3. **Always Keep Full Raw Logs on Disk:**  
   Always provide full, unclipped log files with clickable markdown links (`file:///...`).
4. **Immediate Recovery First:**  
   If the device gets stuck or bootloops, immediately flash the working backup images in Fastboot before attempting further code changes.

---

## 2. Emergency Recovery Guide (Restore Known-Working Backup)

If the device bootloops, hangs, or needs to be returned to a known bootable state:
1. Put the phone into Fastboot mode (Hold Volume Down + Power).
2. Verify device is detected:
   ```bash
   fastboot devices
   ```
3. Flash the known-good working backup images to Slot A:
   ```bash
   fastboot flash boot_a /home/aditya/xaga_working_backup/boot_a_working.img
   fastboot flash vendor_boot_a /home/aditya/xaga_working_backup/vendor_boot_a_working.img
   fastboot flash dtbo_a /home/aditya/xaga_working_backup/dtbo_a_working.img
   ```
4. **DO NOT auto-reboot.** Instruct the user to perform a manual cold boot (hold Power button).

---

## 3. Key Artifact Paths on Disk

### Stock / Working Backup (Known Bootable)
* Directory: [`/home/aditya/xaga_working_backup/`](file:///home/aditya/xaga_working_backup/)
  * `boot_a_working.img` (67,108,864 bytes)
  * `vendor_boot_a_working.img` (67,108,864 bytes)
  * `dtbo_a_working.img` (33,554,432 bytes)
* Automated restore script: [`/home/aditya/restore_stock_slot_a.sh`](file:///home/aditya/restore_stock_slot_a.sh) *(Note: remove `fastboot reboot` from script if running)*

### Run 87 Flashable Images (Compiled in CI, Commit `c6e35a8`)
* Directory: [`/home/aditya/xaga-kernel-modules-6.12/build_artifacts_run87/xaga-6.12-flashable-images/`](file:///home/aditya/xaga-kernel-modules-6.12/build_artifacts_run87/xaga-6.12-flashable-images/)
  * `boot_new.img` (67,108,864 bytes)
  * `vendor_boot_new.img` (67,108,864 bytes)
  * `dtbo_new.img` (33,554,432 bytes)
* Modules Directory: [`/home/aditya/xaga-kernel-modules-6.12/build_artifacts_run87/xaga-6.12-kernel-modules/`](file:///home/aditya/xaga-kernel-modules-6.12/build_artifacts_run87/xaga-6.12-kernel-modules/)
  * `mali_kbase_mt6895.ko` (built with `mali_prepower_cores` module param support)
  * `mali_prot_alloc.ko`, `mali_mgm.ko`
* Flash script: [`/home/aditya/flash_run87.sh`](file:///home/aditya/flash_run87.sh)

### Repository Root & Build Script
* Repo Root: [`/home/aditya/xaga-kernel-modules-6.12/`](file:///home/aditya/xaga-kernel-modules-6.12/)
* Driver Source: [`/home/aditya/xaga-kernel-modules-6.12/kernel_device_modules-6.12/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/`](file:///home/aditya/xaga-kernel-modules-6.12/kernel_device_modules-6.12/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/)
* Build Script: [`/home/aditya/xaga-kernel-modules-6.12/build.sh`](file:///home/aditya/xaga-kernel-modules-6.12/build.sh)

### Captured Hardware Logs from Run 87
* Stage 2 Live Stream: [`/home/aditya/run87_stage2_20261022_055451.log`](file:///home/aditya/run87_stage2_20261022_055451.log) (817 KB)
* Full dmesg: [`/home/aditya/run87_dmesg_20261022_055451.log`](file:///home/aditya/run87_dmesg_20261022_055451.log) (818 KB)
* Full logcat: [`/home/aditya/run87_logcat_20261022_055451.log`](file:///home/aditya/run87_logcat_20261022_055451.log) (2.4 MB)
* Early boot dmesg: [`/home/aditya/run87_early_dmesg_20261022_055451.log`](file:///home/aditya/run87_early_dmesg_20261022_055451.log) (642 KB)
* Pstore dump: [`/home/aditya/run87_pstore_20261022_055451/console-ramoops-0`](file:///home/aditya/run87_pstore_20261022_055451/console-ramoops-0) (256 KB)

---

## 4. Key Facts, Breakthroughs, and Audited Truths

### Breakthrough 1: Zero GPU Faults & MMU Setup 100% Verified
* Hardware register verification at timeout:
  ```text
  AS0 CONFIG: TRANSTAB=0x000000006a402000 MEMATTR=0x00084c8d888d8f88 TRANSCFG=0x0000000062000006
  GPU_FAULT: FAULTSTATUS=0x00000000 FAULTADDR=0x0000000000000000
  MMU: RAW=0x00000000 STAT=0x00000000 MASK=0x000000ff
  L2_MMU_CONFIG=0x00800000 SNOOP_DISPARITY=ENABLED
  ```
* The legacy `0x88` (`GPU_SHAREABILITY_FAULT`) is **permanently resolved**.

### Breakthrough 2: The Mali CSF MCU is Alive and Executing Instructions
* Total memory checksum of writable sections changed between pre-boot and post-timeout:
  ```text
  [CSF SNAPSHOT TOTAL POST] Checksum=0x1c1c1a62 (PRE=0xbbfe11f6 DELTA=CHANGED across 1200128 bytes)
  [CSF SNAPSHOT IFACE POST] va=0x04000000 'shared' pages=148 csum=0xc64415d8 (PRE=0x05a72693 -> CHANGED)
  [CSF SNAPSHOT IFACE POST] va=0x0203e000 ' ' pages=1/1 csum=0x02856ed1 (PRE=0x018555aa -> CHANGED)
  ```
* In `mali_csffw.bin`, offset `0x3d018` (`words[6]`, `GLB_PRFCNT_SIZE`) is initialized to `0x00000000`. On hardware, it updated to `0x0000001b` (27 hardware counters). The MCU wrote this value.

### Fact 3: The 64-Byte Cache Line Proof (Why the Cache Theory is Dead)
* On MT6895 (Cortex-A78), CPU cache line size is 64 bytes (`0x40` bytes).
* `words[0..7]` (`+0x00` through `+0x1C`) all sit in the **exact same 64-byte cache line** (`0x04000000` to `0x0400003F`).
* Because the CPU read `words[6] = 0x0000001b` (the MCU's runtime write) from the cached mapping, that cache line was fetched from DRAM.
* In that exact same fetch, `words[0]` (`GLB_VERSION`) was `0x00000000`.
* **Conclusion:** Physical DRAM genuinely contains `0x0` for `GLB_VERSION`. The MCU has simply not written `GLB_VERSION` yet.

### Fact 4: Why the MCU Paused Before Writing `GLB_VERSION`
* Registers at timeout:
  - `MCU_STATUS = 0x00000001` (Active/Running, bit 1 `HALTED` is 0 - the MCU did NOT crash!)
  - `MCU_CONTROL = 0x00000002` (`MCU_CNTRL_AUTO`)
  - `JOB_IRQ_RAWSTAT = 0x00000000` (MCU has not asserted `JOB_IRQ_GLOBAL_IF`)
  - `GPU_IRQ_RAWSTAT = 0x00000200` (`POWER_CHANGED_SINGLE`)
  - `L2_READY_LO = 0x00000001` (L2 cache is powered)
  - `SHADER_READY_LO = 0x00000000` (**All 6 shader cores are powered OFF**)
  - `STACK_READY_LO = 0x00000000` (**Stack cores are powered OFF**)
  - `TILER_READY_LO = 0x00000000` (**Tiler is powered OFF**)
* The MCU firmware probes the shader core stacks and tiler during initialization. With all cores powered off, the MCU stalls waiting for hardware readiness before signaling global boot complete.
* The driver already contains a module parameter: `mali_prepower_cores` (line 78 of `mali_kbase_csf_firmware.c`) which powers `STACK_PWRON_LO` and `SHADER_PWRON_LO` before enabling the MCU.

### Fact 5: Why the Device Bootlooped 5–6 Times
* In `run87_logcat_20261022_055451.log`:
  Between `05:30:34` and `05:30:54` (`T = 8.5s` to `28.7s`), `SurfaceFlinger` aborted 5 consecutive times:
  `Abort message: 'no suitable EGLConfig found, giving up'`
  because `/dev/mali0` did not exist yet (Mali was kept out of `modules.load` for Option A).
* Concurrently, `android.hardware.thermal-service.mediatek` crashed every 5s because Linux 6.12's `thermal_of.c` rejected DT thermal zones without `trips` subnodes (`ret = -19`).
* Android's `init` marks `SurfaceFlinger` as a `critical` service: exceeding 4 crashes in 4 minutes triggers an automatic system reboot into bootloader.

### Fact 6: Why the Display Froze on the MI Screen
* At `T = 1.351s - 1.353s`: `clk_disable_unused` disabled `mm0_disp_*` and `mm1_disp_*` clocks before `mtk-dsi` finished binding at `T = 2.35s`.
* At `T = 2.388s`: `mtk_smi_init_power_off` cut power to the SMI bus.
* Result: `DSI0 buffer underrun` and `OVL0: frame underflow!` fired repeatedly on every scanout frame.
* **Fix:** Add `clk_ignore_unused` to the kernel boot command line.

### Fact 7: `console-ramoops-0` was Stale
* Stored crash dump was from build `#1 672ef841...` (Run 87 is `#1 79ea8f9a...`) 32 battery cycles ago. Run 87 had zero kernel panics.

---

## 5. Next Steps for Run 88

### Step A: Test Run 87 with Pre-Power (Immediate Verification)
If user wants to test current Run 87 before building Run 88:
1. Flash Run 87:
   ```bash
   fastboot flash boot_a /home/aditya/xaga-kernel-modules-6.12/build_artifacts_run87/xaga-6.12-flashable-images/boot_new.img
   fastboot flash vendor_boot_a /home/aditya/xaga-kernel-modules-6.12/build_artifacts_run87/xaga-6.12-flashable-images/vendor_boot_new.img
   fastboot flash dtbo_a /home/aditya/xaga-kernel-modules-6.12/build_artifacts_run87/xaga-6.12-flashable-images/dtbo_new.img
   ```
2. User performs manual cold boot.
3. Run [`/home/aditya/test_run87_v2.sh`](file:///home/aditya/test_run87_v2.sh):
   - Immediately stops `surfaceflinger` and `thermal-hal` (killing the 4-crash reboot timer).
   - Loads `mali_kbase_mt6895.ko mali_prepower_cores=1`.
   - Checks if `GLB_VERSION` is populated.

### Step B: Code Fixes for Run 88 (Permanent Clean Fix)
1. **Unfreeze Display Clocks:**
   Add `clk_ignore_unused` to kernel cmdline in [`build.sh`](file:///home/aditya/xaga-kernel-modules-6.12/build.sh) (and `vendor_boot_new.img`).
2. **Default Core Pre-Power in Mali Driver:**
   In [`mali_kbase_csf_firmware.c`](file:///home/aditya/xaga-kernel-modules-6.12/kernel_device_modules-6.12/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/csf/mali_kbase_csf_firmware.c#L78):
   Change `static bool mali_prepower_cores = false;` to `true`.
3. **Thermal Zones Fix:**
   Add trip point dummy subnodes to `lvts` and `thermal-ntc` in device tree or return 0 in thermal driver so `ThermalHAL` does not abort.
4. **Pack `mali_kbase_mt6895.ko` directly into `vendor_boot` `modules.load`:**
   Once core prepower is verified, boot with Mali active from frame 0 so `SurfaceFlinger` finds `/dev/mali0` immediately.
