#!/usr/bin/env python3
"""vendor_boot v4 的 **vrt（vendor ramdisk table）自检 / 修正**。

## 背景：BRINGUP 里那条「patch vrt 偏移 43,077,632 处 4 字节为 42,741,855」是什么

vendor_boot header v4 的数据区排布（AOSP `system/tools/mkbootimg` 的写盘顺序）：

    ┌────────────────────┬───────────┬───────────┬───────────┬────────────┐
    │ header(2128→page)  │ ramdisk#0 │ ramdisk#1 │  dtb      │ vrt 表     │ bootconfig
    └────────────────────┴───────────┴───────────┴───────────┴────────────┘
     每段都按 page_size 向上取整。于是

       vrt_offset = round_up(header_size) + Σ round_up(ramdisk_size[i]) + round_up(dtb_size)

     43,077,632 = 4096(header) + round_up(42,741,855, 4096) + round_up(327,372, 4096)
                = 4096 + 42,741,760 + 327,680   ← 只有当 ramdisk 是 42,741,855 时成立

    vrt 表第一项（v4 entry，108 字节）的**头 4 字节就是 `vendor_ramdisk_size`**：

        struct vendor_ramdisk_table_entry_v4 {
            uint32 size;            ← vrt_offset + 0   「那 4 字节」
            uint32 ramdisk_offset;  ← vrt_offset + 4
            uint32 ramdisk_type;
            char   ramdisk_name[32];
            uint32 board_id[16];
        };

    所以那条 patch 的含义是：**vrt 表里记录的 ramdisk 长度必须是真正写进去的
    lz4 流字节数（不含量化 padding）**。手工往原厂镜像里 dd 一个新 ramdisk 之后，
    这个字段（以及 header 里的 `vendor_ramdisk_size`）还留着旧值，LK 会按旧长度去解压
    → 解压失败 / 读到垃圾。

用 mkbootimg 正常打包时这两个字段是自动算对的；本脚本的价值是：
  - 打包后做**独立复算**（不信工具，自己按规范重算一遍布局并对齐字段）；
  - 手工补丁/裁剪过镜像时用 `--fix` 只改这 4 字节（以及 header 对应字段）。

用法：
  vrt-check.py <vendor_boot.img>            # 只检查，不一致 → 退出码 1
  vrt-check.py <vendor_boot.img> --fix      # 不一致就就地修正（先备份 .vrt-bak）
"""
import argparse
import os
import shutil
import struct
import sys

PAGE_ALIGN = 4096  # v4 header 固定写到 4096（BOOT_IMAGE_HEADER_V3_PAGESIZE）

# vendor_boot header v4 字段偏移
H_MAGIC = 0
H_VERSION = 8
H_PAGE_SIZE = 12
H_VENDOR_RAMDISK_SIZE = 24
H_HEADER_SIZE = 2096
H_DTB_SIZE = 2100
H_VRT_TABLE_SIZE = 2112
H_VRT_ENTRY_NUM = 2116
H_VRT_ENTRY_SIZE = 2120
H_BOOTCONFIG_SIZE = 2124
VRT_ENTRY_V4_SIZE = 108


def align(x, page):
    return (x + page - 1) // page * page


def lz4_legacy_length(buf, start, limit):
    """从 buf[start] 起解析 LZ4 legacy 帧，返回 (帧字节数, 块数, 解压后估算长度或是 None)。"""
    magic = struct.unpack('<I', buf[start:start + 4])[0]
    if magic != 0x184C2102:
        return None, 0, None
    off = start + 4
    blocks = 0
    while off + 4 <= limit:
        sz = struct.unpack('<I', buf[off:off + 4])[0]
        if sz == 0 or sz > 0x7E000000:
            break
        off += 4 + sz
        blocks += 1
        if off > limit:
            return None, blocks, None
    return off - start, blocks, None


