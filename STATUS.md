# xaga 6.12 Porting Status Overview

> **Status Baseline**: commit `30c80ea` (2026-08-13, branch `main`, pushed to origin/main; history prior to `a05b916` baseline was rebased, see §9 for old hashes)
> **Note**: Commit history was rewritten via rebase, all hashes referenced in older documentation are invalid; see §9 for old/new cross-reference.
> **Hardware Milestone (2026-08-13)**: After closing the display pipeline root cause (PWM0/SPR0 compatible), **compiled artifacts can enter recovery with normal display** (no longer atomic oops / display size 0x0). **Booting into system (normal Android boot) requires blob support**—this repository contains only kernel + GPL module sources, full system requires Xiaomi/MTK proprietary blobs (TEE/gz/mcupm/sspm firmware, vendor partition proprietary binaries such as camera HAL/fingerprint TEE app, etc.), see §6.7.
> **Hardware Phenomenon (2026-08-13, described only, not fixed)**: **Screen shows corruption during early boot process (LK->early kernel startup), display restores to normal after entering recovery**—corruption occurs during early display initialization stage, does not affect recovery display, temporarily untreated (non-blocking).

## 1. Project Positioning and Tree Structure

**Android 6.12 kernel module porting tree** for xaga (Redmi Note 11T Pro / POCO X4 GT / Redmi K50i, MT6895 / Dimensity 8100) — porting board DTS and OOT drivers from Xiaomi 5.10 ESK to the 6.12 MTK module tree (`kernel_device_modules-6.12`, kleaf/mgk build model, base tree OPPO `android_kernel_oddo_mt6895`).

```
xaga/kernel_xiaomi_mt6895-6.12/
├── STATUS.md / BRINGUP.md / README.md / xaga-drm-restore.md / xaga-log-capture.md
│                                   # Status overview / bring-up / overview / DRM dependency restoration / log capture
└── kernel_device_modules-6.12/
    ├── arch/arm64/boot/dts/mediatek/          # xaga*.dts board chain (overlay mt6895.dts)
    ├── arch/arm64/configs/vendor/xaga.config  # defconfig fragment
    ├── drivers/...                            # Ported Xiaomi OOT drivers
    ├── kernel/kleaf/mgk_64.bzl                # mgk module registry
    └── vendor/mediatek/...                    # sensor source + MTK platform modules
```

**Not included in this tree** (requires user complete MTK/AOSP environment): GKI common kernel (OPPO 6.12.23), `kernel/build` mgk bazel rules, clang toolchain, complete `vendor/mediatek`. This repository cannot be built standalone.

Reference baselines (workspace `xaga/baselines/`): Kernel reference baseline = `baselines/kernel/` (offical/xagaforge/lineage_xaga); Kernel module reference baseline = `baselines/modules/` (alps 6.12 / redmi 5.10 GKI) + `baselines/kernel/xagaforge` (dual role).

## 2. Porting Progress Overview

| Category | Status | Description |
|---|---|---|
| Xiaomi OOT drivers (22 objects) | ✅ Completed | All registered in mgk_64.bzl + BUILD.bazel, compiles clean with -Werror |
| MTK platform modules | ✅ Completed (Local build) | 123 ko (1409327 initial 118 + 5829818 completed) |
| **MTK DRM full dependency chain** | ✅ **Completed (2026-08-10, expanded to 200 ko)** | 0 undefined; ~60 modules restored including mediatek_v2/mml/gpufreq/slbc/hwccf, see `xaga-drm-restore.md` |
| Camera sensor ×6 | ✅ Completed (Syntax verified) | src-v4l2; requires appending 6 names to BUILD.bazel in user environment (§6.3) |
| KTD2687 flash + flashlight wiring | ✅ Completed | drivers/misc/mediatek/flashlight/ (e321232/1aadba1) |
| MTK Pump Express | ✅ Completed | mtk_pep* (pep/pep20/pep40/pep45/pep50/pep50p, 4b2d268) |
| Board DTS chain | ✅ Completed (Statically verified) | 210 label references fully resolved, cpp-preprocess clean; **⚠️ DTS Makefile 0 xaga registrations**, DTBO list must be supplemented in user environment mgk rules (§6.4) |
| defconfig (xaga.config) | ✅ Completed | Key symbols see §3 |
| Full build + packaging | ✅ Locally buildable | Artifacts see §4; user environment needs to rerun full integration |
| Fingerprint goodix_cap | ✅ Completed | Ported to standard Linux 6.12 SPI subsystem APIs (`drivers/input/fingerprint/goodix_cap/`), replacing legacy 5.10 `mtk_spi.h` |
| Hardware verification | ✅ **Recovery bootable, display normal (2026-08-13)** | After closing display pipeline root cause (PWM0/SPR0 compatible -> crtc0 not created), recovery boots normally; **Booting into system requires blob support (§6.7)** |

