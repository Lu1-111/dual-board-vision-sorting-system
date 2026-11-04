#!/usr/bin/env python3
"""Preserve disjoint damage in Qt 5.13.2; argument is the qtbase directory."""
import pathlib
import sys

path = pathlib.Path(sys.argv[1]) / 'src/platformsupport/fbconvenience/qfbwindow.cpp'
original = path.read_text()
marker = '// DR1_DISJOINT_DAMAGE'
if marker in original:
    print('Already patched:', path)
    sys.exit(0)
old = ('    const QRect dirtyClient = region.boundingRect();\n'
       '    const QRect dirtyRegion = dirtyClient.translated(currentGeometry.topLeft());\n')
call = '    platformScreen()->setDirty(dirtyRegion);'
if original.count(old) != 1 or original.count(call) != 1:
    raise SystemExit('Unexpected source; refusing to patch ' + str(path))
updated = original.replace(old, '').replace(call,
    '    ' + marker + '\n'
    '    for (const QRect &dirtyClient : region)\n'
    '        platformScreen()->setDirty(dirtyClient.translated(currentGeometry.topLeft()));')
backup = path.with_suffix('.cpp.pre-damage')
if backup.exists() and backup.read_text() != original:
    raise SystemExit('Backup differs; refusing to overwrite ' + str(backup))
if not backup.exists():
    backup.write_text(original)
path.write_text(updated)
print('Patched:', path)
