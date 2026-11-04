// Bounded linuxfb regression probe. Reads the framebuffer; never accesses GPIO.
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static const QColor bars[] = {
    QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255),
    QColor(255, 255, 0), QColor(0, 255, 255), QColor(255, 0, 255)
};

class Probe : public QWidget
{
public:
    explicit Probe(QScreen *screen) : screen_(screen)
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setCursor(Qt::BlankCursor);
        setGeometry(screen_->geometry());
    }

    ~Probe() override { if (fd_ >= 0) ::close(fd_); }

    bool initialize()
    {
        fd_ = ::open("/dev/fb0", O_RDONLY | O_CLOEXEC);
        if (fd_ < 0 || ::ioctl(fd_, FBIOGET_VSCREENINFO, &v_) < 0 ||
            ::ioctl(fd_, FBIOGET_FSCREENINFO, &f_) < 0) {
            std::fprintf(stderr, "FAIL framebuffer open/ioctl: %s\n", std::strerror(errno));
            return false;
        }
        std::printf("FB %ux%u virtual=%ux%u offset=%u,%u bpp=%u stride=%u "
                    "R=%u/%u G=%u/%u B=%u/%u\n",
                    v_.xres, v_.yres, v_.xres_virtual, v_.yres_virtual,
                    v_.xoffset, v_.yoffset, v_.bits_per_pixel, f_.line_length,
                    v_.red.offset, v_.red.length, v_.green.offset, v_.green.length,
                    v_.blue.offset, v_.blue.length);
        if (f_.type != FB_TYPE_PACKED_PIXELS || f_.visual != FB_VISUAL_TRUECOLOR ||
            (v_.bits_per_pixel != 16 && v_.bits_per_pixel != 24 && v_.bits_per_pixel != 32) ||
            !validField(v_.red) || !validField(v_.green) || !validField(v_.blue) ||
            screen_->devicePixelRatio() != 1.0 ||
            screen_->size() != QSize(int(v_.xres), int(v_.yres)) ||
            v_.xres < 120 || v_.yres < 120) {
            std::fprintf(stderr, "FAIL unsupported format, scaling, or screen size\n");
            return false;
        }
        return true;
    }

    void start()
    {
        showFullScreen();
        // Allow the Qt event loop and linuxfb compositor to flush before sampling.
        QTimer::singleShot(650, this, [this] { runStage(); });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        // Qt's system clip restricts these fills to the requested dirty region.
        for (int i = 0; i < 6; ++i) {
            const int left = i * width() / 6;
            const int right = (i + 1) * width() / 6;
            painter.fillRect(QRect(left, 0, right - left, height()), bars[i]);
        }
        for (const Patch &patch : patches_)
            painter.fillRect(patch.rect, patch.color);
    }

