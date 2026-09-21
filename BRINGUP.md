# xaga 6.12 Bring-up Guide (Goal: Booting)

> Redmi Note 11T Pro / POCO X4 GT / Redmi K50i (xaga, MT6895 / Dimensity 8100)
> 6.12 MTK kernel_device_modules porting tree. Goal: Compile a bootable kernel + DTBO + modules,
> and complete bring-up from "screen on into system" to "charging functionality" on physical hardware.
> Status baseline: commit `30c80ea` (2026-08-13, pushed to origin/main). History was rebased, see STATUS.md §9 for old hashes.
> **Hardware Progress (2026-08-13)**: After closing the display pipeline root cause (PWM0/SPR0 compatible), **compiled artifacts can enter recovery with normal display** (boot process LK->early kernel shows corruption, normal after entering recovery; described only, not fixed). **Booting into system (normal Android boot) requires blob support** (TEE/gz/mcupm/sspm firmware, vendor partition proprietary binaries, etc., see STATUS.md §6.8).

---

## 0. Tree Structure Overview

```
xaga/kernel_xiaomi_mt6895-6.12/            # This porting tree (git repo, 24 commits)
└── kernel_device_modules-6.12/            # MTK 6.12 OOT module source (based on OPPO android_kernel_oddo_mt6895, OPPO-exclusive stripped)
    ├── arch/arm64/boot/dts/mediatek/      # xaga.dts / xaga_global.dts board overlay
    ├── arch/arm64/configs/vendor/xaga.config   # xaga defconfig fragment
    ├── drivers/...                        # Ported Xiaomi drivers (touch/charging/panel/backlight/haptics)
    └── kernel/kleaf/mgk_64.bzl            # mgk module registry
```

**Not included in this tree**: GKI common kernel (requires OPPO `../android_kernel_oddo_mt6895` 6.12.23 or Xiaomi 6.6 corresponding kernel), `kernel/build` (MTK mgk bazel rules), clang toolchain, `vendor/mediatek/kernel_modules` (connectivity/gpu etc. MTK common modules, 6.12 version in OPPO tree `vendor/mediatek/kernel_modules/`). These come from the user's complete MTK repo manifest environment.

---

## 1. Build (In User's Complete MTK Environment)

### 1.1 Required Components
| Component | Source | Description |
|---|---|---|
| GKI common kernel 6.12 | OPPO `../android_kernel_oddo_mt6895` (6.12.23) | Paired build with modules tree |
| kernel_device_modules-6.12 | This porting tree | Contains xaga board + drivers |
| kernel/build (mgk rules) | MTK alps manifest | bazel/kleaf build driver |
| vendor/mediatek/kernel_modules | OPPO tree or MTK manifest | connectivity/gpu/met_drv/udc (6.12 version, mt6895 supported) |
| Clang prebuilt | AOSP | - |

