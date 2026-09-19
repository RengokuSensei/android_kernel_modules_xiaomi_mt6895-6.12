# xaga Kernel Log Capture Methods (LK log_store Restoration + XAGR Ring Buffer + oops Partition kmsg_dumper)

> Hardware Verification Status: Tested and verified on 2026-08-10—6.12 kernel mirrors printk into the log_store reserved area's XAGR ring buffer. LK restores and records the log_store area content to expdb upon next boot, and kernel logs reappear along with expdb (including MIRROR:n heartbeat proving mirror survival).
> **2026-08-13 Added Second Channel**: oops partition (`/dev/block/sdc81`) kmsg_dumper (`xaga-dumpregs`, K-tree built-in `CONFIG_XAGA_DUMPREGS=y`)—writes complete dmesg (XGAD header + 512KiB) to oops partition during crash, read via `dd if=/dev/block/sdc81 bs=4096 skip=1`, independent of LK restoration mechanism (see "oops partition kmsg_dumper" below).

## Principle in One Sentence

**LK's PL_LOG_STORE mechanism restores and records the content of DRAM log_store reserved area (0x7ffbf000, 256KB) to the expdb partition during boot**—therefore, writing kernel logs to the log_store area allows LK to log kernel messages for us, which can be read back from expdb after reboot. **No custom reader end is needed, nor is it affected by console configuration.**

## Mechanism Details (Verified 2026-08-10)

- log_store area = LK mblock-R reserved area: `0x7ffbf000 size 0x40000 map:1 name:log_store`.
- LK boot first line log `PL_LOG_STORE: set ram_header->sig = 0xABCD1234` = LK initializes this area.
- Hardware evidence: `stage=1` appears in expdb kernel log section (no printk prefix)—that is the XAGR ring buffer text restored by LK, immediately following `xaga-marker-writer: XAGR ring armed` (printk).
- Kernel log section of expdb = kernel log buffer capture (printk, starting from "XAGR ring armed" at setup_arch head) + log_store area restored content (ring buffer text) spliced together.

## Usage

1. Flash kernel with built-in XAGR ring buffer writer (`CONFIG_XAGA_MARKER_WRITER=y`).
2. Boot to any stage (including hang / panic / WDT reboot).
3. Dump expdb partition after reboot (via LK side method), the tail of kernel log contains the last section of printk + ring buffer content (`MIRROR:n` heartbeat, `stage=n` marker).

## Writer Implementation (K-tree `drivers/misc/xaga-marker-writer.c`, Built-in)

- `xaga_marker_early_init()`: At `setup_arch` head (first thing after `early_fixmap_init()` + `early_ioremap_init()`) calls `early_ioremap(0x7ffbf000, 0x10000)`, resets magic/cursor/total/stage, and writes `stage=1`. This is the **earliest** mapping point allowed by arm64 MMU (before paging_init linear mapping is not established, 0x7ffbf000 has no mapping, cannot be earlier).
- `xaga_marker_early_printk()`: Top hook in `vprintk_emit()` (kernel/printk/printk.c), mirrors every printk into the ring buffer via `va_copy + vscnprintf` (56KB rolling, lockless & allocation-free, safe in any printk context, messages after panic also enter ring). Writes `MIRROR:n` heartbeat every 64 messages.
- Ring layout (matches lineage reader): `u32 magic 0x52474158 @0x0000 / cursor @0x0004 / total @0x0008 / stage @0x1000 / text ring @0x2000 (0xE000 bytes)`.
- Re-asserts magic on every ring_write (protects header against aee/mrdump overwrite).
- Module notifier retained: Last line when vendor module probe hangs = module name.

## Excluded Candidate Areas (None Usable)

| Area | Address | Reason |
|---|---|---|
| minirdump | 0x48170000 | Writing triggers MTK mrdump mechanism to **reboot immediately** (tested 2026-08-09) |
| pstore | 0x48090000 | Occupied by kernel ramoops (`ramoops: using 0xe0000@0x48090000`) |
| aee_lk | 0x50700000 | **LK does not restore this area** (ring placed here yields empty expdb content, tested 2026-08-10) |
| gz-log / atf-log | 0x7f200000 / 0xbfe00000 | TEE/ATF reserved areas, NS access causes SError panic |
| log_store itself | 0x7ffbf000 | **Only usable option** (automatically restored and recorded by LK) |

