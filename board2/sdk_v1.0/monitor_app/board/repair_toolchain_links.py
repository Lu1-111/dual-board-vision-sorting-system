"""Restore shared-library symlinks lost in a Windows SDK extraction, in WSL copy only."""
from pathlib import Path
import re
import subprocess

root = Path.home() / 'monitor-build/toolchain'
assert root.is_dir() and '/monitor-build/' in str(root)
for p in list(root.rglob('*')):
    if not p.is_file() or p.is_symlink() or '.so' not in p.name or p.stat().st_size == 0:
        continue
    result = subprocess.run(['readelf', '-d', str(p)], capture_output=True, text=True)
    match = re.search(r'\(SONAME\).*\[(.*?)\]', result.stdout)
    if not match:
        continue
    for name in {match[1], match[1].split('.so')[0] + '.so'}:
        link = p.parent / name
        if link == p or link.is_symlink():
            continue
        if link.exists() and link.stat().st_size:
            continue
        if link.exists():
            link.unlink()
        link.symlink_to(p.name)

libc = root / 'aarch64-linux-gnu/libc'
for name in ('libc.so', 'libpthread.so'):
    link = libc / 'lib' / name
    script = libc / 'usr/lib' / name
    if link.is_symlink() and script.is_file() and not script.is_symlink() and script.stat().st_size:
        link.unlink()  # Preserve the GNU ld script's nonshared archive dependency.
for p in (libc / 'usr/lib').glob('*.so'):
    if p.is_file() and not p.is_symlink() and p.stat().st_size == 0:
        target = libc / 'lib' / p.name
        if target.exists():
            p.unlink()
            p.symlink_to('../../lib/' + target.name)
print('Restored shared-library links in', root)
