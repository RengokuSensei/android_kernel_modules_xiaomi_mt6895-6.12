#!/bin/bash
# 校验：覆盖层 dtbo 的全部 fixup 引用能否在基座 dtb 的 __symbols__ 中解析，
# 并把覆盖层实际叠加到基座，确认无冲突。
set -euo pipefail
WS=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
KBOUT=${KBOUT:-$WS/../out}
DTC=$KBOUT/scripts/dtc/dtc
FDTOVERLAY=$KBOUT/scripts/dtc/fdtoverlay
DTBDIR=${OUT:-$WS/../out/dtb}
BASE=$DTBDIR/xiaomi_mt6895_common.dtb
OV=$DTBDIR/xaga.dtbo

for f in "$BASE" "$OV"; do [ -f "$f" ] || { echo "缺少 $f，先运行 scripts/dtb.sh all"; exit 1; }; done

"$DTC" -I dtb -O dts "$BASE" > /tmp/.chk_base.dts 2>/dev/null
"$DTC" -I dtb -O dts "$OV"   > /tmp/.chk_ov.dts   2>/dev/null

python3 - /tmp/.chk_base.dts /tmp/.chk_ov.dts <<'PY'
import re,sys,io
def blk(path,name):
    t=io.open(path,encoding='utf-8',errors='replace').read()
    i=t.find('\t%s {'%name)
    if i<0: return ''
    j=t.find('{',i); depth=0; k=j
    while k<len(t):
        if t[k]=='{': depth+=1
        elif t[k]=='}':
            depth-=1
            if depth==0: return t[j+1:k]
        k+=1
    return ''
def props(t): return set(re.findall(r'^\s+([A-Za-z0-9_]+) = ',t,re.M))
base,ov=sys.argv[1],sys.argv[2]
sy=props(blk(base,'__symbols__'))
fx={l for l in props(blk(ov,'__fixups__')) if not re.match(r'^\d+|^[0-9a-f]{2}$',l)}
miss=sorted(l for l in fx if l not in sy)
print(f"基座符号 {len(sy)} 个, 覆盖层引用 {len(fx)} 个, 未解析 {len(miss)} 个")
if miss:
    print("未解析: "+" ".join(miss)); sys.exit(1)
print("标签引用校验通过 ✓")
PY

"$FDTOVERLAY" -i "$BASE" -o /tmp/.chk_merged.dtb "$OV"
echo "覆盖层叠加到基座成功 ✓  (merged: $(stat -c%s /tmp/.chk_merged.dtb) bytes)"