## 3. Configuration (xaga.config Key Symbols)

| Symbol | Value | Description |
|---|---|---|
| XMEXT_LN8000 / SC8551A_CHG_PUMP | =m | Dual charge pumps ln8000/sc8551 |
| XMEXT_TI_GAUGE | =m | bq28z610 fuel gauge ("bms" psy) |
| XM_PD_MANAGER | =m | Charging algorithm manager |
| DRM_PANEL_L16_* / DRM_PANEL_LEDS_KTZ8863A | =m | Two L16 DSC panels + backlight |
| MI_DISP_ESD_CHECK | =y | Display ESD recovery |
| TOUCHSCREEN_NVT36672C_HOSTDL_SPI | =m | Novatek SPI touch |
| TOUCHSCREEN_XIAOMI_TOUCHFEATURE / DOUBLE_CLICK | =m | Touch feature class + double-tap wake |
| SIMTRAY_STATUS / MI_HARDWARE_ID | =m | Simtray / /sys/hwid |
| INPUT_AW8697_HAPTIC | =m | Linear haptic motor |
| MTK_VIDEO_KTD2687 | =m | Camera flash |
| CUSTOM_KERNEL_IMGSENSOR | 6 items | Sensor name list (s5khm2/s5k4h7/ov16a1/s5kgw1/gc02m1/ov02b10) |
| XAGA_MARKER_WRITER | =y | Boot-stage marker (XAGR ring writes log_store 0x7ffbf000 (minirdump triggers mrdump reboot), hang location; **Reading = direct inspection of expdb dump**, lineage_xaga reader code for backup only) |

## 4. Build Status

**Artifacts** (2026-08-11, local `./build.sh` full build): `Image.gz` (13.3MB) + **193 OOT module `.ko` files** + `mt6895.dtb` / `xaga.dtbo` / `xaga_global.dtbo` + three flashable images (`boot_new.img` / `vendor_boot_new.img` / `dtbo_new.img`, see §4a). Single full build takes ~4 minutes (32 cores), with timestamps/step counts/progress bar output (since 2026-08-11).

### 4a. Packaged Image Set Composition (Finalized 2026-08-13)

- **204 ko into vendor_boot = 200 OOT + 4 in-tree**: OOT modules come from M-tree `make M=` (200, build.sh hard assertion; expanded from 197 to 200 on 2026-08-13, restoring eMMC/cache providers); 4 in-tree (K-tree) modules are symbol providers for OOT, `=m` in 6.12 (not compiled into Image), must be packaged into vendor_boot:
  - `drm_display_helper.ko` (`drm_dp_*`, for mediatek-drm) -> `CONFIG_DRM_DISPLAY_HELPER=m`
  - `drm_dma_helper.ko` (`drm_gem_dma_vm_ops`) -> `CONFIG_DRM_GEM_DMA_HELPER=m`
  - `industrialio-triggered-buffer.ko` + `kfifo_buf.ko` (for mt6375-adc)
- **Module Pruning (2026-08-11)**: 4 pairs of "dual module duplicate exported symbol" conflicts (hardware `exports duplicate symbol` -> init kill) kept one each per alps platform mapping:
  - `device-apc-common-legacy` (old SoC v1 interface) removed / kept `device-apc-common` (multi-ao)
  - `mtk-mmdvfs-v5` (mt6993 exclusive) removed / kept `mtk-mmdvfs-v3` (mt6895)
  - `mtk-mmdebug-vcp` (real implementation) removed / kept `mtk-mmdebug-vcp-stub` (only one referenced by ko_deps)
  - `mcDrvModule-ffa` (FF-A transport) removed / kept `mcDrvModule` (same as official 5.10)
- **`mtk_drm_gateic` -> `mediatek-drm-gateic`** (2026-08-11): Composite module name must differ from main source object name (Kbuild circular dependency), adopted official 5.10 (xagaforge) and alps mgk_64.bzl names; `-y` list contains main file `mtk_drm_gateic.o`.

