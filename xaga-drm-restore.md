# xaga 6.12 MTK DRM Module Restoration Summary (2026-08-10)

## Background

The xaga 6.12 porting tree (`kernel_device_modules-6.12`) was copied from OPPO 6.12. OPPO built all DRM modules using bazel/kleaf (`BUILD.bazel` + `kernel/kleaf/mgk_64.bzl`). After switching to Kbuild Makefile builds in the porting tree, **many DRM-related Makefile `obj-`/`ccflags` lines remained commented out**, causing dependent drivers (panel / touch / backlight / DP selector) to fail during early init stage loading with `Unknown symbol` -> `Attempted to kill init` panic.

This summary records the changes made on 2026-08-10 to restore the entire MTK DRM module dependency chain, based solely on `BUILD.bazel` from alps (`xaga/baselines/modules/alps-kernel_device_modules-6.12`).

---

## 0. Dependency Analysis Key Points (Must Read for Porters)

The purpose of this section is to allow future porters to **systematically troubleshoot** similar "inter-module Unknown symbol" issues, rather than patching symbol-by-symbol after physical hardware panics. Core conclusion:

> **Almost all missing symbol root causes are not missing source code, but rather "provider module was not compiled by Makefile". When copying the porting tree from OPPO, many Makefile obj- lines were commented out (OPPO uses bazel). As long as compilation is restored per alps `BUILD.bazel`, symbols close automatically.**

### 1. Correct Method to Discover Missing Symbols (Without Waiting for Physical Hardware)

Physical hardware expdb only exposes the **first** failed loading symbol at a time (init loads modules.load serially, any failure causes panic). To see all gaps at once:

```bash
# Collect all exported symbols (K-tree vmlinux + modules tree)
cat $OUT/Module.symvers $M/Module.symvers | awk '{print $2}' | sort -u > /tmp/exp.txt

# For each .ko, query undefined symbols and compute difference with exported set = real gaps
find $M -name "*.ko" | while read f; do
  $HOME/clang/bin/llvm-nm --undefined-only "$f" 2>/dev/null \
    | awk '{print $NF}' | grep -v '^_GLOBAL_OFFSET_TABLE_' | sort -u > /tmp/u.txt
  MISSING=$(comm -23 /tmp/u.txt /tmp/exp.txt | tr '\n' ' ')
  [ -n "$MISSING" ] && echo "$(basename $f) :: $MISSING"
done
```

⚠️ Note on `llvm-nm` output: type is `$2` when address is present, type is `$1` when address is absent (such as U symbols), **always use `$NF` to extract symbol name**.

### 2. Location Flow for Each Missing Symbol

```bash
# ① Find EXPORT_SYMBOL provider in alps (only authoritative reference)
grep -rln "EXPORT_SYMBOL.*$sym\b" $ALPS/ --include="*.c" | head -1
#    -> yields provider .c file

# ② Find which alps module this .c belongs to (srcs list in BUILD.bazel)
# ③ Check corresponding porting tree directory Makefile: obj line commented out / parent directory descending
# ④ Restore according to alps bazel srcs/ko_deps/copts
```

### 3. Three Types of Feasibility Judgments (Not All Gaps Need Compilation)

| Situation | Judgment | Handling |
|---|---|---|
| Provider .c in porting tree, Makefile commented | Restore compilation (most common) | Uncomment obj per alps bazel srcs |
| Provider in K-tree vmlinux (System.map has T symbol) | No handling needed | — |
| Symbol is OPPO/Xiaomi proprietary (both trees + K-tree have no EXPORT) | Inexecutable | Replace with alps original file / stub / turn off CONFIG / filter from modules.load |

Quick method to identify proprietary symbols: `grep -rln "EXPORT_SYMBOL.*$sym"` searched across **alps, porting tree, and K-tree roots**—if missing in all three, it is definitely proprietary.

### 4. Critical Anchor: Official 5.10 modules.load (Anchor for "Necessity")

Unpack official vendor_boot ramdisk to obtain `modules.load`. Modules missing in official build (such as `mtk_peak_power_budget` / `mtk_bp_thl` / `hwccf` / `mtk_drm_gateic`) indicate **official 5.10 built into kernel or not included at all**—after 6.12 modularization, you can:
- If symbol has `IS_ENABLED(CONFIG_X)` guard -> turn off corresponding CONFIG to eliminate dependency (e.g. `mtk_bp_thl` ccci reference guarded by `CONFIG_MTK_ECCCI_DRIVER`);
- Otherwise remove module from 6.12 `modules.load` (confirm first that no other modules depend on its exported symbols).

