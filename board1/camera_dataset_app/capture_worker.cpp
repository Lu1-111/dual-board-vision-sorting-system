#include "capture_worker.hpp"

#include <QElapsedTimer>
#include <QFont>
#include <QFile>
#include <QLockFile>
#include <QMutexLocker>
#include <QPainter>
#include <QSaveFile>
#include <QSysInfo>
#include <cerrno>
#include <cstring>
#include <exception>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "vendor/mipicam_module.h"
#include "vendor/imx415_regs.h"
#endif

namespace {
const int Width = 1920;
const int Height = 1080;
const unsigned FrameBytes = Width * Height * 4;

QString systemError(const QString &operation)
{
    const int error = errno;
    return operation + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(error));
}

QImage artificialFrame(quint64 sequence)
{
    QImage image(Width, Height, QImage::Format_RGB32);
    if (image.isNull()) return image;
    image.fill(QColor(24, 28, 36));
    QPainter painter(&image);
    const QColor colors[] = { Qt::white, Qt::yellow, Qt::cyan, Qt::green,
                              Qt::magenta, Qt::red, Qt::blue, Qt::black };
    for (int i = 0; i < 8; ++i)
        painter.fillRect(i * Width / 8, 0, Width / 8, Height / 2, colors[i]);
    painter.setPen(QPen(Qt::white, 5));
    painter.setBrush(QColor(35, 170, 225));
    painter.drawEllipse(QPoint(200 + int(sequence * 9 % 1500), 760), 110, 110);
    painter.fillRect(1260, 660, 270, 240, QColor(235, 130, 45));
    painter.fillRect(0, Height - 110, Width, 110, QColor(120, 0, 0));
    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Sans Serif"), 28, QFont::Bold));
    painter.drawText(image.rect().adjusted(35, 0, -35, -20),
                     Qt::AlignBottom | Qt::AlignLeft,
                     QStringLiteral("SIMULATION - ARTIFICIAL IMAGE - Frame %1").arg(sequence));
    return image;
}

#ifdef Q_OS_LINUX
const char ResetMarker[] = "/tmp/camera_dataset_app-reset-required";

bool readCurrentBootId(QByteArray *bootId, QString *error)
{
    QFile boot(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    if (!boot.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取系统启动标识，不能安全建立 DMA 超时保护：") + boot.errorString();
        return false;
    }
    *bootId = boot.read(129).trimmed();
    if (bootId->isEmpty() || bootId->size() > 128) {
        *error = QStringLiteral("系统启动标识无效，不能安全建立 DMA 超时保护");
        return false;
    }
    return true;
}

QString rememberResetRequired(const QByteArray &bootId)
{
    QSaveFile marker(QString::fromLatin1(ResetMarker));
    marker.setDirectWriteFallback(false);
    const QByteArray contents = bootId + '\n';
    if (!marker.open(QIODevice::WriteOnly) || marker.write(contents) != contents.size()
        || !marker.commit()) {
        return QStringLiteral("；无法写入重启保护标记：%1。请勿重新启动采集程序，必须先重启开发板")
            .arg(marker.errorString());
    }
    return QString();
}

int sensorRead(int fd, uint16_t reg, uint8_t *value)
{
    uint8_t address[] = { uint8_t(reg >> 8), uint8_t(reg & 255) };
    struct i2c_msg messages[] = { { 0x1a, 0, 2, address }, { 0x1a, I2C_M_RD, 1, value } };
    struct i2c_rdwr_ioctl_data request = { messages, 2 };
    return ioctl(fd, I2C_RDWR, &request) == 2 ? 0 : -1;
}

int sensorWrite(int fd, uint16_t reg, uint8_t value, bool verify)
{
    uint8_t bytes[] = { uint8_t(reg >> 8), uint8_t(reg & 255), value };
    struct i2c_msg message = { 0x1a, 0, 3, bytes };
    struct i2c_rdwr_ioctl_data request = { &message, 1 };
    if (ioctl(fd, I2C_RDWR, &request) != 1) return -1;
    if (verify) {
        uint8_t readback = 0;
        if (sensorRead(fd, reg, &readback)) return -1;
        if (readback != value) { errno = EIO; return -1; }
    }
    return 0;
}

int cameraWrite(int fd, uint32_t offset, uint32_t value)
{
    mipicam_rw_info_t request = { offset, value };
    return ioctl(fd, MIPICAM_IOCTL_WRITE_REG, &request);
}

// This object never releases the driver-owned DMA allocation. The reference
// driver retains that storage even if a DMA times out while PL is still writing.
struct CameraResources {
    int camera = -1;
    int i2c = -1;
    void *pixels = MAP_FAILED;
    unsigned bytes = 0;
    bool sensorTouched = false;
    ~CameraResources() {
        if (sensorTouched && i2c >= 0) sensorWrite(i2c, 0x3000, 1, false);
        if (pixels != MAP_FAILED) munmap(pixels, bytes);
        if (i2c >= 0) close(i2c);
        if (camera >= 0) close(camera);
    }
};
#endif
}