## Gotchas

- **"Before setup_arch" is physically impossible on arm64**: 0x7ffbf000 has no mapping before paging_init; `early_ioremap` (fixmap, same mechanism as earlycon) is the earliest hardware-permitted path, mapping is persistent.
- **`CONFIG_EARLY_PRINTK` is dead code on arm64**: Dummy functions in printk.h, no arm64 Kconfig, nobody registers `early_console`. Actual early printk flow = `vprintk_emit` hook.
- **`early ioremap leak of 1 areas` warning is normal** (intentionally retained mapping for writing to ring throughout execution).
- **merge_config -m only adds, never modifies**: Disabling GKI base symbols (KVM / features / KASAN) must be overridden via sed in `build.sh config()` with if-grep assertions (`grep && {...}` will kill script under set -e if grep doesn't match, must use if form).
- **Kernel log captured by expdb starts from setup_arch head** ("XAGR ring armed"), earlier banner/start_kernel early printks are not present (log buffer capture start point).
- Other boot fixes on 6.12 porting chain (accompanying this method, indispensable): PKVM removal (xaga lacks hardware virtualization), SMCCC TRNG/SOC_ID probe guards, aee_aed module (otherwise init first-stage loading aee_rs aborts due to missing `aee_get_mode` symbol).

## Related Files

- Writer: K-tree `drivers/misc/xaga-marker-writer.c` + `include/linux/xaga_marker.h`
- Hooks: K-tree `kernel/printk/printk.c` (top of vprintk_emit) + `arch/arm64/kernel/setup.c` (setup_arch head calls `xaga_marker_early_init()`)
- **Reading Method = Direct inspection of expdb dump** (LK PL_LOG_STORE restores log_store area content into expdb, rebooting and dumping expdb partition yields ring text, no reader code needed)
- Reader Code (for reference/backup only): lineage_xaga `drivers/misc/xaga-marker.c`
- Build Wiring: Porting tree `build.sh` (config sed + module assertion) + `arch/arm64/configs/vendor/xaga.config`
- **Patch Set (Exported 2026-08-11)**: `xaga/patches-lk-log/` (5 git format-patch files, including README)—K-tree writer chain + smccc pairing, can be applied sequentially via `git am` from OPPO baseline `c5d442d1d`; `git am` verified byte-for-byte identical with K-tree HEAD `84a4857b1`

## oops Partition kmsg_dumper (Finalized 2026-08-13, Second Channel)

- Writer: K-tree `drivers/misc/xaga-dumpregs.c` (built-in, `CONFIG_XAGA_DUMPREGS=y`, set by xaga.config). Registers `kmsg_dumper` (`max_reason=KMSG_DUMP_OOPS`, triggered by both oops + panic), actively calls `kmsg_dump_desc()` inside die notifier (earlier than mrdump drowning logs), writing **complete dmesg (up to 512KiB)** to `/dev/block/sdc81` (oops partition, 16MB) via panic-safe polled bio.
- Layout: sdc81 header 4096B = 64B `XGAD` header (magic/version/reason/len/ts) + empty; log text starts from byte 4096 (full page 4096B aligned—UFS rejects non-4KB aligned bio, oops context write chain fixed: kzalloc buffer / virt_addr_valid / one bio per page / IRQ window).
- Reading: `dd if=/dev/block/sdc81 bs=1 count=64 | xxd` (XGAD header), `dd if=/dev/block/sdc81 bs=4096 skip=1 | head -c 20000` (dmesg text).
- Partition Opening: delayed_work 200ms polls `/dev/block/by-name/oops` -> `/dev/block/sdc81` (devtmpfs automatically creates node, does not depend on init).
- Complementary with XAGR ring: XAGR ring (log_store->expdb) covers early boot / hangs; oops partition covers full dmesg during crash (including oops stack, module loading sequence, last log before crash).