### 1.2 Project Registration (mgk side, cannot be done in this tree)
- **DTBO List**: In `kernel/build/bazel_mgk_rules`, add `xaga` to the project DTBO list
  (refer to OPPO's own `arch/arm64/boot/dts/oplus/oplus6895_23021.dts` registration method).
  ⚠️ This tree's `arch/arm64/boot/dts/mediatek/Makefile` **has not registered xaga** (0 references); classic build
  path requires manually adding `dtb-$(CONFIG_ARCH_MEDIATEK) += xaga.dtb xaga_global.dtb`,
  while kleaf path goes through the DTBO list in mgk rules (main path above).
- **Module List**: `mgk_64.bzl` has registered xaga-related modules (see §2.3); other modules go through
  OPPO/MTK default list, verify unneeded modules for xaga (oplus_* already stripped).

### 1.3 defconfig
Merge `arch/arm64/configs/vendor/xaga.config` on top of `mgk_64_k612_defconfig`:
```
scripts/kconfig/merge_config.sh -m -r \
  arch/arm64/configs/mgk_64_k612_defconfig \
  arch/arm64/configs/vendor/xaga.config
```
Key symbols: `CONFIG_TOUCHSCREEN_NVT36672C_HOSTDL_SPI=m` (touch),
`CONFIG_DRM_PANEL_L16_*` (panel), `CONFIG_XMEXT_LN8000/SC8551A_CHG_PUMP=m` (charge pump),
`CONFIG_XMEXT_TI_GAUGE=m` (bq28z610 -> "bms" psy), `CONFIG_XM_PD_MANAGER=m` (charging algorithm,
**keep fallback charging before aligning usb_psy after boot, do not plug fast charger**), `CONFIG_SIMTRAY_STATUS=m`,
`CONFIG_INPUT_AW8697_HAPTIC=m`, `CONFIG_TOUCHSCREEN_XIAOMI_DOUBLE_CLICK=m`,
`CONFIG_MTK_VIDEO_KTD2687=m` (flash), `CONFIG_DRM_PANEL_LEDS_KTZ8863A=m` (backlight).

> ⚠️ **boot-stage marker (XAGR ring, hang location)**: `CONFIG_XAGA_MARKER_WRITER=y`
> (**Built-in kernel**, source in OPPO kernel tree `drivers/misc/xaga-marker-writer.c`,
> no longer built as a module—module version never wrote) maps **log_store reserved area 0x7ffbf000**
> at **arm64 setup_arch head** (after `early_ioremap_init`, earliest mapping point allowed by MMU fixmap) as
> XAGR ring (magic 0x52474158 @0x0000 / cursor @0x0004 / total @0x0008 /
> stage @0x1000 / text ring @0x2000 length 0xE000), and mirrors **every printk()** (vprintk_emit
> hook) into the ring—hang at any boot stage, ring tail is the last section of kernel log.
> The ring survives AP watchdog reset in DRAM, next boot printed by **lineage_xaga kernel's
> xaga-marker reader** (postcore_initcall dump, dmesg + `/proc/xaga_marker`).
> kmsg_dumper does not trigger during kernel hang, so xaga_oops_log/oops partition scheme is not used
> (deprecated, 2026-08-09).
> ⚠️ **Lesson (2026-08-09 test)**: XAGR ring **must never be placed in minirdump area 0x48170000**—
> writing to this area triggers MTK mrdump mechanism to **reboot immediately** (same lesson noted in diff.txt);
> aee/mrdump_mini will also overwrite header of this area. Switched to log_store (0x7ffbf000, NS secure area,
> not in LK mblock table, non-mrdump area).
> ⚠️ **Note**: "Advancing to **before** setup_arch" is impossible on arm64—before paging_init
> linear mapping is not established, 0x7ffbf000 is unwriteable, `early_ioremap` (fixmap) is the
> earliest hardware-permitted path; module loading phase (formerly module notifier's job) is naturally covered by printk mirroring.

### 1.4 Artifacts
`Image.gz` + `mt6895.dtb` + `xaga.dtbo` (+`xaga_global.dtbo`) + module ko set.
When packaging into boot/vendor_boot note: use `xaga.dtbo` for dtbo (CN version); use CN version first for boot.

#### Packaging Requirements (Finalized 2026-08-08, verified official content version bootable)
- **boot = magiskboot** (`xaga/images/building/tools/magiskboot`, -n flow): `unpack -n -h boot_a.img` -> replace kernel (Image.gz original) / ramdisk (`magiskboot cpio ... "add 0755 kernelsu.ko <6.12 version>"`) -> `repack -n`. ⚠️ Default repack re-gzips kernel, **tested unable to boot**, must use -n.
- **Android 16 VABC Ramdisk Requirement (Upgraded 2026-09-20, Closes #7)**: Base `boot.img` was upgraded from Android 12 GSI base (`OS_VERSION: 12.0.0`) to Native Android 16 (`OS_VERSION: 16.0.0`, `OS_PATCH_LEVEL: 2026-09`) containing the userspace snapshot daemon (`snapuserd` and `snapuserd_ramdisk`). For Android 16 Virtual A/B with Compression (VABC), `snapuserd` in the first-stage ramdisk is mandatory for mounting dynamic partitions (`system`, `vendor`, `product`). Repacked with legacy LZ4 compression padded to 64MB to match xaga bootloader expectations.
- **vendor_boot = local mkbootimg** (`/tmp/mkboot_new/mkbootimg_lineage-21.0.py`): `--vendor_boot out.img --vendor_ramdisk <official complete ramdisk area 42,743,860B containing 2,005B "TRAILER!!!" tail> --dtb <DTBO_TAG container> --vendor_cmdline "bootopt=64S3,32N2,64N2" --base 0x40000000 --kernel_offset 0 --ramdisk_offset 0x26f00000 --tags_offset 0x7c80000 --dtb_offset 0x7c80000 --header_version 4 --pagesize 4096 --board ""` ; artifact needs patch vrt offset 43,077,632 location 4 bytes to 42,741,855 + pad to 64MB. ⚠️ magiskboot packaging vendor_boot tested failure (loses 2,005B tail + ramdisk_size field mismatch), do not use.
- Detailed recipe see container root `AGENTS.md` "xaga image packaging requirements" subsection and `xaga/README.md`.

### 1.5 Module Coverage (Official 198 -> 6.12, no hard gaps)
Official 5.10 ramdisk's 198 .ko files (vermagic `5.10.198`, 6.12 kernel unable to load) covered in 4 layers on 6.12 side:
① 109 out of 123 packaged modules directly replaced with same name; ② Renamed/merged (clkchk/mt6375-gauge/mtk_system_heap/pinctrl-mtk-common-v2/fan53870-ldo etc., see STATUS.md §4); ③ ~30 built into 6.12 kernel (mediatek-drm*/mtk-mmc-autok/regmap-spmi/mac80211 etc., no .ko needed); ④ Debug/diagnostic omitted as non-essential.
**Only non-counterpart = `mi-memory`** (Xiaomi proprietary, not required for boot).

**2026-08-10 Packaging set expanded to 197 ko**: init first stage insmod one-by-one per modules.load topological order, any `Unknown symbol` causes `Attempted to kill init`; restored ~60 provider modules after full `llvm-nm` audit (mediatek_v2/mml/devapc/iommu/cmdq/tcpc_class/cqhci/gpufreq v2+hal/slbc/hwccf/mtu3/system_heap etc., all per alps `BUILD.bazel`), **197 ko, 0 undefined**—full dependency table and porting methodology see `xaga-drm-restore.md`. Packaging stage performs `llvm-strip --strip-debug` on .ko (official has no DWARF, porting tree with DWARF5 reaches 130MB, 25MB after strip).

---

## 2. Boot Bring-up (Power-on Sequence)

> Principle: Get kernel up, light up screen, enter system first; then enable features one by one. Check corresponding log signatures at each step.

### 2.0 Boot Chain Check
1. **bootloader loading**: Confirm preloader/lk applies `mt6895.dtb` (SoC base) + `xaga.dtbo` (board overlay) in sequence.
   Error signature: `overlay not applied` -> DTBO list missing xaga (§1.2).
2. **early log checkpoints** (dmesg):
   - `mt6895` SoC initialization, `androidboot.hardware=mt6895` (xaga.dts bootargs_ext already included)
   - DTS verification: Kernel outputs `OF: fdt: ...` / if stuck check §2.1

### 2.1 Boot Stuck / Panic Troubleshooting
| Symptom | Check | Corresponding Code |
|---|---|---|
| dtc compile/load error | Board DTS reference unresolved (this tree statically verified 210 references, recheck if base in your environment matches) | arch/arm64/boot/dts/mediatek/xaga*.dts |
| mtee/svp related panic | xaga.dts memory_ssmr svp-region depends on TEE; if no TEE firmware, use xaga_global.dts (no svp) | xaga.dts vs xaga_global.dts |
| `exports duplicate symbol` -> init kill | Two modules export same symbol name (devapc legacy/mmdvfs-v3-v5/mmdebug-vcp-stub/gud ffa four pairs, fixed); locate via `grep -n "exports duplicate"` if new appears, keep per alps mgk_64.bzl platform mapping | drivers/{soc/mediatek,tee/gud}/Makefile |
| `Unknown symbol` -> init kill | Missing provider module: ① OOT module missed packaging ② **K tree `=m` in-tree module not in vendor_boot** (drm_display_helper/drm_dma_helper/iio buffer/kfifo fixed) ③ Composite module `-y` missed main file (gateic fixed) | build.sh pack_vendor / respective Makefiles |
| mmqos NULL deref Oops | DTS property names must use 6.12 syntax: `mediatek,larbs-supply`/`mediatek,commons-supply`/`mmqos-state` (5.10 old names fixed) | mt6895.dts mmqos node |
| mmqos->dramc NULL drvdata | dramc probe returned early due to missing DTS property but `dramc_pdev` already assigned; 4 getters now guarded with NULL checks | drivers/memory/mediatek/dramc.c |
| msdc1 QoS `plist_del` BUG | **xaga has no SD slot** -> mmc1 kept disabled (xaga-mt6895.dtsi override; 6.12 mtk-mmc has cpu_latency_qos, 5.10 does not) | xaga-mt6895.dtsi `&mmc1` |
| UFS `legacy doorbell mode not supported` | ufshci node requires `mediatek,ufs-disable-mcq` (MT6895 UFS is legacy-doorbell, lost during 5.10 porting) | mt6895.dts ufshci node |
| init `partition(s) not found` timeout | Cascading effect of above (UFS not up); self-heals after fixing UFS | — |
| init second stage `libbacktrace.so not found` | **LK boot mode 2 (recovery)**: `First stage mount skipped (recovery mode)` -> system not mounted. Not a kernel defect, check BCB/misc or normal reboot (§2.4) | User operation |
| Stuck at display probe | L16 panel driver depends on 3 providers (see §3.3), probe failure -> black screen if missing; try `xaga_global.dts` + confirm `CONFIG_DRM_PANEL_L16_*` + `CONFIG_DRM_PANEL_LEDS_KTZ8863A` enabled | panel-l16-*.c, leds-ktz8863a.c |
| Stuck at touch | NVT36672C probe; confirm `CONFIG_TOUCHSCREEN_NVT36672C_HOSTDL_SPI=m`, spi2 node | drivers/input/touchscreen/NVT36672C/ |
| Charger IC probe failed | i2c9/i2c7 nodes (lm8000/sc8551/bq28z610) present in DTS; bq28z610 probe fail -> no "bms" psy (§3.2 depends on it) | drivers/power/supply/{ln8000,sc8551,bq28z610}*.c |

### 2.4 Boot Mode (Recovery) Troubleshooting

**Symptom**: Kernel and all modules load normally (`Loaded 197 kernel modules took 707 ms`), but
`init: First stage mount skipped (recovery mode)` -> `/system/bin/init` reports
`library "libbacktrace.so" not found` -> `Attempted to kill init` (exitcode 0x100).

**Reason**: LK boots kernel with `boot mode = 2` (recovery) (`boot_linux_fdt: lk boot mode = 2`).
System partition is not mounted in recovery environment, init cannot link system libraries. **Not a kernel/module issue**.

**Handling**:
1. `fastboot reboot` (without recovery parameter) normal reboot, observe if boot mode returns to 0
2. If still entering recovery: Check if misc partition BCB (bootloader message) retains
   `boot-recovery` flag (`fastboot erase misc` or `adb reboot` inside recovery)
3. Confirm `androidboot.bootmode` / `bootmode` tag (passed from LK to kernel via pl-boottag)

### 2.2 Boot Required Probe Drivers (In Order)
```
mtk framework (mtk_charger/mtk_battery/mtk_disp_*)  ← 6.12 native, generally OK
├─ bq28z610 (i2c7, registers "bms" psy)             ← Fuel gauge, manager dependency
├─ ln8000/sc8551 (i2c9, dual charge pumps)          ← Fast charge hardware
├─ NVT36672C (spi2, touch)                          ← Required, otherwise unable to unlock/enter desktop
├─ L16 panel (dsi0) + ktz8863a backlight (i2c6)     ← Required, otherwise black screen
├─ aw8697_haptic (i2c1)                             ← Optional (haptics)
└─ simtray (GPIO42)                                 ← Optional (sim tray detection)
```

### 2.3 Module Registration Verification (Already in mgk_64.bzl, confirm listed during build)
`bq28z610` / `sc8551` / `sc8561` / `ln8000_charger` / `pmic_voter` /
`pd_cp_manager` / `charger_class` (power/supply);
`nt36672c` (touchscreen/NVT36672C); `panel-l16-*` + `leds-ktz8863a` (drm/panel);
`aw8697_haptic` (input/misc, via ddk_makefile glob); `simtray` (misc, Kconfig wired).
> Note: KTD2687 flash goes through `drivers/misc/mediatek` BUILD.bazel (mtk-composite wiring, not mgk_64.bzl device_modules); 6 sensors go through src-v4l2 (see §6). qc_cp_manager source present in tree but not enabled (xaga is MTK PD fast charge device, 5.10 also did not enable QC manager).

---

## 3. Post-Boot Function Alignment (Focus: usb_psy / Charging Manager)

> Background: 5.10 xaga charging psy named `"usb"`; 6.12 tree registered as `"mtk-master-charger"`
> (mtk_charger.c:4127). drvdata are both `struct mtk_charger*` (type identical, no handling needed).
> Ported usb_get/set_property and manager still query `"usb"` -> returns -ENODEV, fields all 0.

### 3.1 Step 1: psy Name Alignment ✅ Implemented
5 locations `power_supply_get_by_name("usb")` -> `"mtk-master-charger"`:
```
drivers/power/supply/mtk_charger.c          :4439/4453  (inside usb_get/set_property)
drivers/power/supply/pd_cp_manager.c        :270
drivers/power/supply/pd_single_cp_manager.c :230   (xaga does not build, modified in passing)
drivers/power/supply/qc_cp_manager.c        :238
```
⚠️ Do not reverse rename psy to "usb" (20+ calls inside 6.12 + property table, high risk).

### 3.2 Step 2: USB_PROP Field Writers ✅ Implemented (guarded with CONFIG_XM_PD_MANAGER)
6.12 originally had no writer -> fields constantly 0 -> manager cannot get typec orientation/PD status -> does not enter fast charging.
Mirrored 5.10 calls:
```
drivers/power/supply/mtk_chg_type_det.c  (TCP_NOTIFY_TYPEC_STATE branch, ~:196)
    usb_set_property(USB_PROP_TYPEC_MODE, POWER_SUPPLY_TYPEC_SINK/AUDIO_ADAPTER/NONE);
    usb_set_property(USB_PROP_TYPEC_CC_ORIENTATION, noti->typec_state.polarity);
drivers/power/supply/mtk_pd_adapter.c    (pd_authentication success path, ~:568)
    usb_set_property(USB_PROP_PD_VERIFYING, 1);
    usb_set_property(USB_PROP_PD_VERIFY_DONE, 0);
    usb_set_property(USB_PROP_APDO_MAX, data->pdp);
    usb_set_property(USB_PROP_PD_AUTHENTICATION, 1);
    (+ #include "mtk_charger.h" under CONFIG_XM_PD_MANAGER)
```

> ✅ **Status (2026-08-06)**: §3.1's 5 name replacements (commit 109b0d0), §3.2's two writers
> (commit 109b0d0/19311e8) are implemented and committed, only verification needed on device, no code changes required.

### 3.3 Known Dependencies (Ported, do not delete)
- Panel `panel-l16-*.c` references 3 providers: `is_tp_doubleclick_enable()`
  (double_click.c), `get_panel_dead_flag()` (mtk_disp_recovery.c), `ktz8863a_*`
  (leds-ktz8863a.c) — all ported and wired.
- `pd_cp_manager` also depends on psy: `"bms"` (registered by bq28z610), `"battery"` (registered by mtk-battery-manager).

### 3.4 Hardware Verification Sequence
```
1. dmesg confirms above drivers probe successfully (§2.2 sequence)
2. ls /sys/class/power_supply/  → should have mtk-master-charger, bms, battery
3. zcat /proc/config.gz | grep XM_PD_MANAGER → =m
4. Plug 5V charger → observe mtk_charger log (normal charging, does not depend on manager)
5. Plug PD fast charger → pd_cp_manager log (depends on §3.1+§3.2 completion)
6. Before completing §3.1: Manager falls back due to usb_psy failure, won't burn hardware, test safely
```

---

## 4. Decision Log (Why Done This Way)

| Decision | Reason |
|---|---|
| Based on OPPO `android_kernel_oddo_mt6895` instead of Xiaomi 6.6 | Same version (6.12) preferred; Xiaomi 6.6 is GKI common without MTK device layer |
| Board DTS directly ported from 5.10 ESK chain, does not include k6895v1_64.dts | Both trees define same labels -> DTC duplicate label error |
| Touch uses NVT36672C instead of 6.12 NT36532 | xaga actual shipping driver (double-tap wake + game parameters); NT36532 only has basic bindings |
| Charging manager enabled by default, but usb_psy alignment left for physical device | No build issue; safe fallback on physical device |
| C7 does not port fpsgo_cus/msync2_frd_cus | fpsgo fully covered by 6.12 fpsgo_v3; msync2 core is closed source 5.10 binary |
| dtbo.dts.0 (decompiled from physical device) matches ported DTS node by node | Board DTS verified on physical device, no changes required |

## 5. Remaining Items (Non-Boot Blocking)
- [x] §3.1 psy name alignment — **Implemented and committed** (commit 109b0d0)
- [x] §3.2 USB_PROP writers (mtk_chg_type_det + mtk_pd_adapter) — **Implemented and committed** (commit 109b0d0/19311e8)
- [x] **init stage module load chain** — **2026-08-11 All passed on physical device** (197 ko / 707ms, §2.1 troubleshooting table fixed item by item)
- [ ] **System Boot Verification** (after boot mode 0): init second stage / zygote / desktop (§2.4)
- [ ] Verify §3.4 verification sequence on physical device (charging flow, requires system boot)
- [ ] Touch fw (nt36672e fw file) placed in vendor partition corresponding path
- [ ] xaga_global variant boot verification (if CN version TEE/svp has issues)
- [ ] mtk-master-charger name kABI/module load order check (if modprobe has dependencies)
- [x] **Fingerprint Driver (goodix_cap)**: Ported to standard Linux 6.12 SPI subsystem APIs (`gf3626zs9.c`)
- [ ] **Sensor User Environment Merge**: Append 6 xaga* names in user environment `src-v4l2/BUILD.bazel` `config_cust_kernel_imgsensor` (steps see §6)
- [ ] **lm3644 Registration Cleanup** (optional): `mgk_64.bzl:1361` registered lm3644 for mt6895, xaga uses KTD2687 instead, retaining is harmless and removable

## 6. Camera Sensor Porting (2026-08-07, Committed 7f75af8 + f3e8e80)
xaga's 6 camera sensor drivers have been ported from 5.10 ESK to this tree:

| Sensor | Role | Directory |
|---|---|---|
| s5khm2 | Main camera (108MP) | `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xagas5khm2_mipi_raw/` |
| s5k4h7 | Main camera backup | `.../xagas5k4h7_mipi_raw/` |
| ov16a1 | Front camera | `.../xagaov16a1_mipi_raw/` |
| s5kgw1 | Ultra-wide angle | `.../xagas5kgw1_mipi_raw/` |
| gc02m1 | Macro | `.../xagagc02m1_mipi_raw/` |
| ov02b10 | Front camera backup | `.../xagaov02b10_mipi_raw/` |

Adapted:
- `subdrv.mk` -> 6.12 `Makefile` (`imgsensor-objs += $(subdrv-rpath)/<name>mipiraw_Sensor.o`)
- `kd_imgsensor.h`: Added 6 `XAGA*_SENSOR_ID` + `SENSOR_DRVNAME_XAGA*` macros (imported as-is from 5.10)
- gc02m1/ov02b10 (8-bit reg): `subdrv_i2c_{rd,wr}_u8_u8` -> 6.12 `_u8_reg8`; missing `wr_regs_u8_u8` (2-byte table write), added local `*_table_write()` loop implementation inside sensor
- All 6 sensors passed compilation verification via -fsyntax-only against 6.12 mtkcam v4l2 framework (subdrv_ctx/subdrv_ops/subdrv_entry)

**User Complete MTK Environment Merge Steps (bazel/kleaf path, cannot be completed in this tree)**:
1. Merge 6 directories in `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xaga*/` into the same path in user environment (or directly use vendor/ delta of this tree)
2. In user environment `mtkcam/imgsensor/src-v4l2/BUILD.bazel` `config_cust_kernel_imgsensor` string, append:
   `xagas5khm2_mipi_raw xagas5k4h7_mipi_raw xagaov16a1_mipi_raw xagas5kgw1_mipi_raw xagagc02m1_mipi_raw xagaov02b10_mipi_raw`
   (bazel path collects per hardcoded list ∩ common/**/Makefile; make/Kbuild path automatically reads CONFIG_CUSTOM_KERNEL_IMGSENSOR, declared in xaga.config, no change needed)
3. xaga.config `CONFIG_CUSTOM_KERNEL_IMGSENSOR` already contains 6 sensors (no need to touch)
4. DTS side `xaga_mt6895_camera_v4l2.dtsi` imgsensor node compatible corresponds to sensor name, already provided by board DTS

Note: Sensor drivers only depend on v4l2 framework (user environment mtkcam), not in mgk_64.bzl device_modules list; if build reports missing symbols like `subdrv_i2c_wr_u8_u8`, it indicates older mtkcam version in user environment, rely on Makefile/local table write in this tree.

## 7. Flashlight and Fast Charge Protocols (2026-08-07, Non-Boot Blocking)

### 7.1 KTD2687 Camera Flashlight (commit e321232/1aadba1)
- Driver: `drivers/misc/mediatek/flashlight/v4l2/ktd2687.c`, dual light (v4l2 subdev).
- Wiring: `CONFIG_MTK_VIDEO_KTD2687=m` + flashlight core/composite restored (top-level Kbuild
  obj-y + `drivers/misc/mediatek` BUILD.bazel ddk_makefile/ddk_kconfigs).
- Hardware Verification: `/sys/class/leds/` shows ktd2687 related nodes, camera flash log has no probe failures.

### 7.2 MTK Pump Express Protocol (commit 4b2d268)
- Restored pep/pep20/pep40/pep45/pep50/pep50p six protocol modules (`mtk_pep*`, synchronized from alps),
  registered in mgk_64.bzl device_modules.
- Works with `CONFIG_XM_PD_MANAGER=m` to select PE protocol branch during PD fast charge negotiation, non-boot blocking.
