#!/usr/bin/env python3
"""Apply the narrowly scoped Qt 5.13.2 packed 24-bit BGR framebuffer fix.

Usage: python3 patch_linuxfb.py /path/to/qtbase/src/plugins/platforms/linuxfb
The compositor stays RGB888. Only BGR framebuffer stores and screen grabs swap.
Original files are retained beside the sources; reruns are idempotent.
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
changes = {
    "qlinuxfbscreen.h": [
        ("    int mOldTtyMode;", "    int mOldTtyMode;\n    bool mSwapRgb24 = false; // DR1_PACKED_BGR24_FIX")
    ],
    "qlinuxfbscreen.cpp": [
        ("    mFormat = determineFormat(vinfo, mDepth);", """    mFormat = determineFormat(vinfo, mDepth);
    // RGB888 is byte-ordered R,G,B; fb bit offsets refer to a native integer.
    // Qt 5.13 has no QImage::Format_BGR888, so convert only at the fb boundary.
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    mSwapRgb24 = mDepth == 24 && vinfo.red.offset == 16
            && vinfo.green.offset == 8 && vinfo.blue.offset == 0;
#else
    mSwapRgb24 = mDepth == 24 && vinfo.red.offset == 0
            && vinfo.green.offset == 8 && vinfo.blue.offset == 16;
#endif
    mSwapRgb24 = mSwapRgb24 && mFormat == QImage::Format_RGB888;
    qInfo("linuxfb: %d bpp, R/G/B offsets %u/%u/%u, packed BGR24 conversion %s",
          mDepth, vinfo.red.offset, vinfo.green.offset, vinfo.blue.offset,
          mSwapRgb24 ? "enabled" : "disabled"); // DR1_PACKED_BGR24_FIX"""),
        ("    if (!mBlitter)\n        mBlitter = new QPainter(&mFbScreenImage);", """    if (mSwapRgb24) {
        // Always read the unswapped compositor. Repeated dirty rectangles must
        // not swap the framebuffer in place or previously drawn colors toggle.
        for (const QRect &dirty : touched) {
            const QRect rect = dirty.intersected(mScreenImage.rect());
            for (int y = rect.top(); y <= rect.bottom(); ++y) {
                const uchar *src = mScreenImage.constScanLine(y) + rect.left() * 3;
                uchar *dst = mMmap.data + y * mBytesPerLine + rect.left() * 3;
                for (int x = 0; x < rect.width(); ++x, src += 3, dst += 3) {
                    dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
                }
            }
        }
        return touched;
    }

    if (!mBlitter)
        mBlitter = new QPainter(&mFbScreenImage);"""),
        ("        return QPixmap::fromImage(mFbScreenImage).copy(x, y, width, height);", """        QImage grabbed = mFbScreenImage.copy(x, y, width, height);
        return QPixmap::fromImage(mSwapRgb24 ? grabbed.rgbSwapped() : grabbed);"""),
        ("        return QPixmap::fromImage(mFbScreenImage).copy(rect);", """        QImage grabbed = mFbScreenImage.copy(rect);
        return QPixmap::fromImage(mSwapRgb24 ? grabbed.rgbSwapped() : grabbed);""")
    ]
}
for name, edits in changes.items():
    path = root / name
    original = path.read_text()
    if "DR1_PACKED_BGR24_FIX" in original:
        print("Already patched:", path)
        continue
    text = original
    for old, new in edits:
        if text.count(old) != 1:
            raise SystemExit("Unexpected source; refusing to patch " + str(path))
        text = text.replace(old, new)
    backup = path.with_suffix(path.suffix + ".pre-bgr24")
    if backup.exists():
        if backup.read_text() != original:
            raise SystemExit("Backup differs; refusing to overwrite " + str(backup))
    else:
        backup.write_text(original)
    path.write_text(text)
    print("Patched:", path)