CaptureWorker::CaptureWorker(bool simulation, QObject *parent)
    : QThread(parent), m_simulation(simulation)
{
    qRegisterMetaType<SaveResult>("SaveResult");
}

CaptureWorker::~CaptureWorker()
{
    stop();
    // No terminate(): an in-flight DMA must finish or reach the driver's 1500 ms
    // timeout before resources are closed. Disk saves also finish atomically.
    wait();
}

bool CaptureWorker::requestPhoto(const SaveOptions &options)
{
    QMutexLocker locker(&m_mutex);
    if (!m_accepting || m_photoBusy || isInterruptionRequested()) return false;
    m_options = options;
    m_photoBusy = true;
    m_pending = true;
    return true;
}

void CaptureWorker::stop()
{
    {
        QMutexLocker locker(&m_mutex);
        m_accepting = false;
    }
    requestInterruption();
}

void CaptureWorker::acknowledgePreview()
{
    QMutexLocker locker(&m_mutex);
    m_previewOutstanding = false;
}

bool CaptureWorker::pause(unsigned milliseconds)
{
    for (unsigned elapsed = 0; elapsed < milliseconds && !isInterruptionRequested();) {
        const unsigned step = qMin(10U, milliseconds - elapsed);
        msleep(step);
        elapsed += step;
    }
    return !isInterruptionRequested();
}

void CaptureWorker::completePhoto(const SaveResult &result)
{
    {
        QMutexLocker locker(&m_mutex);
        m_pending = false;
        m_photoBusy = false;
    }
    emit photoSaved(result);
}

void CaptureWorker::cancelPending(const QString &reason)
{
    bool busy;
    {
        QMutexLocker locker(&m_mutex);
        m_accepting = false;
        busy = m_photoBusy;
    }
    if (busy) {
        SaveResult result;
        result.error = reason;
        completePhoto(result);
    }
}

