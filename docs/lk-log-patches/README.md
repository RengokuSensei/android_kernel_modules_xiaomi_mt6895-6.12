# xaga LK 抓日志链路补丁集

> 生成自 xaga 6.12 移植内核（`xaga/android_kernel_oddo_mt6895`，分支
> `oppo/mt6896_b_16.0_reno11`），基线 = OPPO 同步 commit `c5d442d1d`。
> 真机验证：2026-08-10 expdb2 起稳定工作（MIRROR 心跳 + stage 标记随 expdb 恢复）。

## 原理（一句话）

LK 的 PL_LOG_STORE 机制在每次启动时把 DRAM log_store 保留区
（`0x7ffbf000`, 256KB）的内容恢复记录到 expdb 分区。本补丁集让内核把
printk 流镜像进该保留区的 XAGR 环，于是**任何内核阶段（含挂死/WDT 重启）
的日志尾部都会随下一次启动的 expdb 出现**——无需自建读端、不受
`console=ttynull` 影响。

## 应用

按编号顺序 `git am`（或 `git apply` 逐个应用）：

```sh
cd <OPPO 6.12 kernel tree>     # 基线 = c5d442d1d（OPPO CPH2607 同步 commit）
git am 0001-*.patch 0002-*.patch 0003-*.patch 0004-*.patch 0005-*.patch
```

## 补丁清单

| # | commit | 内容 |
|---|--------|------|
| 0001 | `c3bf6dda5` | **写端核心**：`drivers/misc/xaga-marker-writer.c`（CONFIG_XAGA_MARKER_WRITER=y）+ `include/linux/xaga_marker.h`；`setup_arch` 头 `early_ioremap(0x7ffbf000)` 武装环；`printk.c` `vprintk_emit()` 顶部镜像每个 printk 进环；Kconfig/Makefile 接线 |
| 0002 | `c2ae5f617` | **配套启动修复**：`drivers/firmware/smccc/smccc.c` 跳过 SMCCC TRNG/SOC_ID probe（5.10 时代 ATF 会挂在这些调用上） |
| 0003 | `bf2cbe08c` | 环址尝试迁到 aee_lk 0x50740000（LK 不恢复该区，真机验证失败） |
| 0004 | `682fed54f` | 回退 0003，环回 log_store 0x7ffbf000（**最终态**） |
| 0005 | `84a4857b1` | **MIRROR 心跳**：每 64 条 printk 写 `MIRROR:n` 进环，证明镜像存活 |

0003/0004 保留在历史里以记录环址决策；最终生效态 = 0001 + 0002 + 0005。

## 配套（不在本补丁集，仓库内已含）

- 移植树 `xaga.config`：`CONFIG_XAGA_MARKER_WRITER=y`
- 移植树 `build.sh`：merge 后断言 `CONFIG_XAGA_MARKER_WRITER=y`
- 文档：移植树 `xaga-log-capture.md`（机制细节 / 已排除区表 / 坑）

## 环布局（写端与读取端必须一致）

```
u32 magic 0x52474158 @0x0000
u32 cursor          @0x0004
u32 total           @0x0008
u32 stage           @0x1000
文本环               @0x2000，0xE000 字节
```

## 读取

重启后 dump expdb 分区（`xaga/expdb`，128MB raw），内核日志段从
"XAGR ring armed" 开始，环文本（`stage=n`、`MIRROR:n`、模块加载序列）在
panic/挂死点之前。5.10 侧读取端（lineage_xaga `drivers/misc/xaga-marker.c`）
仅为备用，非必需。

## 验证记录

- `git am` 顺序应用 5 补丁 → 最终树与 K 树 HEAD `84a4857b1` 逐字节一致。
- 2026-08-10 真机：expdb2 内核日志段含 `stage=1` + `MIRROR:64..576` 全序列，
  证明 log_store 区内容被 LK 恢复进 expdb。