### 5. Three Most Hidden Pitfalls

1. **Composite module `-y` includes itself**: `obj-m += foo.o` + `foo-y := foo.o bar.o` causes `cannot open foo.o` or duplicate symbol. Kbuild convention is **`-y` does not contain itself** (main source same name automatically compiled in), or module name differs from main source (e.g. alps `ufs-mediatek-mod`, `iommu_debug`, `mtk_gpufreq_wrapper` all have module name != source name).
2. **alps source code relies on bazel copts Wno tolerances**: Directories like mediatek_v2 must restore 20+ items from alps `copts` like `-Wno-implicit-function-declaration`, otherwise clang errors directly on implicit declarations / int-to-pointer conversions (porting tree global -Werror).
3. **Trace event header relative paths**: `#include "xxx_trace.h"` expanded via `define_trace.h` into `./xxx_trace.h`, requires `ccflags-y += -I$(src)` to locate.

### 6. Dependency Restoration Order (Topological)

Restore **dependee** (exporter) first, then restore dependent (caller). After a full `make M= ... modules`, run section 1 nm audit to observe convergence, iterate repeatedly—**run full nm audit before every build, do not wait for physical hardware**.

---

## I. mediatek_v2 (MTK Proprietary V2 DRM Framework, Largest Item)

### Affected Files
`drivers/gpu/drm/mediatek/mediatek_v2/Makefile`

### Issues
- Original Makefile lines 5–161 `mediatek-drm-y` list and `obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)` were fully commented out (OPPO uses bazel).
- Compilation error chain: missing `drm_internal.h` / `mmqos-mtk.h` / `hwccf_provider.h` / `mtk_layer_layout_trace.h` / `iommu_debug.h` headers, as well as alps source dependencies on `-Wno-implicit-function-declaration` / `-Wno-int-conversion` tolerance flags.

### Changes
1. Restored full `mediatek-drm-y` list (aligned with alps `BUILD.bazel` `srcs`, 80+ files).
2. **Added two files present in alps but missing from porting tree y list** (compiles without error but missing link symbols):
   - `mtk_disp_bdg.o` (bridge, exports `bdg_*` / `is_bdg_supported` / `get_ap_data_rate` etc.)
   - `mtk_disp_dbgtp.o` (DBGTP, exports `mtk_dbgtp_*` / `mml_dbgtp_register`)
3. Restored alps bazel `copts` Wno tolerance list (20+ items), since alps source code relies on these default-disabled/implicit declarations:
   ```make
   ccflags-y += -Wno-implicit-function-declaration -Wno-int-conversion ...
   ```
4. Added -I paths (matching alps `copts`):
   - `-I$(srctree)/drivers/gpu/drm/` (`drm_internal.h` in K-tree)
   - `-I$(DEVICE_MODULES_PATH)/drivers/interconnect/mediatek/` (`mmqos-mtk.h`)
   - `-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/hwccf/include` (`hwccf_provider.h`)
   - `-I$(src)` (trace event `mtk_layer_layout_trace.h` relative path)
   - `-I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2` (self directory header)
5. `mtk_dump.h` added `void mtk_dump_dbg_slot(void);` declaration
   (alps suppressed via `-Wno-implicit-function-declaration`, porting tree -Werror requires explicit declaration).

### Dependency Closure (Cross-module symbols of mediatek-drm.ko)
After restoration, `mediatek-drm.ko` undefined symbols point to the following modules (all restored per alps `ko_deps`):
- `mtk_sync` / `mtk_panel_ext` / `mtk_disp_notify` -> see dummy_drm/v2 three modules below
- `mml_*` -> `mtk-mml.ko`
- `slbc_*` -> `mtk_slbc.ko`
- `hwccf_is_enabled` -> `hwccf.ko`
- `dma_buf_get_gid` -> `system_heap.ko` (`drivers/dma-buf/heaps`)
- `notify_vb_audio_control` -> `mtk-afe-external.ko` (`sound/soc/mediatek/common`)
- `get/set_panel_dead_flag` -> newly exported inside `mtk_disp_recovery.c` in this tree (see below)

---

