"""Build a board2-only FSBL tuning-boundary fix from the verified native image.

HAL currently rejects PhaseOffset == 8 even though this means all 8 phases
have been measured. Allow the final pass to select the valid window (0..6).
Equivalent source correction in AlMmc_Hal_Init:
  Ret == AL_MMC_ERR_TUNING_FAIL && PhaseOffset <= MMC_TOP_NS_CFG_CTRL_CLK_PHASE_SEL_MAX
The original ELF is retained; one verified AArch64 conditional branch is changed.
No flashing is performed by this script.
"""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess

root = Path(__file__).resolve().parents[1]
original = root.parent / 'display_profiles/board2_20261024/native-release'
out = root / 'release/boot-fix'
out.mkdir(exist_ok=True)
data = bytearray((original / 'fsbl.elf').read_bytes())
assert hashlib.sha256(data).hexdigest() == '4135a4de77aa9e0515d1f4ed02410d8c756c72eb317515270d81e3556929f5d7'
# ELF64 PT_LOAD mapping: discover file offset from program headers.
phoff = struct.unpack_from('<Q', data, 32)[0]
entsize, count = struct.unpack_from('<HH', data, 54)
address = 0x61007b6c
offset = None
for i in range(count):
    typ, flags, off, va, pa, size, memsize, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * entsize)
    if typ == 1 and va <= address < va + size:
        offset = off + address - va
assert offset is not None
assert struct.unpack_from('<I', data, offset)[0] == 0x540003c0 # b.eq error
struct.pack_into('<I', data, offset, 0x540003c8) # b.hi error (reject > 8)
(out / 'fsbl.elf').write_bytes(data)
for name in ('system.bit', 'u-boot.bin', 'boot.bif'):
    shutil.copy2(original / name, out / name)
cmd = [r'D:\FPGA\FD\tools\bootgen\albg1.exe', '-arch', 'dr1m90', '-image', 'boot.bif', '-o', 'BOOT.fixed.bin', '-w', 'on']
run = subprocess.run(cmd, cwd=out, capture_output=True, text=True, check=True)
(out / 'bootgen.log').write_text(run.stdout + run.stderr)
before = (original / 'BOOT.native.bin').read_bytes()
after = (out / 'BOOT.fixed.bin').read_bytes()
assert len(before) == len(after)
ranges = []
for i, (a, b) in enumerate(zip(before, after)):
    if a != b:
        if ranges and ranges[-1][0] + len(ranges[-1][1]) == i:
            ranges[-1][1].append(b)
        else:
            ranges.append([i, [b]])
manifest = {'original_sha256': hashlib.sha256(before).hexdigest(),
            'fixed_sha256': hashlib.sha256(after).hexdigest(),
            'elf_sha256': hashlib.sha256(data).hexdigest(),
            'changed_bytes': sum(len(x[1]) for x in ranges), 'ranges': ranges}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2))
script = '''#!/bin/sh
set -eu
boot=/mnt/mmcblk1p1
backup=/root/monitor-ui-backup-20261025
test "$(cat /sys/class/graphics/fb0/virtual_size)" = 1024,600
test "$(sha256sum "$boot/BOOT.bin" | cut -d ' ' -f 1)" = @OLD@
mkdir -p "$backup"
test -f "$backup/BOOT.before-ui.bin" || cp "$boot/BOOT.bin" "$backup/BOOT.before-ui.bin"
cp "$boot/BOOT.bin" "$boot/BOOT.monitor-new.bin"
'''.replace('@OLD@', manifest['original_sha256'])
for off, values in ranges:
    octal = ''.join('\\%03o' % value for value in values)
    script += "printf '%s' | dd of=\"$boot/BOOT.monitor-new.bin\" bs=1 seek=%d conv=notrunc 2>/dev/null\n" % (octal, off)
script += '''test "$(sha256sum "$boot/BOOT.monitor-new.bin" | cut -d ' ' -f 1)" = @NEW@
sync
mv "$boot/BOOT.monitor-new.bin" "$boot/BOOT.bin"
sync
sha256sum "$boot/BOOT.bin"
echo BOOT_TUNING_FIX_INSTALLED
'''.replace('@NEW@', manifest['fixed_sha256'])
(out / 'install_boot_fix.sh').write_text(script, newline='\n')
print(json.dumps({k:v for k,v in manifest.items() if k != 'ranges'}, indent=2))