**Module Coverage (Official 5.10 ramdisk 198 modules -> 6.12, 2026-08-08/10 unpack comparison)**: Official 198 5.10 version .ko files (vermagic 5.10.198) cannot be loaded on 6.12 kernel, fully covered by 6.12 side in 4 layers, **no hard gaps**:
- ① Direct same-name replacement (109): 109 out of 123 packaged modules share same name with official (bq28z610/mtk_wdt/phy-mtk-ufs/pinctrl-mt6895 etc.)
- ② Renamed/merged replacement: clk-chk->clkchk, pinctrl-mtk-v2->pinctrl-mtk-common-v2, mt6375-battery->mt6375-gauge, mtk_mm_heap->mtk_system_heap, fan53870->fan53870-ldo, wl2868c->wl2868c-regulator, emi series->memory/mediatek, mtk_pep* (= mtk_pe*.o combination)
- ③ Built into kernel (~30, no .ko needed): mediatek-drm* (DRM_MEDIATEK_V2=y), mtk-mmc-autok (built into mtk-mmc.c), regmap-spmi/reboot-mode/zsmalloc/system_heap, industrialio/kfifo_buf/mac80211/cfg80211 (upstream)
- ④ Debug/diagnostic omitted; **only non-counterpart = mi-memory** (Xiaomi proprietary, missing in all three trees, non-essential for boot)

⚠️ **Historical Snapshot Note**: 123 ko is 2026-08-07 packaging snapshot; starting 2026-08-10 packaging set expanded to 197 ko (restoring DRM/typec/gpufreq dependency chain); finalized on 2026-08-11 as 193 OOT + 4 in-tree = 197 (see §4a).

**modpost undefined**: **Cleared starting 2026-08-10**. Previously vendor/mediatek cross-referencing MTK typec/tcpc (`tcpm_*`, `tcpc_dev_*`) resolved alongside tcpc_class module restoration; all 200 ko audited via `llvm-nm` with 0 undefined.

**Build Recipe Key Points** (error-prone, details see BRINGUP.md §1):
- Every `make` must carry `KCONFIG_EXT_PREFIX=<modules>/`, otherwise syncconfig drops module symbols
- Config merge uses `merge_config.sh -m` (append-only semantics; KCONFIG_ALLCONFIG unsupported by this conf)
- `CONFIG_MODULE_SIG_KEY` must be an absolute pem path
- Need to restore classic Kbuild wiring missing in OPPO tree (top-level obj-y += drivers/misc + drivers/input/misc; power/supply/Makefile MTK_CHARGER framework lines; panel Makefile mediatek_v2 include paths); on 2026-08-10 completed parent descending (iommu/tinysys/vcp/mmqos/blocktag/usb/dma-buf-heaps/hal etc., details see xaga-drm-restore.md §VI)
- Packaging stage performs `llvm-strip --strip-debug` on .ko (retains .symtab/__ksymtab/modinfo)—official ko has no DWARF (39.4MB), porting tree carries DWARF5 (130MB), 25MB after strip (2026-08-10)