void CaptureWorker::run()
{
    QString failure;
    QElapsedTimer previewClock;
    quint64 frameId = 0;
#ifdef Q_OS_LINUX
    // Fixed shared path keeps two copies of this app from racing the legacy
    // driver's open() check. Other camera applications are rejected by open().
    QLockFile cameraLock(QStringLiteral("/tmp/camera_dataset_app-mipicam.lock"));
    cameraLock.setStaleLockTime(0);
    CameraResources hardware;
    mipicam_frame_buffer_t buffer = {};
    QByteArray currentBootId;
#endif
    emit statusChanged(m_simulation ? QStringLiteral("模拟模式：正在生成测试画面")
                                    : QStringLiteral("正在初始化 IMX415 相机"));
    try {
        if (!m_simulation) {
#ifdef Q_OS_LINUX
            // A timed-out DMA has no safe userspace cancel/reset operation in
            // the reference driver. A boot-scoped marker also protects a newly
            // launched process from mistaking a late IRQ for its own new frame.
            if (requiresHardwareReset()) {
                failure = QStringLiteral("DMA 状态未复位，必须先重启开发板才能重新采集");
            } else {
                readCurrentBootId(&currentBootId, &failure);
            }
            if (failure.isEmpty() && !cameraLock.tryLock(0)) {
                failure = QStringLiteral("相机已被其他采集程序占用，或无法创建相机锁文件");
            }
            // Check while holding the same lock used by the writer, so another
            // process cannot fault and publish a marker between check and open.
            if (failure.isEmpty()) {
                QFile marker(QString::fromLatin1(ResetMarker));
                if (marker.exists()) {
                    if (!marker.open(QIODevice::ReadOnly)) {
                        failure = QStringLiteral("无法读取 DMA 重启保护标记：") + marker.errorString();
                    } else if (marker.read(129).trimmed() == currentBootId) {
                        m_requiresHardwareReset.store(true);
                        failure = QStringLiteral("本次系统启动中曾发生 DMA 完成异常；必须先重启开发板，不能直接重开相机");
                    }
                }
            }
            if (failure.isEmpty() && QSysInfo::ByteOrder != QSysInfo::LittleEndian) {
                failure = QStringLiteral("当前硬件 BGRA 布局仅支持小端 Linux");
            } else if (failure.isEmpty()) {
                hardware.camera = open("/dev/mipicam", O_RDWR | O_CLOEXEC);
                if (hardware.camera < 0)
                    failure = systemError(QStringLiteral("无法独占打开 /dev/mipicam（请关闭其他预览程序）"));
            }
            mipicam_reg_info_t registers = {};
            if (failure.isEmpty() &&
                ioctl(hardware.camera, MIPICAM_IOCTL_GET_REGISTER_INFO, &registers))
                failure = systemError(QStringLiteral("读取相机寄存器信息失败"));
            if (failure.isEmpty() &&
                (registers.addr != 0x80300000ULL || registers.size < 12))
                failure = QStringLiteral("相机控制地址或寄存器范围与 DR1M 工程不匹配");
            if (failure.isEmpty() &&
                ioctl(hardware.camera, MIPICAM_IOCTL_GET_FRAME_BUFFER, &buffer))
                failure = systemError(QStringLiteral("获取 DMA 缓冲区失败（需要配套 mipicam 驱动）"));
            if (failure.isEmpty() &&
                (buffer.size != FrameBytes || !buffer.phys_addr || buffer.phys_addr > 0xffffffffULL ||
                 (buffer.phys_addr & 3) ||
                 buffer.phys_addr + buffer.size > 0x100000000ULL))
                failure = QStringLiteral("DMA 缓冲区不是有效的 1920×1080 BGRA / 32 位地址范围");
            if (failure.isEmpty()) {
                hardware.bytes = buffer.size;
                hardware.pixels = mmap(nullptr, buffer.size, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, hardware.camera, 0);
                if (hardware.pixels == MAP_FAILED)
                    failure = systemError(QStringLiteral("映射 DMA 缓冲区失败"));
            }
            if (failure.isEmpty()) {
                hardware.i2c = open("/dev/i2c-0", O_RDWR | O_CLOEXEC);
                if (hardware.i2c < 0) failure = systemError(QStringLiteral("打开 /dev/i2c-0 失败"));
            }
            uint8_t standby = 0;
            if (failure.isEmpty() && sensorRead(hardware.i2c, 0x3000, &standby))
                failure = systemError(QStringLiteral("无法读取 IMX415（I2C 0 / 地址 0x1a）"));
            if (failure.isEmpty()) {
                for (unsigned r = 0; r < sizeof(sensor_regs) / sizeof(sensor_regs[0]); ++r) {
                    if (isInterruptionRequested()) break;
                    const sensor_reg &reg = sensor_regs[r];
                    hardware.sensorTouched = true;
                    if (sensorWrite(hardware.i2c, reg.addr, reg.value, true)) {
                        failure = systemError(QStringLiteral("IMX415 寄存器 0x%1 写入/读回校验失败")
                                              .arg(reg.addr, 4, 16, QLatin1Char('0')));
                        break;
                    }
                    if (reg.addr == 0x3000 && reg.value == 0) pause(18);
                    if (reg.addr == 0x3002 && reg.value == 0) pause(320);
                }
            }
#else
            failure = QStringLiteral("真实相机采集需要 DR1M 板上的 Linux；可显式选择模拟模式");
#endif
        }
        if (failure.isEmpty() && !isInterruptionRequested()) {
            {
                QMutexLocker locker(&m_mutex);
                m_accepting = !isInterruptionRequested();
                m_previewOutstanding = false;
            }
            emit statusChanged(m_simulation ? QStringLiteral("模拟模式就绪 · 1920×1080 · 非真实采集")
                                            : QStringLiteral("相机就绪 · IMX415 · 1920×1080"));
            previewClock.start();
            while (!isInterruptionRequested()) {
                SaveOptions saveOptions;
                bool saveThisFrame = false;
                {
                    QMutexLocker locker(&m_mutex);
                    if (m_pending) {
                        saveOptions = m_options;
                        m_pending = false;
                        saveThisFrame = true;
                    }
                }
                // Selecting the request before DMA start guarantees a request
                // arriving mid-capture is fulfilled by the NEXT complete frame.
                QImage frame;
                if (m_simulation) {
                    if (!pause(35)) break;
                    frame = artificialFrame(frameId + 1);
                } else {
#ifdef Q_OS_LINUX
                    if (ioctl(hardware.camera, MIPICAM_IOCTL_SYNC_FRAME_DEVICE, 0)) {
                        failure = systemError(QStringLiteral("DMA 设备缓存同步失败"));
                        break;
                    }
                    if (cameraWrite(hardware.camera, 0, uint32_t(buffer.phys_addr)) ||
                        cameraWrite(hardware.camera, 4, 1)) {
                        failure = systemError(QStringLiteral("启动相机采集失败"));
                        break;
                    }
                    mipicam_intr_info_t interrupt = {};
                    interrupt.timeout = 1500;
                    if (ioctl(hardware.camera, MIPICAM_IOCTL_WAIT_INTERRUPT, &interrupt)) {
                        failure = systemError(QStringLiteral("相机帧完成等待失败（最长 1500 毫秒）"));
                        m_requiresHardwareReset.store(true);
                        failure += QStringLiteral("；当前驱动没有安全的 DMA 取消/复位接口，必须先重启开发板");
                        failure += rememberResetRequired(currentBootId);
                        break;
                    }
                    if (!interrupt.intr_flag) {
                        m_requiresHardwareReset.store(true);
                        failure = QStringLiteral("驱动未确认完整帧；当前驱动没有安全的 DMA 取消/复位接口，必须先重启开发板");
                        failure += rememberResetRequired(currentBootId);
                        break;
                    }
                    if (ioctl(hardware.camera, MIPICAM_IOCTL_SYNC_FRAME_CPU, 0)) {
                        failure = systemError(QStringLiteral("DMA CPU 缓存同步失败"));
                        break;
                    }
                    // Own the frame BEFORE the next DMA can overwrite its data.
                    frame = QImage(static_cast<const uchar *>(hardware.pixels), Width,
                                   Height, Width * 4, QImage::Format_RGB32).copy();
                    // PL's fourth byte is padding, not a valid alpha channel.
                    // RGB32 requires it to be 0xff; leave RGB samples untouched.
                    if (!frame.isNull()) {
                        for (int y = 0; y < Height; ++y) {
                            QRgb *row = reinterpret_cast<QRgb *>(frame.scanLine(y));
                            for (int x = 0; x < Width; ++x) row[x] |= 0xff000000U;
                        }
                    }
#endif
                }
                const QDateTime capturedUtc = QDateTime::currentDateTimeUtc();
                ++frameId;
                if (frame.isNull()) {
                    failure = QStringLiteral("无法分配完整图像内存");
                    break;
                }
                if (isInterruptionRequested()) break;
                emit streamFrameReady(frame, capturedUtc, frameId);
                bool sendPreview = false;
                {
                    QMutexLocker locker(&m_mutex);
                    if (!m_previewOutstanding && (frameId == 1 || previewClock.elapsed() >= 100)) {
                        m_previewOutstanding = true;
                        sendPreview = true;
                    }
                }
                if (sendPreview) {
                    previewClock.restart();
                    emit frameReady(frame, capturedUtc, frameId);
                }
                if (saveThisFrame) {
                    SaveResult saved;
                    try {
                        saved = DatasetStore::save(frame, saveOptions, capturedUtc, frameId,
                                                  m_simulation ? QStringLiteral("simulation")
                                                               : QStringLiteral("imx415"));
                    } catch (const std::exception &error) {
                        saved.error = QStringLiteral("保存失败：") + QString::fromLocal8Bit(error.what());
                    } catch (...) {
                        saved.error = QStringLiteral("保存失败：未知文件写入错误");
                    }
                    completePhoto(saved);
                }
            }
        }
    } catch (const std::exception &error) {
        failure = QStringLiteral("采集错误：") + QString::fromLocal8Bit(error.what());
    } catch (...) {
        failure = QStringLiteral("采集发生未知错误");
    }
    cancelPending(failure.isEmpty() ? QStringLiteral("采集已停止，本次拍照已取消") : failure);
    if (!failure.isEmpty()) emit captureError(failure);
    emit statusChanged(failure.isEmpty() ? QStringLiteral("采集已停止") : QStringLiteral("采集失败"));
}