def read_header(img):
    with open(img, 'rb') as fh:
        head = fh.read(4096)
    if len(head) < 2128:
        die('%s 太小，不是 vendor_boot' % img)
    magic = head[:8].rstrip(b'\x00').decode('ascii', 'replace')
    if magic != 'VNDRBOOT':
        die('%s magic=%r，不是 vendor_boot 镜像' % (img, magic))
    hv = struct.unpack('<I', head[H_VERSION:H_VERSION + 4])[0]
    page = struct.unpack('<I', head[H_PAGE_SIZE:H_PAGE_SIZE + 4])[0]
    rd_size = struct.unpack('<I', head[H_VENDOR_RAMDISK_SIZE:H_VENDOR_RAMDISK_SIZE + 4])[0]
    hdr_size = struct.unpack('<I', head[H_HEADER_SIZE:H_HEADER_SIZE + 4])[0]
    dtb_size = struct.unpack('<I', head[H_DTB_SIZE:H_DTB_SIZE + 4])[0]
    tsize = struct.unpack('<I', head[H_VRT_TABLE_SIZE:H_VRT_TABLE_SIZE + 4])[0]
    tnum = struct.unpack('<I', head[H_VRT_ENTRY_NUM:H_VRT_ENTRY_NUM + 4])[0]
    esize = struct.unpack('<I', head[H_VRT_ENTRY_SIZE:H_VRT_ENTRY_SIZE + 4])[0]
    bcsize = struct.unpack('<I', head[H_BOOTCONFIG_SIZE:H_BOOTCONFIG_SIZE + 4])[0]
    return dict(magic=magic, version=hv, page=page, ramdisk_size=rd_size,
                header_size=hdr_size, dtb_size=dtb_size, table_size=tsize,
                entry_num=tnum, entry_size=esize, bootconfig_size=bcsize)