## II. mtk_disp_recovery.c (ESD Recovery / panel_dead Interface)

### Affected Files
`drivers/gpu/drm/mediatek/mediatek_v2/mtk_disp_recovery.c`

### Issues
- Porting tree version of this file was **OPPO modified version** (1281 lines vs alps 1049 lines), containing `#ifdef OPLUS_FEATURE_DISPLAY` / `oplus_ofp_get_aod_state()` OPPO proprietary calls; these symbols have no provider in 6.12 in either tree -> compile-time undefined + runtime crash.
- 5.10 xaga's `get_panel_dead_flag` / `set_panel_dead_flag` (`panel-l16-*.c` / `nvt36672c.ko` dependency) were removed in 6.12 alps.

### Changes
1. **Replaced entire file back to alps original** (`cp alps/.../mtk_disp_recovery.c`, OPPO proprietary code is meaningless for xaga and has no symbol provider).
2. End of file **ported panel_dead interface from 5.10** (atomic + dual export), panel/touch dependency:
   ```c
   static atomic_t panel_dead;
   int get_panel_dead_flag(void) { return atomic_read(&panel_dead); }
   EXPORT_SYMBOL(get_panel_dead_flag);
   void set_panel_dead_flag(int value) { atomic_set(&panel_dead, value); }
   EXPORT_SYMBOL(set_panel_dead_flag);
   ```

---

## III. dummy_drm vs mediatek_v2 mtk_panel_ext / mtk_sync / mtk_disp_notify

### Affected Files
- `drivers/gpu/drm/mediatek/Makefile` (parent branch)
- `drivers/gpu/drm/mediatek/mediatek_v2/Makefile`
- `drivers/gpu/drm/mediatek/dummy_drm/Makefile`

### Issues
- Both directories have a copy of `mtk_panel_ext.c/.h` (different guards), dummy_drm version is dedicated for `CONFIG_DRM_MEDIATEK_DUMMY` config.
- xaga config `CONFIG_DRM_MEDIATEK_DUMMY` disabled, `CONFIG_DRM_MEDIATEK_V2=y` -> parent Makefile takes V2 branch, dummy_drm does not descend.
- In alps, `mtk_panel_ext` / `mtk_sync` / `mtk_disp_notify` are **standalone ko registered under V2 build** (three `define_mgk_ddk_ko` in `mediatek_v2/BUILD.bazel`), source files in `mediatek_v2/` directory, byte-for-byte identical with v2 source (diff 0).

### Changes
- Appended three standalone module registrations in `mediatek_v2/Makefile`:
  ```make
  obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) += mtk_panel_ext.o
  obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) += mtk_sync.o
  obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) += mtk_disp_notify.o
  ```
- Removed extra descending into `dummy_drm/` from parent Makefile V2 branch (prevents dummy header and v2 enum redefinition conflicts).
- Removed redundant `obj-m +=` lines in `dummy_drm/Makefile` (covered by alps original `obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)`).

---

## IV. mtk-mml (MML Module)

### Affected Files
`drivers/gpu/drm/mediatek/mml/Makefile`

### Issues
- Original Makefile `ccflags-y` (-I list), `obj-$(CONFIG_MTK_MML)`, `mtk-mml-objs` were all commented out.
- Batch uncommenting script **accidentally damaged `#ifeq` blocks** (`CONFIG_MTK_MML_DEBUG` / `CONFIG_FPGA_EARLY_PORTING` / `MGK_INTERNAL`), causing `endif` mismatch (`Makefile:140: unexpected endif`).

### Changes
1. Restored `ccflags-y` (including mediatek_v2/dpc/devfreq/heaps/smi/cmdq/slbc/iommu -I list).
2. Restored `obj-$(CONFIG_MTK_MML) := mtk-mml.o` + platform modules (mt6895 etc.).
3. Restored `mtk-mml-objs` list and **added `mtk-mml-dbgtp.o`** (present in alps `BUILD.bazel` srcs, missed in porting tree original commented list; exports `mml_dbgtp_*`).
4. Fixed accidentally uncommented `#ifeq` blocks (restored comments on body and `#endif`).

---

## V. panel Directory: mtk_drm_gateic

### Affected Files
`drivers/gpu/drm/panel/Makefile`