private:
    struct Patch { QRect rect; QColor color; };

    bool validField(const fb_bitfield &field) const
    {
        return field.length > 0 && field.length <= 16 &&
               field.offset + field.length <= v_.bits_per_pixel;
    }

    static int component(uint32_t pixel, const fb_bitfield &field)
    {
        const uint32_t mask = (uint32_t(1) << field.length) - 1;
        uint32_t value = (pixel >> field.offset) & mask;
        if (field.msb_right) {
            uint32_t reversed = 0;
            for (unsigned i = 0; i < field.length; ++i) {
                reversed = (reversed << 1) | (value & 1);
                value >>= 1;
            }
            value = reversed;
        }
        return int((value * 255 + mask / 2) / mask);
    }

    bool readPixel(QPoint point, QColor &color)
    {
        const unsigned bytes = v_.bits_per_pixel / 8;
        const uint64_t offset = uint64_t(point.y() + v_.yoffset) * f_.line_length +
                                uint64_t(point.x() + v_.xoffset) * bytes;
        if (offset + bytes > f_.smem_len) return false;
        unsigned char raw[4] = {};
        if (::pread(fd_, raw, bytes, off_t(offset)) != ssize_t(bytes)) return false;
        uint32_t pixel = 0;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        for (unsigned i = 0; i < bytes; ++i) pixel |= uint32_t(raw[i]) << (i * 8);
#else
        for (unsigned i = 0; i < bytes; ++i) pixel = (pixel << 8) | raw[i];
#endif
        color = QColor(component(pixel, v_.red), component(pixel, v_.green),
                       component(pixel, v_.blue));
        return true;
    }

    QColor expected(QPoint point) const
    {
        for (auto it = patches_.rbegin(); it != patches_.rend(); ++it)
            if (it->rect.contains(point)) return it->color;
        for (int i = 0; i < 6; ++i)
            if (point.x() < (i + 1) * width() / 6) return bars[i];
        return QColor();
    }

    bool verify()
    {
        const QImage capture = screen_->grabWindow(0).toImage();
        if (capture.isNull() || capture.size() != size()) {
            std::fprintf(stderr, "FAIL stage=%d grabWindow size=%dx%d expected=%dx%d\n",
                         stage_, capture.width(), capture.height(), width(), height());
            return false;
        }
        unsigned errors = 0;
        unsigned samples = 0;
        for (int i = 0; i < 6; ++i) {
            const int x = (2 * i + 1) * width() / 12;
            // Upper centers cover patches; lower centers remain untouched.
            // Extra points immediately outside each patch detect dirty-region spill.
            const QPoint points[] = {
                QPoint(x, height() / 4), QPoint(x, 3 * height() / 4),
                QPoint(x - width() / 24 - 2, height() / 4),
                QPoint(x + width() / 24 + 2, height() / 4)
            };
            for (QPoint point : points) {
                const QColor want = expected(point);
                QColor physical;
                const QColor screenshot = capture.pixelColor(point);
                const bool readable = readPixel(point, physical);
                ++samples;
                if (!readable || physical.rgb() != want.rgb() || screenshot.rgb() != want.rgb()) {
                    ++errors;
                    std::fprintf(stderr,
                        "FAIL stage=%d xy=%d,%d expected=%06x fb=%06x readable=%d grab=%06x\n",
                        stage_, point.x(), point.y(), unsigned(want.rgb() & 0xffffff),
                        unsigned(physical.rgb() & 0xffffff), int(readable),
                        unsigned(screenshot.rgb() & 0xffffff));
                }
            }
        }
        std::printf("stage=%d samples=%u framebuffer+grabWindow %s errors=%u\n",
                    stage_, samples, errors ? "FAIL" : "PASS", errors);
        std::fflush(stdout);
        return errors == 0;
    }

    void runStage()
    {
        if (!verify()) { QApplication::exit(1); return; }
        if (stage_ == 3) {
            std::puts("PASS all stages: full frame, partial change, repeated partial redraw, second partial change");
            QApplication::exit(0);
            return;
        }
        QRegion dirty;
        if (stage_ == 0 || stage_ == 2) {
            const int first = stage_ == 0 ? 1 : 0;
            for (int i = first; i < 6; i += 2) {
                const int x = (2 * i + 1) * width() / 12;
                const QRect rect(x - width() / 24, height() / 4 - height() / 16,
                                 width() / 12, height() / 8);
                patches_.push_back(Patch{rect, bars[(i + 2) % 6]});
                dirty += rect;
            }
        } else {
            // Repaint identical colors to catch accidental in-place double swaps.
            for (const Patch &patch : patches_) dirty += patch.rect;
        }
        ++stage_;
        update(dirty);
        QTimer::singleShot(550, this, [this] { runStage(); });
    }

    QScreen *screen_;
    int fd_ = -1;
    fb_var_screeninfo v_ = {};
    fb_fix_screeninfo f_ = {};
    int stage_ = 0;
    std::vector<Patch> patches_;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (!app.primaryScreen()) return 2;
    Probe probe(app.primaryScreen());
    if (!probe.initialize()) return 2;
    QTimer::singleShot(4500, &app, [&app] {
        std::fprintf(stderr, "FAIL timeout\n");
        app.exit(2);
    });
    probe.start();
    return app.exec();
}
