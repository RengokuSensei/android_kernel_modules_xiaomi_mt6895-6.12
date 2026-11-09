#!/usr/bin/env python3
"""选出一个「符号完全自洽」的模块子集。

背景：Android init 加载 modules.load 时，**任何一个模块加载失败都会让 init 退出**，
      进而 "Attempted to kill init" 内核 panic。所以只能发布那些未定义符号
      全部能被「内核导出 + 本子集内其它模块」满足的模块。

算法：不动点迭代 —— 先取所有对内核闭合的模块，再反复加入依赖已满足的模块，直到收敛。
      （等价于对有向依赖图求「可加载闭包」；环内模块只要外部依赖满足也一并纳入。）
用法：select-modules.py <ko_dir> <kernel_symvers> <out_list> [required...]
"""
import os, subprocess, sys

import shutil
NM = os.environ.get('NM', shutil.which('llvm-nm') or shutil.which('nm') or 'nm')
ko_dir, ksymvers, out_list = sys.argv[1], sys.argv[2], sys.argv[3]
required = sys.argv[4:]

kernel = set()
for line in open(ksymvers, encoding='utf-8', errors='replace'):
    p = line.split()
    if len(p) >= 2:
        kernel.add(p[1])

kos = sorted(f for f in os.listdir(ko_dir) if f.endswith('.ko'))
exports, undef = {}, {}
for f in kos:
    p = os.path.join(ko_dir, f)
    d = subprocess.run([NM, '-g', '--defined-only', p], capture_output=True, text=True).stdout
    u = subprocess.run([NM, '-u', p], capture_output=True, text=True).stdout
    exports[f[:-3]] = {l.split()[-1] for l in d.splitlines() if l.split()}
    undef[f[:-3]] = {l.split()[-1] for l in u.splitlines() if l.split()}

all_names = set(exports)
# 每个符号的候选提供者
provider = {}
for m, syms in exports.items():
    for s in syms:
        provider.setdefault(s, set()).add(m)

selected = set()
changed = True
while changed:
    changed = False
    avail = set(kernel)
    for m in selected:
        avail |= exports[m]
    for m in all_names - selected:
        miss = {s for s in undef[m] if s not in avail}
        # 缺口要么为空，要么只能由已经在集合里的模块补（否则等下一轮）
        if not miss:
            selected.add(m); changed = True
        elif all(provider.get(s, set()) & selected for s in miss):
            selected.add(m); changed = True

# 保证必需模块在集合内（若其依赖不满足则报错，避免静默缺失）
missing_required = [r for r in required if r not in selected]
with open(out_list, 'w') as fh:
    for m in sorted(selected):
        fh.write(m + '.ko\n')

print(f"   可加载闭包: {len(selected)}/{len(all_names)} 个模块")
if missing_required:
    print(f"   ⚠ 必需模块未进闭包: {' '.join(missing_required)}")
dropped = sorted(all_names - selected)
print(f"   丢弃（符号不闭合）: {len(dropped)} 个: {' '.join(dropped[:12])}{' ...' if len(dropped) > 12 else ''}")