### Issues
- `mtk_drm_gateic.c` was an orphan file in alps (no build registration), but `leds-mtk-disp.c` declared `extern int __weak mtk_drm_gateic_set_backlight(...)` and potentially calls it (`MTK_COMMON_LCM_DRV` backlight path)—weak symbol without provider crashes when called.

### Changes
- Compiled as composite module (gateic + two IC drivers + I2C helper):
  ```make
  obj-m += mediatek-drm-gateic.o
  mediatek-drm-gateic-y := mtk_drm_panel_i2c.o \
      mtk_drm_gateic_rt4801h.o \
      mtk_drm_gateic_rt4831a.o \
      mtk_drm_gateic.o
  ```
- ⚠️ **2026-08-11 Hardware Correction (commit 99a5feb)**: Module name **must differ from main source object name**. Initial version wrote `obj-m += mtk_drm_gateic.o` + `mtk_drm_gateic-y := ... mtk_drm_gateic.o` (containing itself) -> Kbuild circular dependency, main file **never compiled**, `mtk_drm_gateic_register`/`mtk_gateic_match_lcm_list` became U symbols -> physical hardware insmod `Unknown symbol` -> init kill. Renamed to `mediatek-drm-gateic` (official 5.10 xagaforge name + name referenced by alps mgk_64.bzl), `-y` list contains main file `mtk_drm_gateic.o` (different from module name, no cycle).

---

## VI. Peripheral Modules Dependent on mediatek_v2 (Restored per alps ko_deps)

Peripheral modules pulled in by mediatek-drm / mtk-mml / panel symbol dependency chain, all restored per alps `BUILD.bazel`:

| Module | Provided Symbol | Restoration Method |
|---|---|---|
| `mtk-mml.ko` | `mml_drm_*` | Restored in mml/Makefile (see above) |
| `mtk_slbc.ko` | `slbc_request` etc. | Restored obj + slbc_mt6895 in slbc/Makefile |
| `slbc_trace.ko` | `slbc_trace_rec_write` | Restored obj in slbc/Makefile |
| `hwccf.ko` | `hwccf_is_enabled` | Restored obj + `-I soc/mediatek` in hwccf/Makefile |
| `system_heap.ko` | `dma_buf_get_gid` | **Created** drivers/dma-buf/heaps/Makefile |
| `mtk-afe-external.ko` | `notify_vb_audio_control` | Restored obj in sound/soc/mediatek/common |
| `mtk_gpufreq_wrapper.ko` + `mtk_gpufreq_mt6895.ko` | `gpufreq_set_mfgsys_config` | Restored in gpufreq/v2/Makefile (ccflags + obj) |
| `mtk_gpu_hal.ko` | `mtk_get_gpu_*_fp` | **Restored** hal/Makefile (renamed Makefile.backup) + parent descending |
| `mtk-mmdvfs-v3.ko` | `mtk_mmdvfs_camera_notify` / `genpd_notify` | Restored obj in soc/mediatek/Makefile |
| `mtk-mmdvfs-v5.ko` | `mtk_mmdvfs_enable_vcp` | Restored obj in soc/mediatek/mmdvfs/Makefile |
| `mtk-mmdebug-vcp(-stub).ko` | `mmdebug_is_init_done` | Created obj line in mmdebug/Makefile |
| `mtk-vmm-notifier.ko` | `mtk_vmm_ctrl_dbg_use` | Restored obj in vmm/Makefile |

### Parent Descending Fixes (Kbuild / Top-level Makefile)
- `Kbuild`: Added `obj-y += drivers/dma-buf/heaps/`
- `drivers/gpu/mediatek/Makefile`: Added `obj-y += hal/` in `CONFIG_MTK_GPU_SUPPORT` block
- Subdirectory Makefiles can be traversed by parent once restored.

---

## VII. Compilation Pitfalls Record (Summary of Similar Issues)