def die(msg):
    print('vrt-check: ERROR: %s' % msg, file=sys.stderr)
    sys.exit(2)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image', nargs='?')
    ap.add_argument('--fix', action='store_true', help='不一致时就地修正（先备份）')
    ap.add_argument('--page', type=int, default=0, help='覆盖 header 里的 page_size')
    ap.add_argument('--calc', action='store_true',
                    help='只按公式算布局（配合 --ramdisk/--dtb/--page），用于复算历史偏移')
    ap.add_argument('--ramdisk', type=int, default=0, help='--calc 用：ramdisk 帧长')
    ap.add_argument('--dtb', type=int, default=0, help='--calc 用：dtb 段长度')
    args = ap.parse_args(argv)

    if args.calc:
        page = args.page or 4096
        hdr = 2128
        hdr_pad = align(hdr, page)
        vrt = hdr_pad + align(args.ramdisk, page) + align(args.dtb, page)
        print('按 v4 布局公式复算（page=%d, header=2128→%d）：' % (page, hdr_pad))
        print('  round_up(ramdisk=%d) = %d' % (args.ramdisk, align(args.ramdisk, page)))
        print('  round_up(dtb=%d)     = %d' % (args.dtb, align(args.dtb, page)))
        print('  vrt 偏移 = %d + %d + %d = %d'
              % (hdr_pad, align(args.ramdisk, page), align(args.dtb, page), vrt))
        print('  → vrt entry[0].size 必须写 %d（实测 LZ4 帧长，不是页对齐长度 %d）'
              % (args.ramdisk, align(args.ramdisk, page)))
        return 0

    if not args.image:
        die('需要 vendor_boot.img 路径（或用 --calc --ramdisk N --dtb M --page P）')
    h = read_header(args.image)
    page = args.page or h['page']
    size = os.path.getsize(args.image)
    print('== vendor_boot v4 布局复算: %s ==' % args.image)
    print('  header_version  : %d' % h['version'])
    print('  page_size       : %d' % page)
    print('  header_size     : %d' % h['header_size'])
    print('  ramdisk_size(h) : %d' % h['ramdisk_size'])
    print('  dtb_size        : %d' % h['dtb_size'])
    print('  vrt: entry_num=%d entry_size=%d table_size=%d' %
          (h['entry_num'], h['entry_size'], h['table_size']))
    print('  bootconfig_size : %d' % h['bootconfig_size'])
    print('  file size       : %d' % size)

    probe_page = page if page else PAGE_ALIGN
    start = align(h['header_size'], probe_page)
    vrt_off = start + align(h['ramdisk_size'], probe_page) + align(h['dtb_size'], probe_page)
    print('  推算布局        : ramdisk@%d, dtb@%d, vrt@%d, bootconfig@%d' % (
        start, start + align(h['ramdisk_size'], probe_page), vrt_off,
        vrt_off + align(h['table_size'], probe_page)))

    ok = True
    # 1) 魔数/版本/尺寸字段自洽
    if h['version'] != 4:
        print('  ✗ header_version=%d（本脚本只处理 v4）' % h['version'])
        ok = False
    if h['entry_size'] != VRT_ENTRY_V4_SIZE:
        print('  ✗ vrt entry_size=%d，v4 应为 %d' % (h['entry_size'], VRT_ENTRY_V4_SIZE))
        ok = False
    if h['table_size'] != h['entry_num'] * h['entry_size']:
        print('  ✗ table_size(%d) != entry_num*entry_size(%d)'
              % (h['table_size'], h['entry_num'] * h['entry_size']))
        ok = False

    # 2) 读 vrt 表，逐项校验 size/offset 累加
    with open(args.image, 'rb') as fh:
        fh.seek(vrt_off)
        table = fh.read(h['table_size'])
    entries = []
    acc = 0
    for i in range(h['entry_num']):
        e = table[i * h['entry_size']:(i + 1) * h['entry_size']]
        esz, eoff, etype = struct.unpack('<3I', e[:12])
        entries.append((esz, eoff, etype))
        print('    entry[%d]: size=%d offset=%d type=0x%x' % (i, esz, eoff, etype))
        if eoff != acc:
            print('  ✗ entry[%d].offset=%d，应为累加值 %d' % (i, eoff, acc))
            ok = False
        acc += esz
    if acc != h['ramdisk_size']:
        print('  ✗ Σentry.size=%d != header.vendor_ramdisk_size=%d' % (acc, h['ramdisk_size']))
        ok = False

    # 3) 实测：ramdisk 段里的 LZ4 legacy 帧到底多长（这才是「应该写进 vrt 的值」）
    actual = None
    with open(args.image, 'rb') as fh:
        fh.seek(start)
        buf = fh.read(min(size - start, 0x8000000))
    if buf[:4] == b'\x02\x21\x4c\x18':
        actual, blocks, _ = lz4_legacy_length(buf, 0, len(buf))
        if actual:
            print('  实测 LZ4 legacy 帧: %d 字节, %d 个 block' % (actual, blocks))
    else:
        print('  ramdisk 段不是 LZ4 legacy 帧头（前 4 字节 %s），跳过实测长度校验' % buf[:4].hex())
    if actual is not None and entries:
        if entries[0][0] != actual:
            print('  ✗ vrt entry[0].size=%d != 实测 LZ4 帧长 %d  → LK 会按错长度解压'
                  % (entries[0][0], actual))
            ok = False
        if h['ramdisk_size'] != actual:
            print('  ✗ header.vendor_ramdisk_size=%d != 实测 LZ4 帧长 %d'
                  % (h['ramdisk_size'], actual))
            ok = False

    # 4) 文件大小与布局自洽（末尾 bootconfig 之后应正好结束或只有 padding）
    tail_end = vrt_off + align(h['table_size'], probe_page) + align(h['bootconfig_size'], probe_page)
    if size < tail_end:
        print('  ✗ 文件 %d 字节 < 推算布局末端 %d 字节（镜像被截断）' % (size, tail_end))
        ok = False
    else:
        print('  文件尾部余量    : %d 字节' % (size - tail_end))

    if ok:
        print('  ✓ vrt 与 header 完全自洽（ramdisk 长度=%d）' % h['ramdisk_size'])
        return 0

    if not args.fix:
        print('vrt-check: 不一致；加 --fix 可修正 vrt/header 里的 ramdisk 长度', file=sys.stderr)
        return 1

    # ---- 修正：把实测 LZ4 帧长写回 header 与 vrt entry[0]，并重排 entry offset ----
    if actual is None:
        die('无法实测 ramdisk 帧长，拒绝 --fix')
    shutil.copy2(args.image, args.image + '.vrt-bak')
    with open(args.image, 'r+b') as fh:
        fh.seek(H_VENDOR_RAMDISK_SIZE)
        fh.write(struct.pack('<I', actual))
        fh.seek(H_DTB_SIZE)
        dtb_size = struct.unpack('<I', fh.read(4))[0]
        new_vrt = align(h['header_size'], probe_page) + align(actual, probe_page) + align(dtb_size, probe_page)
        # entry[0]
        fh.seek(new_vrt)
        esz, eoff, etype = struct.unpack('<3I', fh.read(12))
        fh.seek(new_vrt)
        fh.write(struct.pack('<I', actual))
        if eoff != 0:
            fh.seek(new_vrt + 4)
            fh.write(struct.pack('<I', 0))
    print('  已修正：header.vendor_ramdisk_size 与 vrt entry[0].size → %d（offset=%d，备份 %s.vrt-bak）'
          % (actual, new_vrt, args.image))
    return 0


if __name__ == '__main__':
    sys.exit(main())