**Real 6.12 API Differences Discovered During Build (Fixed)**: i2c probe 1 parameter, remove->void, FW_ACTION_HOTPLUG removed, GPIOF_DIR_IN->GPIOF_IN, devm_gpio_free->gpio_free, of_get_named_gpio_flags missing, spi_device.master removed, MTK_PD_CONNECT enum deduplication (6.12's mtk_pd_connect_type promoted to adapter_class.h), bq28z610 time_init->fg_time_init + night_charging deprecated, pd_cp_manager helper functions made file-static, added vmalloc/pinctrl includes. Additionally on 2026-08-10: cmdq-util.c kvm include guard, nt36xxx.c stubbed get_lockdown_info_for_nvt, mtk_disp_recovery.c swapped to alps original + panel_dead ported, mtk_dump.h added declaration.

## 5. Ported Driver List

| Group | Driver | Path |
|---|---|---|
| Input/Misc | simtray (gpiod rewritten), hwid, double_click, xiaomi_touch, NVT36672C (nt36xxx SPI), aw8697_haptic, goodix_cap (gf3626zs9 SPI) | `drivers/misc/simtray.c`, `drivers/misc/hwid/`, `drivers/input/double_click.c`, `drivers/input/xiaomi/`, `drivers/input/touchscreen/NVT36672C/`, `drivers/input/misc/aw8697_haptic/`, `drivers/input/fingerprint/goodix_cap/` |
| Charging/Power | ln8000, sc8551, sc8561, bq28z610, pd_cp_manager, pmic_voter, charger_class, adapter_class, full mtk_charger framework set, mtk_pd_adapter, mtk_chg_type_det, MTK Pump Express (mtk_pep*) | `drivers/power/supply/` |
| Display | panel-l16-42-02-0a / -36-02-0b-dsc-vdo, leds-ktz8863a backlight | `drivers/gpu/drm/panel/` |
| **DRM Framework Chain (Restored 2026-08-10)** | mediatek_v2 (mediatek-drm 80+ files), mtk_panel_ext/mtk_sync/mtk_disp_notify, mtk-mml, mtk_drm_gateic | `drivers/gpu/drm/mediatek/{mediatek_v2,mml,panel}/` (details see xaga-drm-restore.md) |
| Camera | 6 sensors (s5khm2/s5k4h7/ov16a1/s5kgw1/gc02m1/ov02b10), KTD2687 flash | `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xaga*/`, `drivers/misc/mediatek/flashlight/` |
| MTK Platform Modules | 200 ko (including DRM dependency chain) | `drivers/misc/mediatek/` + `vendor/mediatek/` |

**DTS**: `xaga.dts`, `xaga_global.dts` (global variant), `xaga-mt6895.dtsi` chain (touch/camera/charger/display/thermal), `cust/xaga.dtsi`.

## 6. Known Gaps / Risks

| # | Gap | Status | Handling |
|---|---|---|---|
| 1 | Fingerprint goodix_cap | ✅ Completed | Ported to standard Linux 6.12 SPI subsystem APIs (`drivers/input/fingerprint/goodix_cap/`), replacing legacy 5.10 `mtk_spi.h` |
| 2 | vendor/mediatek full set not in this tree | Requires user environment | mtkcam etc. provided by user MTK manifest |
| 3 | sensor merge | ✅ Completed | 6 xaga camera sensor drivers registered in `src-v4l2/BUILD.bazel`, `Kbuild`, `Makefile`, and `mgk_64.bzl` (Closes #6) |
| 4 | DTS Makefile 0 xaga registrations | Requires user environment | DTBO list registered in `kernel/build` mgk rules (cannot be completed in this tree); mgk_64.bzl:1361 registered lm3644 for mt6895 (xaga uses KTD2687, retaining is harmless and removable) |
| 5 | Touch fw files | Requires device side | nt36672e fw file placed in vendor partition corresponding path |
| 6 | Functional verification (Module load passed) | In progress | After 200 ko load success: enter system -> `/sys/class/power_supply/` should have mtk-master-charger/bms/battery -> 5V normal charging -> PD fast charge manager verification (sequence see BRINGUP.md §3.4; entering system requires blob support see §6.8) |
| 7 | **Boot Mode = recovery (boot mode 2)** | ✅ Resolved (2026-08-13) | After display pipeline fix (PWM0/SPR0 compatible), **recovery enters normally with normal display**; normal reboot into system constrained by blobs (see below) |
| 8 | **Booting into system requires blob support** | ⚠️ Current boundary | **Artifacts of this repository (kernel + GPL modules) verified up to recovery**. Full Android system boot also requires **proprietary blobs** (not in this repo, non-redistributable): TEE/gz/mcupm/sspm/pi_img firmware images, vendor partition proprietary binaries (camera HAL/ISP, fingerprint GF3626ZS9 TEE app, audio DSP firmware, etc.), as well as system/vendor partition images beyond official vendor_boot. These originate from Xiaomi official ROM / MTK release packages, requiring user integration & verification in a complete environment (with blobs) |

## 7. Next Steps

1. **User Environment Integration (Blocks full build)**:
   - mgk rules add `xaga` to project DTBO list
   - Merge vendor/mediatek and alps sibling projects
   - Sensor append (§6.3)
2. **Physical Device Boot into System Verification** (Module load chain passed, 2026-08-11):
   - Normal reboot (clear recovery flag) -> confirm boot mode 0
   - init stage 2 / system mount / zygote startup
   - Charging flow (sequence see BRINGUP.md §3.4): `/sys/class/power_supply/` -> 5V normal charging -> PD fast charge
3. **Remaining Tasks (Non-blocking)**: Fingerprint (optional), lm3644 cleanup (optional), touch fw placement.

## 8. Key Decision Records

| Decision | Reason |
|---|---|
| Based on OPPO `android_kernel_oddo_mt6895` instead of Xiaomi 6.6 | Same version (6.12) preferred; Xiaomi 6.6 is GKI common without MTK device layer |
| qc_cp_manager disabled | xaga is MTK PD fast charge device; 5.10 vendor config never set XM_QC_MANAGER |
| Touch uses NVT36672C instead of 6.12 NT36532 | xaga actual shipping driver (double-tap wake + game parameters) |
| C7 does not port fpsgo_cus/msync2_frd_cus | fpsgo fully covered by 6.12 fpsgo_v3; msync2 core is closed source 5.10 binary |
| Retain of_gpio.h/of_get_named_gpio | 63 in-tree users; do not "modernize" to gpiod when porting more 5.10 drivers |
| Dual channel log capture (Finalized 2026-08-13): **Hang location uses xaga-marker (XAGR ring) + Crash log uses oops partition kmsg_dumper** | kmsg_dumper does not trigger during kernel hang (hangs have no oops/panic), use marker ring (log_store 0x7ffbf000, last line in ring = hanging module, preserved in DRAM across WDT reset); during crash (oops/panic), `xaga-dumpregs` kmsg_dumper writes complete dmesg to sdc81 (does not rely on LK restoration). Both complementary (marker finalized 2026-08-09; added oops partition channel 2026-08-13) |
| Module dependency restoration strictly based on alps `BUILD.bazel` (2026-08-10) | Porting tree Makefiles had many obj- commented out (OPPO uses bazel); restored symbol by symbol per alps srcs/ko_deps/copts, source code byte-for-byte identical with alps, only 3 code-level changes (panel_dead porting / nt36xxx stub / cmdq kvm guard) |
| Packaging stage `llvm-strip --strip-debug` (2026-08-10) | Official 5.10 ko has no DWARF (198 ko total 39.4MB), porting tree carries DWARF5 (130MB); strip retains .symtab/__ksymtab/modinfo -> 25MB, vendor_boot returns to 64MB specification |
| Turn off `CONFIG_MTK_ECCCI_DRIVER` (2026-08-10) | eccci m mode Makefile structure incomplete (alps uses bazel); mtk_bp_thl ccci reference guarded by IS_ENABLED, turning off eliminates dependency |
| Swapped `mtk_disp_recovery.c` to alps original (2026-08-10) | Porting tree version of this file was OPPO modified version, containing oplus_ofp_* proprietary calls (no provider in 6.12); xaga does not need OPPO code, swapped to original + 5.10 ported panel_dead interface |

## 9. Old/New Commit Cross-Reference (All old hashes invalid after rebase)

| Old (Invalid) | New (2026-08-07) | Content |
|---|---|---|
| bf0eb34 | 49dfceb | Import 6.12 modules + xaga board level porting |
| 8de91e2 | 19311e8 | Xiaomi charging framework + panel providers |
| 14b999c | 3c16b0a | xaga_global variant + enable charge-pump |
| — | f4bc147 | NVT36672C SPI touch |
| 26f455e | 109b0d0 | usb_psy aligned with mtk-master-charger |
| — | 59499d6 | Kconfig.ext chain fix |
| — | d94a22c | Do not enable qc_cp_manager |
| 132501c | e361186 | Initial compilation 6.12 API fixes |
| 6fc39f0 | d55ac4f | Full build fixes |
| — | 0b7a811 | pstore/blk oops log |
| — | 1409327 / 5829818 | Full platform modules 118 / 123 ko |
| — | a377ba2 / 4f9489f | Audit fixes (crash bug / config gaps / kleaf registration) |
| — | 11690c7 | Clean up OPPO config remnants |
| 9c6a13e etc. | 7f75af8 / f3e8e80 | 6 camera sensors + docs |
| — | e321232 / 1aadba1 | KTD2687 flash + flashlight wiring |
| — | 4b2d268 | MTK Pump Express |
| — | 47e0ac5 / 270467e | README + fingerprint gap documentation / device name correction |
| — | 06ee319 | Restore mrdump module (aee_sram_printk) + modules.load topological order (2026-08-10) |
| — | 551d6cd | Restore clk-common module (get_all_provider_clks, 2026-08-10) |
| — | 17f5d4c | **Restore MTK DRM module dependency chain (197 ko, 0 undefined) + xaga-drm-restore.md** (2026-08-10) |
| — | 32c3adf | **Prune 4 pairs of duplicate exported symbol modules** (devapc legacy / mmdvfs-v5 / mmdebug-vcp / gud ffa; 197->193 OOT) (2026-08-11, expdb5 verified) |
| — | c5113ed | **vendor_boot package 4 in-tree dependency modules** (drm_display_helper/drm_dma_helper/iio buffer/kfifo; 193 OOT + 4 = 197) (2026-08-11, expdb verified) |
| — | 99a5feb | **Fix mediatek-drm-gateic composite module** (rename to avoid Kbuild circular dependency) (2026-08-11) |
| — | 84e1aaf | **mmqos DTS property names aligned with 6.12** (larbs-supply/commons-supply/mmqos-state) (2026-08-11, NULL deref Oops) |
| — | dc99de2 | **build.sh progress visualization** (timestamps/step counts/real-time progress bar) (2026-08-11) |
| — | f8313fa | **dramc getters NULL drvdata guards** (4 exported functions) (2026-08-11, mmqos Oops) |
| — | 3b9b285 | **Disable mmc1** (xaga has no SD slot, QoS plist_del BUG) (2026-08-11) |
| — | a05b916 | **ufshci add mediatek,ufs-disable-mcq** (UFS legacy doorbell) (2026-08-11, init partition timeout) |
| — | 4ac9c34 | **DTS aligned with alps 6.12** (smi-supply 49 locations / dispsys-num / fifo-size / mmdvfs prefix / panel1/2 swap / xaga.config add XAGA_DUMPREGS+DEVTMPFS+USB_G_SERIAL) (2026-08-13, physical hardware 38/42 components bound) |
| — | 81093dd | **Build & package: 200 OOT ko + mediatek-drm-panel-drv/nvmem/wdt wiring** + modules.dep dependency order + llvm-strip (2026-08-13) |
| — | 30c80ea | **Fix disp_pwm0/disp_spr0 compatible aligned with mtk_ddp_comp_dt_ids** (display pipeline root cause: crtc0 not created -> display size 0x0 -> atomic oops) (2026-08-13, recovery bootable) |

## 10. expdb Physical Hardware Diagnostic History (2026-08-08 ~ 08-11, 7 Rounds)

> Diagnostic Method: Flash build onto physical hardware -> after WDT reset/panic, dump LK expdb partition (`xaga/expdb`, 128MB raw, PL_LOG_STORE mechanism automatically writes kernel logs to reserved area and restores with expdb) -> extract text to locate crashes. **Each round fixes one init stage issue**, ultimately **all 197 modules loaded successfully**.

| Round | expdb | Failure Mode | Root Cause | Fix Commit |
|---|---|---|---|---|
| 1-4 | expdb1-4 | `Unknown symbol` -> `Attempted to kill init` (module load missing provider) | devapc/clk-common/mrdump/aee_aed etc. Makefile obj- lines commented out (OPPO uses bazel) | 06ee319 / 551d6cd / 17f5d4c (2026-08-10) |
| 5 | expdb5 | `exports duplicate symbol` -> init kill | devapc legacy/common dual modules duplicate export `register_devapc_vio_callback` | 32c3adf |
| 6 | (cont. expdb5) | `exports duplicate symbol` (mmdvfs-v5) | mmdvfs-v3/v5 duplicate exports; alps platform mapping for mt6895 uses v3 only | 32c3adf (+mmdebug/gud three-to-one cleaned up) |
| — | (Build) | — | OOT dependencies on K-tree `=m` modules (drm_display_helper etc.) not packaged | c5113ed |
| 7 | expdb (14:22) | `Unknown symbol mtk_drm_gateic_register` | Composite module `-y` missed main file -> Kbuild circular dependency | 99a5feb |
| 8 | expdb (14:22) | mmqos NULL deref Oops | DTS property names 5.10 old syntax (larbs/commons missing -supply) | 84e1aaf |
| 9 | expdb (14:22) | mmqos->dramc NULL drvdata | dramc probe missing DTS property returned early, drvdata unset; getter unguarded | f8313fa |
| 10 | expdb (14:46) | msdc1 QoS `plist_del` BUG | xaga has no SD slot but mmc1 enabled (6.12 mtk-mmc has QoS calls, 5.10 does not) | 3b9b285 |
| 11 | expdb (15:03) | UFS `legacy doorbell mode not supported` -> init partition timeout | ufshci missing `mediatek,ufs-disable-mcq` (lost during 5.10 port) | a05b916 |
| 12 | expdb (15:03 cont.) | `libbacktrace.so not found` -> init kill | **LK boot mode 2 (recovery)**, not a kernel defect; 197 ko all loaded successfully (707ms) | (User action: normal reboot) |

### 08-12/08-13 Display Pipeline Rounds (sdc81 oops partition + XAGR dual channel log capture)

| Round | Phenomenon | Root Cause | Fix |
|---|---|---|---|
| 13 | recovery gray screen / components unbound | fw_devlink blocked (cmdq-config missing -> gce never probes -> dma_configure -517 -> mdp_iommu -22) | DTS added cmdq-config / smi-supply 49 locations / 16 disp node properties (2026-08-12, 38->42 components bound) |
| 14 | atomic oops (`mtk_dsi_connector_duplicate_state` NULL deref) | display size 0x0: crtc0 not created | **Probe location: `Not creating crtc 0 because component 54` -> PWM0 compatible `-pwm` mismatched match table `-pwm0`** (2026-08-13) |
| 15 | (cont.) | crtc0 uses ext path (DP_INTF0) -> GET_TIMING sent wrong | **30c80ea: disp_pwm0 `-pwm`->`-pwm0` + disp_spr0 `-SPR`->`-spr`** -> **recovery enters normally with normal display** |

**Log Capture (Finalized 2026-08-13)**: XAGR ring (log_store -> expdb) + **oops partition kmsg_dumper** (`xaga-dumpregs`, writes full dmesg to `/dev/block/sdc81` during crash, XGAD header + 512KiB; write chain fixed: 4KB aligned bio / IRQ window / kzalloc buffer).

**Diagnostic Key Points**: Kernel log captured by expdb starts from setup_arch head (XAGR ring armed), module loading sequence + panic tail fully visible; `grep -aE "Kernel panic|Unable to handle|exports duplicate|Unknown symbol"` pinpoints each round failure point.

### 08-15 Black Screen Investigation Rounds (8 Rounds, Final Rollback to 16:33 Checkpoint)

> Background: After recovery display restored on 08-13, flashing again caused **black screen regression** (backlight on, no picture). Investigation confirmed **LK never initialized DSI** (`DSI_STATE_DBG6-9` all 0; LK `set dsi panel index to dtb failed` / `SLEEPOUT_DONE` polling timeout; expdb proved LK writes `videolfb - islcmfound = 1` every boot), while probe unconditionally assumed "DSI0 enabled in LK" (`output_en=true, clk_refcnt=1`) -> preconfig skipped, poweron shorted -> DSI engine never started -> CMDQ waiting for `FRAME_DONE`(313) timed out -> black screen. Before 16:33 first_enable forced DSI re-init fallback existed, unresolved; after 16:33 continued deep investigation for 8 rounds layer-by-line, ultimately **unresolved, rolled back to 16:33 checkpoint for record** (changes committed, can be restored anytime).

| Round | Phenomenon | Root Cause (Code level confirmed) | Handling |
|---|---|---|---|
| 16 | `Failed to set data rate: -16`(EBUSY) during re-init | probe's `phy_power_on` kept mipi_tx PLL prepared (clock registered `CLK_SET_RATE_GATE`); **MTK customized CCF `clk_core_rate_is_protected` returned `protect_count` (upstream is `>1`)** -> `clk_set_rate` returns EBUSY whenever PLL prepared | `clk_disable_unprepare(hs_clk)` before poweron (rolled back) |
| 17 | `mtk_dsi_ps_control_vact` / `mtk_dsi_config_vdo_timing` NULL deref (0x7d8) in preconfig | During DRM-init first_enable, `encoder->crtc` unassigned (only present during first atomic commit) | `dcrtc` falls back to `comp->mtk_crtc` (rolled back) |
| 18 | `mtk_ddp_connect_dual_pipe_path` NULL deref (0x5c = `comp->id`) | 6.12 OPPO tree changed `DLO_ASYNC0-7/DLI_ASYNC0-7` from `MTK_DISP_VIRTUAL` to real types (exclusive for MT6991/93 discrete path); MT6895 lacks this hardware -> `priv->ddp_comp[107]` NULL -> dual pipe ctx left NULL | Type table changed back to `MTK_DISP_VIRTUAL` (same as 5.10; rolled back) |
| 19 | DSI_STATE still 0 after engine_start, CMDQ continues waiting for 313 | **Engine never started when LK uninitialized**: engine_start only performed poweron (clocks/PLL/PHY) + DSI_EN, missing **VDO mode selection + `DSI_START` pulse** (VDO mode requires `DSI_MODE_CTRL` mode selection + `DSI_START` 0->1) | engine_start added `_mtk_dsi_set_mode` + `mtk_dsi_start` (rolled back) |
| 20 | Same as above | **MTCMOS Enable Timing**: In first_enable `mtk_drm_top_clk_prepare_enable`+`mtk_crtc_ddp_prepare` were **after** `first_enable_ddp_config` (normal design relies on LK power enabled) -> writing DSI registers during DRM-init phase entirely invalid (DSI_STATE reads 0 / bus garbage) | Turned power on early before engine_start in first_enable (rolled back) |
| 21 | Same as above | **mutex/Path Never Configured**: Normally configured by LK (MUTEX EN/SOF=0x41, OVL EN, RDMA/DSC, crossbar—see 5.10 dumpregs reference); kernel's `mtk_crtc_connect_default_path`+`mtk_crtc_config_default_path` only execute during first atomic commit -> pipeline has no data source when first_enable waits for FRAME_DONE | Added LK replication in first_enable (connect+config, rolled back) |
| 22 | atomic's `mtk_dsi_exit_ulps` stuck for 2s then failed | LK uninitialized -> PHY did not enter ULPS -> `SLEEPOUT_DONE` does not arrive -> `wait_dsi_wq(..., 2*HZ)` timed out, dragging down atomic commit (NST_LOCK timeout) | Skipped or shortened in engine_start/preconfig (rolled back) |
| 23 | Conclusion | **When LK does not initialize DSI, kernel must fully replicate LK pipeline initialization** (MTCMOS -> DSI engine -> mutex/path -> FRAME_DONE), involving major refactoring across first_enable/engine_start/preconfig, unresolved | **Rolled back to 16:33 checkpoint and committed for record** |

**Checkpoint (2026-08-15 16:33, status when artifacts built)**:

- **M-tree** (this repo): `7b9cb8f` (after rebase to remote main `64a634b`) — *xaga: display bring-up checkpoint (recovery bootable, DSI re-init fallback)*. Contains all uncommitted changes prior to 16:33: first_enable DSI re-init fallback, gray screen SMI self-recovery (`mtk_disp_ovl`), `mtk_drm_esd_recover` export, DTS/panel, g_serial console (`console=ttyGS0`), OOT build INC (200 ko). **Pushed to origin/main pending network recovery** (GnuTLS to github handshake blocked by network).
- **K-tree** (`../android_kernel_oddo_mt6895`): `20b279ed9` -> **Pushed to github `xaga-6.12`** (fast-forward `6dce1808b..20b279ed9`) — kmsg2usb oops partition gate, g_serial console, probe/defer traces, xaga-dumpregs removal.

**Future Direction (if black screen continues)**: Using `64a634b` as baseline, advance along "LK replication" approach: ① in first_enable before engine_start call `mtk_drm_top_clk_prepare_enable`+`mtk_crtc_ddp_prepare` (turn on MTCMOS); ② engine_start fully start engine (VDO mode + `DSI_START` + skip exit_ulps); ③ add `mtk_crtc_connect_default_path`+`mtk_crtc_config_default_path` (mutex/path); ④ reset probe LK handoff mislabeled `panel->prepared/enabled` -> verify layer by layer against 5.10 dumpregs (DSI START/MODE/TXRX, MUTEX EN/SOF, OVL EN).

## 11. Related Documentation Index

| Document | Role | When to Read |
|---|---|---|
| **STATUS.md (This File)** | Status Overview | Asking "to what extent is porting complete / what's missing" |
| BRINGUP.md | Bring-Up Guide | Build recipes, power-on sequence, charging alignment, sensor merge steps |
| README.md | Repository Overview | Quick understanding of tree structure, features, commit key points |
| **xaga-drm-restore.md** | DRM Dependency Restoration Special | Module dependency table (consumer -> provider) + dependency analysis/porting methodology + image size handling (2026-08-10) |
| xaga-log-capture.md | Log Capture Methods | XAGR ring / LK expdb / hang positioning (2026-08-10) |