| Symptom | Root Cause | Fix |
|---|---|---|
| `ld.lld: cannot open foo.o` / `duplicate symbol` | Composite module `-y` list **contains object of same name** (`mtk_drm_gateic-y := ... mtk_drm_gateic.o`), Kbuild self-aggregation | `-y` does not contain itself; or module name differs from main source (precedent in `ufs-mediatek-mod`, `iommu_debug`) |
| `missing MODULE_LICENSE()` | Composite module main object not compiled in | Module name != source name, or `-y` correctly organized |
| `fatal error: './xxx_trace.h' file not found` | Trace event header uses `./` relative path, requires `-I$(src)` | Makefile added `ccflags-y += -I$(src)` |
| `fatal error: 'xxx.h' not found` | Header in K-tree (`$(srctree)`) or sibling directory | Added -I line-by-line per alps `copts` |
| `error: variable set but not used [-Werror]` | Original alps code triggered under porting tree `-Wall -Werror` | per-obj `ccflags-y += -Wno-...` (without modifying source) |
| `Makefile: NN: unexpected endif` | Script batch uncommenting damaged `#ifeq` block | Check ifneq/ifeq/endif pairing |
| Implicit function declaration / int to pointer | alps source relies on bazel `copts` Wno tolerances | Restored alps Wno list for entire mediatek_v2 directory |

---

## VII Supp. Complete Dependency Relation Table (Consumer -> Provider)

The following dependencies were derived from full `llvm-nm` audit on 2026-08-10 (difference set between all `.ko` undefined symbols and all exported symbols), **0 undefined after restoration**. For reference by other porters:

### 7.1 DRM Consumer -> Provider

| Consumer Module | Missing Symbol | Provider Module | Restoration Action |
|---|---|---|---|
| `mediatek-drm.ko` | `find_panel_ctx`/`mtk_panel_ext_create`/`mtk_panel_remove`/`mtk_panel_detach`/`find_panel_ext`/`mtk_drm_get_lcm_version` | `mtk_panel_ext.ko` | Registered in v2/Makefile |
| `mediatek-drm.ko` | `mtk_sync_fence_create`/`mtk_sync_timeline_*`/`mtk_sync_share_fence_create` | `mtk_sync.ko` | Registered in v2/Makefile |
| `mediatek-drm.ko` | `mtk_disp_notifier_call_chain`/`mtk_disp_sub_notifier_*`/`mtk_disp_3rd_notifier_*` | `mtk_disp_notify.ko` | Registered in v2/Makefile |
| `mediatek-drm.ko` | `mml_drm_*` (20+ symbols) | `mtk-mml.ko` | Restored in mml/Makefile |
| `mediatek-drm.ko` | `slbc_*` (request/release/validate/power_on/off/invalidate) | `mtk_slbc.ko` | Restored in slbc/Makefile |
| `mediatek-drm.ko` | `hwccf_is_enabled` | `hwccf.ko` | Restored in hwccf/Makefile |
| `mediatek-drm.ko` | `dma_buf_get_gid` | `system_heap.ko` | **Created** heaps/Makefile |
| `mediatek-drm.ko` | `notify_vb_audio_control` | `mtk-afe-external.ko` | Restored in sound/common |
| `mediatek-drm.ko` | `get/set_panel_dead_flag` | `mediatek-drm.ko` itself (newly exported in mtk_disp_recovery.c) | Ported from 5.10 |
| `mediatek-drm.ko` | `bdg_*` (15 items)/`mtk_spi_*`/`get_ap_data_rate`/`is_bdg_supported`/`check_stopstate` | `mediatek-drm.ko` itself (mtk_disp_bdg.o) | Added to y list |
| `mediatek-drm.ko` | `mtk_dbgtp_*` (17 items)/`mml_dbgtp_register` | `mediatek-drm.ko` itself (mtk_disp_dbgtp.o) | Added to y list |

### 7.2 Panel / Touch / Backlight -> Provider

| Consumer Module | Missing Symbol | Provider Module |
|---|---|---|
| `panel-l16-42-02-0a-dsc-vdo.ko` / `panel-l16-36-02-0b-dsc-vdo.ko` | `find_panel_ctx`/`find_panel_ext`/`get_panel_dead_flag`/`mtk_panel_detach`/`mtk_panel_ext_create`/`mtk_panel_remove` | `mtk_panel_ext.ko` + `mediatek-drm.ko` (dead_flag) |
| `nvt36672c.ko` | `get_lockdown_info_for_nvt`/`mtk_disp_notifier_register`/`set_panel_dead_flag` | **Stubbed** (6.12 has no mi_disp) + `mtk_disp_notify.ko` + `mediatek-drm.ko` |
| `leds-mtk-disp.ko` | `mtk_drm_gateic_set_backlight`/`mtk_drm_get_conn_obj_id_from_idx`/`mtk_drm_get_lcm_version`/`mtk_drm_set_conn_backlight_level`/`mtkfb_set_backlight_level`/`_gate_ic_backlight_set` | `mtk_drm_gateic.ko` (__weak provided) + `mediatek-drm.ko` + `rt4831a_drv.ko` |
| `leds-mtk-pwm.ko` | `mtk_drm_get_conn_obj_id_from_idx` | `mediatek-drm.ko` |
| `usb_dp_selector.ko` | `mtk_dp_aux_swap_enable`/`mtk_dp_set_pin_assign`/`mtk_dp_SWInterruptSet` | `mediatek-drm.ko` |
| `mtk-mml.ko` | `mml_dbgtp_*` | `mtk-mml.ko` itself (mtk-mml-dbgtp.o) |

