#!/usr/bin/env python3
# usage: check_syms.py Module.symvers modules_dir modules.load
import os, subprocess, sys

if len(sys.argv) < 4:
    print(f"Usage: {sys.argv[0]} <Module.symvers> <modules_dir> <modules.load>", file=sys.stderr)
    sys.exit(1)

symvers, moddir, loadfile = sys.argv[1:4]
norm = lambda n: os.path.basename(n).removesuffix('.ko').replace('-', '_')

owner = {}  # symbol -> provider ('vmlinux' or module normalized name)
if os.path.exists(symvers):
    for line in open(symvers, errors='ignore'):
        f = line.rstrip('\n').split('\t')
        if len(f) >= 3:
            owner[f[1]] = 'vmlinux' if f[2] == 'vmlinux' else norm(f[2])
else:
    print(f"WARNING: symvers file '{symvers}' not found!", file=sys.stderr)

mods = [l.strip() for l in open(loadfile) if l.strip()]
pos = {norm(m): i for i, m in enumerate(mods)}

# Detect nm binary
nm_bin = os.environ.get('NM')
if not nm_bin:
    for candidate in ['llvm-nm', 'aarch64-linux-gnu-nm', 'nm']:
        if subprocess.run(['which', candidate], capture_output=True).returncode == 0:
            nm_bin = candidate
            break
if not nm_bin:
    nm_bin = 'nm'

errors = 0
checked_modules = 0

for ko in mods:
    me = norm(ko)
    ko_filename = ko if ko.endswith('.ko') else (ko + '.ko')
    ko_path = os.path.join(moddir, ko_filename)
    if not os.path.isfile(ko_path):
        continue

    checked_modules += 1
    out = subprocess.run([nm_bin, '-u', ko_path], capture_output=True, text=True).stdout
    for l in out.splitlines():
        p = l.split()
        if len(p) != 2 or p[0] != 'U':  # skip weak symbols or non-undefined
            continue
        sym = p[1]
        prov = owner.get(sym)
        if prov is None:
            print(f"[ERROR] {me}: undefined symbol '{sym}' -> exported by nobody!")
            errors += 1
        elif prov not in ('vmlinux', me) and pos.get(prov, 10**9) >= pos[me]:
            prov_pos = pos.get(prov, 'NOT_IN_MODULES_LOAD')
            print(f"[ERROR] {me} (pos {pos[me]}): symbol '{sym}' provided by '{prov}' (pos {prov_pos}), but '{prov}' is NOT loaded before it!")
            errors += 1

print(f"\ncheck_syms summary: checked {checked_modules} modules in {loadfile}, {errors} unresolved/ordering error(s).")
if errors > 0:
    sys.exit(1)
sys.exit(0)
