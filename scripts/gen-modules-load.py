#!/usr/bin/env python3
"""从 .ko 的符号依赖生成有序 modules.load（不依赖 depmod，避开厂商栈的循环依赖）。

做法：读每个 .ko 的「导出全局符号」与「未定义符号」，若 A 引用 B 导出的符号则 A 依赖 B；
      拓扑排序（依赖在前），遇到环则按模块名顺序强行打断并记录，保证每个模块只出现一次。
用法：gen-modules-load.py <ko_dir> <out_file>
"""
import os, re, subprocess, sys, collections

import shutil
NM = os.environ.get('NM', shutil.which('llvm-nm') or shutil.which('nm') or 'nm')
ko_dir, out = sys.argv[1], sys.argv[2]
kos = sorted(f for f in os.listdir(ko_dir) if f.endswith('.ko'))
name = {f: f[:-3] for f in kos}

exports, undefined = {}, {}
for f in kos:
    p = os.path.join(ko_dir, f)
    try:
        defined = subprocess.run([NM, '-g', '--defined-only', p],
                                 capture_output=True, text=True, timeout=60).stdout
        undef = subprocess.run([NM, '-u', p], capture_output=True, text=True, timeout=60).stdout
    except Exception:
        defined, undef = '', ''
    exports[name[f]] = {l.split()[-1] for l in defined.splitlines() if l.split()}
    undefined[name[f]] = {l.split()[-1] for l in undef.splitlines() if l.split()}

owner = {}
for m, syms in exports.items():
    for s in syms:
        owner.setdefault(s, m)

deps = collections.defaultdict(set)
for m, syms in undefined.items():
    for s in syms:
        o = owner.get(s)
        if o and o != m:
            deps[m].add(o)

order, state, cycles = [], {}, []
def visit(m):
    st = state.get(m)
    if st == 2:
        return
    if st == 1:          # 环：打断
        cycles.append(m)
        return
    state[m] = 1
    for d in sorted(deps.get(m, ())):
        visit(d)
    state[m] = 2
    order.append(m)

for m in sorted(deps):
    visit(m)
for m in sorted(exports):
    visit(m)

# 关键子系统优先加载（时钟/引脚/存储/PMIC/总线/IPI），保证 init 第一阶段拿到块设备
CRITICAL = re.compile(r'^(clk|pinctrl|ufs|phy-|scsi|cqhci|spmi|mtk-pmic|regulator|aee|mtk_ipi|mtk_tinysys|mtk-mbox|mtk-smi|mtk-smi-dbg|dramc|mtk_dramc|dvfsrc|i2c|spi|soc|mtk-icc|hwccf|clk-common|clkchk)')
prio = sorted(range(len(order)), key=lambda i: (0 if CRITICAL.match(order[i]) else 1, i))
order = [order[i] for i in prio]

with open(out, 'w') as fh:
    for m in order:
        fh.write(m + '\n')

# 同步生成 modules.dep / modules.alias / modules.softdep
# （init 的 libmodprobe 依赖这两个文件做别名与依赖解析；缺了会导致
#  "LoadWithAliases was unable to load xxx" 并中断后续模块加载）
base = os.path.dirname(out)
with open(os.path.join(base, 'modules.dep'), 'w') as fh:
    for m in order:
        deps_l = sorted(d for d in deps.get(m, ()))
        fh.write(m + '.ko:' + ''.join(' ' + d + '.ko' for d in deps_l) + '\n')

alias_lines = []
mi = os.environ.get('MODINFO', 'modinfo')
for m in order:
    try:
        r = subprocess.run([mi, '-F', 'alias', os.path.join(ko_dir, m + '.ko')],
                           capture_output=True, text=True, timeout=30).stdout
        for a in r.splitlines():
            if a.strip():
                alias_lines.append(f"alias {a.strip()} {m}")
    except Exception:
        pass
with open(os.path.join(base, 'modules.alias'), 'w') as fh:
    fh.write('\n'.join(alias_lines) + ('\n' if alias_lines else ''))
with open(os.path.join(base, 'modules.softdep'), 'w') as fh:
    fh.write('')
print(f"   modules.alias: {len(alias_lines)} 条")
print(f"   modules.load: {len(order)} 条；依赖边 {sum(len(v) for v in deps.values())} 条；"
      f"环处打断 {len(cycles)} 次")