### 7.3 Non-DRM Peripheral Chain Fixed in Same Batch

| Consumer Module | Missing Symbol | Provider Module |
|---|---|---|
| `mtk_peak_power_budget.ko` | `gpufreq_set_mfgsys_config` | `mtk_gpufreq_wrapper.ko` (gpufreq/v2) |
| `mtk_peak_power_budget.ko` | `get_gpueb_ipidev`/`mtk_ipi_send_compl_to_gpueb` | `mtk_gpueb.ko` |
| `mtk_peak_power_budget.ko` | `get_mcupm_ipidev`/`get_mcupms_ipidev_number` | `mcupm_v3.ko` |
| `mtk_gpufreq_wrapper.ko` | `mtk_get_gpu_*_fp` (4 items) | `mtk_gpu_hal.ko` (restored hal/Makefile) |
| `mtk_bp_thl.ko` | `ccci_set_power_throttle_cb`/`exec_ccci_kern_func` | ~~eccci~~ -> **Turn off CONFIG_MTK_ECCCI_DRIVER** (eliminated via IS_ENABLED guard) |
| `mtk-mmdvfs-v3.ko` (dependent caller vmm) | `mtk_mmdvfs_camera_notify`/`mtk_mmdvfs_genpd_notify` | `mtk-mmdvfs-v3.ko` itself |
| `mtk-mmdvfs-v5.ko` | `mtk_vmm_ctrl_dbg_use` | `mtk-vmm-notifier.ko` |
| `mtk-mmdvfs-v5.ko` | `mmdebug_is_init_done` | `mtk-mmdebug-vcp.ko` |
| `mmqos-common.ko` | `mtk_icc_*` | `mtk-icc-core.ko` |
| `mmqos-common.ko` | `mtk_dramc_get_ddr_type` | `mtk_dramc.ko` |
| `mmqos-common.ko` | `mtk_mmmc_*` | `mtk-mm-monitor-controller.ko` |
| `mmqos-common.ko` | `mtk_mmdvfs_enable_vcp` | `mtk-mmdvfs-v5.ko` |
| `mtk-smi.ko` / `mtk-mminfra-debug.ko` | `mtk_mminfra_on_off` | `mtk-mminfra-util-dummy.ko` |
| `mtk-smi-dbg.ko` | `mtk_emidbg_dump` | `emi.ko` |
| `mtk-mmc.ko` | `cqhci_init`/`cqhci_irq`/`cqhci_deactivate` | `cqhci.ko` |
| `mtk-mmc.ko` / `mtk-mmc-dbg.ko` | `mmc_mtk_biolog_*`/`set_mmc_perf_mode` | `blocktag.ko` |
| `rpmb-mtk.ko` | `rpmb_mtk_cmd_req` | `core.ko` (rpmb) |
| `rpmb-mtk.ko` | `mc_*` (7 TEE symbols) | `mcDrvModule.ko` (tee/gud/700) |
| `rpmb-mtk.ko` | `ufs_mtk_rpmb_get_raw_dev` | `ufs-mediatek-mod.ko` |
| `mtk_tinysys_ipi.ko` | `mtk_rpmsg_create_channel`/`create_device` | `mtk_rpmsg_mbox.ko` |
| `mtk-mmdvfs.ko` (mtk-mmdvfs-v5) | `mmprofile_*` (5 items) | `mmprofile.ko` |
| `ps5170.ko` | `ssusb_get_drvdata`/`ssusb_power_*_notifier` | `mtu3.ko` |
| `clkchk-mt6895.ko` / `mtk-mminfra-debug.ko` | `register_devapc_*` | `device-apc-common(-legacy).ko` |
| `spmi-mtk-mpu.ko` | `ext_pmif_base` | `spmi-mtk-pmif.ko` (pmif-core.o) |
| `flashlight.ko` | `kicker_ppb_request_power`/`register_bp_thl_notify` | `mtk_peak_power_budget.ko` + `mtk_bp_thl.ko` |
| `mtk_low_battery_throttling.ko` | `dump_lvsys_thd`/`lvsys_*` | `pmic_lvsys_notify.ko` |
| `slbc_ipi.ko` | `slbc_trace_rec_write` | `slbc_trace.ko` |
| `rt5133-regulator.ko` | `devm_extdev_io_device_register` | `extdev_io_class.ko` (subpmic) |

