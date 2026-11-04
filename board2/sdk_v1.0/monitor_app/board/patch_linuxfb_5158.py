"""Correct little-endian packed RGB888/BGR888 interpretation for this framebuffer."""
from pathlib import Path
import sys
p = Path(sys.argv[1]) / 'src/plugins/platforms/linuxfb/qlinuxfbscreen.cpp'
s = p.read_text()
if 'BOARD2_BGR888' not in s:
    start = s.index('    case 24: {')
    end = s.index('    case 18:', start)
    block = s[start:end]
    assert block.count('format = QImage::Format_RGB888;') == 1
    assert block.count('format = QImage::Format_BGR888;') == 1
    block = block.replace('format = QImage::Format_RGB888;', '''// BOARD2_BGR888: fb bit offsets describe integers, QImage describes bytes.
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
            format = QImage::Format_BGR888;
#else
            format = QImage::Format_RGB888;
#endif''', 1)
    # The other branch was originally BGR888; replace only its original context.
    block = block.replace('''            format = QImage::Format_BGR888;
            // pixeltype = BGRPixel;''', '''#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
            format = QImage::Format_RGB888;
#else
            format = QImage::Format_BGR888;
#endif''')
    p.write_text(s[:start] + block + s[end:])
print('linuxfb BGR888 patch ready:', p)
