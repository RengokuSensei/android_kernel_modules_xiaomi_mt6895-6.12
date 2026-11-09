#!/usr/bin/env python3
"""体检：每个 .ko 的未定义符号是否都能被「内核导出 + 本集合内其它 .ko」满足。
输出会加载失败的模块及其缺口，按缺口数排序。"""
import os, subprocess, sys, collections
import shutil
NM = os.environ.get('NM', shutil.which('llvm-nm') or shutil.which('nm') or 'nm')
ko_dir = sys.argv[1]
kernel_symvers = sys.argv[2] if len(sys.argv) > 2 else 'out/Module.symvers'

kernel_syms = set()
if os.path.exists(kernel_symvers):
    for line in open(kernel_symvers, encoding='utf-8', errors='replace'):
        p = line.split()
        if len(p) >= 2:
            kernel_syms.add(p[1])

kos = sorted(f for f in os.listdir(ko_dir) if f.endswith('.ko'))
exports, undef = {}, {}
for f in kos:
    p = os.path.join(ko_dir, f)
    d = subprocess.run([NM, '-g', '--defined-only', p], capture_output=True, text=True).stdout
    u = subprocess.run([NM, '-u', p], capture_output=True, text=True).stdout
    exports[f[:-3]] = {l.split()[-1] for l in d.splitlines() if l.split()}
    undef[f[:-3]] = {l.split()[-1] for l in u.splitlines() if l.split()}

provider = {}
for m, syms in exports.items():
    for s in syms:
        provider.setdefault(s, m)

bad = {}
for m, syms in undef.items():
    missing = [s for s in syms if s not in kernel_syms and s not in provider]
    if missing:
        bad[m] = missing

print(f"模块总数 {len(kos)}；有缺口的 {len(bad)} 个\n")
for m, miss in sorted(bad.items(), key=lambda kv: -len(kv[1])):
    print(f"{m} ({len(miss)}): {' '.join(sorted(miss)[:6])}{' ...' if len(miss) > 6 else ''}")