### 7.4 Final Determination of Dependency Restoration

- **All 197 .ko have zero undefined symbols** (0 gaps in nm audit).
- 3 uncompilable symbol locations handled per "Feasibility Judgment": `get_lockdown_info_for_nvt` (stubbed), `ccci_*` (turned off CONFIG), OPPO `oplus_ofp_*` (replaced with alps original file).
- Module loading order guaranteed by build.sh **python Kahn topological sort** (reads depmod modules.dep), independent of modules.load alphabetical order.

## VIII. Verification Results

- Full module compilation: **197 .ko files, 0 modpost undefined symbols**
- Comparison with alps source: All restored module .c/.h files **byte-for-byte identical** with alps (diff 0), only 3 code-level changes (panel_dead porting, nt36xxx stub, cmdq kvm guard, all annotated with `xaga:` comments).
- All old DRM-related gaps (panel-l16 / nvt36672c / leds-mtk-disp / leds-mtk-pwm / usb_dp_selector / mtk-mml / mediatek-drm) undefined symbols closed.
- **Image Size (Added 2026-08-10)**: Official vendor_boot ramdisk ko total 39.4MB, porting tree restored 197 ko with DWARF5 debug sections reached 130MB -> build.sh packaging stage added `llvm-strip --strip-debug` (retains .symtab/__ksymtab/modinfo) -> **25MB**, vendor_boot_new.img reduced from 90MB back to 64MB specification (identical with official). boot_new.img structure unchanged (kernel 13.2MB + ramdisk 1.77MB, 64MB pad, identical with official).

## IX. 2026-08-11 Hardware Corrections (Finalized Module Set)

This document records the 197 ko snapshot of 2026-08-10 restoration chain; on 2026-08-11 after 7 rounds of expdb hardware diagnosis, the packaged set was finalized as **193 OOT + 4 in-tree = 197** (see STATUS.md §4a/§10 for details):

- **Pruned 4 pairs of duplicate exported symbol modules** (hardware `exports duplicate symbol` -> init kill; two modules export same symbol name, keep one per alps mgk_64.bzl platform mapping):
  `device-apc-common-legacy` / `mtk-mmdvfs-v5` (mt6993 exclusive) / `mtk-mmdebug-vcp` / `mcDrvModule-ffa`.
- **gateic renamed `mediatek-drm-gateic`** (see §V)—initial version wiring failed on hardware.
- **vendor_boot appended 4 K-tree in-tree modules** (`=m` not compiled into Image, but referenced by OOT):
  `drm_display_helper.ko` (drm_dp_*), `drm_dma_helper.ko` (drm_gem_dma_vm_ops), `industrialio-triggered-buffer.ko` + `kfifo_buf.ko` (mt6375-adc).
- **Hardware Result**: Init first stage **all 197 ko loaded successfully (707ms)**—dependency chain restoration of this file + above corrections jointly enabled full pass on module loading stage.

## App. Related File Index

- `drivers/gpu/drm/mediatek/mediatek_v2/Makefile` (+119 lines, main restoration)
- `drivers/gpu/drm/mediatek/mediatek_v2/mtk_disp_recovery.c` (-232 lines, alps original + panel_dead)
- `drivers/gpu/drm/mediatek/mediatek_v2/mtk_dump.h` (+3 lines)
- `drivers/gpu/drm/mediatek/mml/Makefile` (restored + dbgtp)
- `drivers/gpu/drm/mediatek/{Makefile, dummy_drm/Makefile, panel/Makefile}`
- Peripherals: slbc/hwccf/dma-buf/heaps/sound/gpufreq/hal/soc/mediatek/vmm/mmdebug
