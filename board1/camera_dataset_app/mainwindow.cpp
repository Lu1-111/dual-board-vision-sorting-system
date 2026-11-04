#include "mainwindow.hpp"
#include "capture_worker.hpp"
#include "latest_frame_server.hpp"
#include <QCloseEvent>
#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QDebug>
#include <QCryptographicHash>
#include <QUuid>
#include <cmath>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStorageInfo>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

class PreviewWidget : public QWidget {
public:
    explicit PreviewWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("previewLabel");
        setMinimumSize(320, 160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setFrame(const QImage &image) { m_frame = image; update(); }
    void clearFrame() { m_frame = QImage(); update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#080e18"));
        if (m_frame.isNull()) {
            painter.setPen(QColor("#8b9bb3"));
            painter.drawText(rect().adjusted(22, 22, -22, -22), Qt::AlignCenter,
                             QStringLiteral("相机尚未连接\n\n点击“打开相机”开始预览"));
            return;
        }
        QSize fitted = m_frame.size();
        fitted.scale(size(), Qt::KeepAspectRatio);
        const QRect area(QPoint((width() - fitted.width()) / 2,
                                (height() - fitted.height()) / 2), fitted);
        painter.drawImage(area, m_frame);
        // This framing aid is drawn only on screen; the worker saves its original frame.
        painter.setPen(QPen(QColor(61, 224, 197, 170), 1.5));
        const int inset = qMax(12, fitted.width() / 18);
        const QRect guide = area.adjusted(inset, inset, -inset, -inset);
        const int arm = qMin(28, guide.height() / 8);
        for (int x : {guide.left(), guide.right()}) {
            for (int y : {guide.top(), guide.bottom()}) {
                painter.drawLine(x, y, x + (x == guide.left() ? arm : -arm), y);
                painter.drawLine(x, y, x, y + (y == guide.top() ? arm : -arm));
            }
        }
    }
private:
    QImage m_frame;
};

namespace {
QLabel *caption(const QString &text, QWidget *parent = nullptr)
{
    QLabel *label = new QLabel(text, parent);
    label->setProperty("kind", "caption");
    return label;
}
QFrame *panel(QWidget *parent = nullptr)
{
    QFrame *frame = new QFrame(parent);
    frame->setObjectName("panel");
    return frame;
}
}

MainWindow::MainWindow(const AppOptions &options, QWidget *parent)
    : QMainWindow(parent), m_options(options)
{
    m_startupCapturePending = options.captureFirst;
    setWindowTitle(QStringLiteral("相机采集台 · DR1M90")
                   + (options.simulation ? QStringLiteral(" [模拟模式]") : QString()));
    setMinimumSize(1024, 600);
    resize(1280, 720);
    buildUi();
    m_guidanceTimer = new QTimer(this);
    m_guidanceTimer->setInterval(1000);
    connect(m_guidanceTimer, &QTimer::timeout, this, [this]() {
        if (m_guidanceSeconds > 0) --m_guidanceSeconds;
        if (m_guidanceSeconds == 0) {
            m_guidanceTimer->stop();
            if (m_guidancePhase == "prepare" || m_guidancePhase == "hold") {
                m_guidancePhase = "hold";
                m_guidanceText = QStringLiteral("请移开手并保持静止，等待电脑拍摄");
            }
        }
        updateGuidance();
    });
    QSettings settings;
    const auto preference = [&settings, &options](const QString &key, const QVariant &fallback) {
        return options.simulation || options.pcOnly ? fallback : settings.value(key, fallback);
    };
    const QString defaultRoot = options.simulation ? QStringLiteral("/tmp/camera_dataset_simulation")
                                                  : QStringLiteral("/home/uisrc/datasets");
    const QString defaultDataset = options.simulation ? QStringLiteral("simulation_dataset")
                                                     : QStringLiteral("vision_dataset");
    m_root->setText(options.output.isEmpty()
                   ? preference("root", defaultRoot).toString() : options.output);
    m_dataset->setText(options.dataset.isEmpty()
                      ? preference("dataset", defaultDataset).toString() : options.dataset);
    m_scene->setText(options.scene.isEmpty()
                    ? preference("scene", "unlabeled").toString() : options.scene);
    m_format->setCurrentIndex(preference("format", 0).toInt() == 1 ? 1 : 0);
    m_quality->setValue(preference("quality", options.pcOnly ? 80 : 95).toInt());
    m_interval->setValue(preference("interval", 1.0).toDouble());
    m_burstTimer = new QTimer(this);
    m_burstTimer->setTimerType(Qt::PreciseTimer);
    connect(m_burstTimer, &QTimer::timeout, this, [this]() {
        if (m_photoPending || !m_freshFrame) { ++m_skipped; return; }
        capturePhoto();
    });
    m_storageTimer = new QTimer(this);
    connect(m_storageTimer, &QTimer::timeout, this, &MainWindow::updateStorage);
    m_storageTimer->start(4000);
    connect(m_root, &QLineEdit::textChanged, this, &MainWindow::updateStorage);
    connect(m_open, &QPushButton::clicked, this, &MainWindow::openCamera);
    connect(m_close, &QPushButton::clicked, this, &MainWindow::closeCamera);
    connect(m_capture, &QPushButton::clicked, this, &MainWindow::capturePhoto);
    connect(m_startBurst, &QPushButton::clicked, this, &MainWindow::startBurst);
    connect(m_stopBurst, &QPushButton::clicked, this, &MainWindow::stopBurst);
    connect(m_browse, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(this,
                QStringLiteral("选择数据集保存位置"), m_root->text());
        if (!directory.isEmpty()) m_root->setText(directory);
    });
    connect(m_format, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateControls(); });
    QShortcut *shortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        // Keep spaces available while editing a path or a directory name.
        if (qobject_cast<QLineEdit *>(focusWidget())) return;
        if (m_capture->isEnabled()) capturePhoto();
    });
    updateControls();
    updateStorage();
    setStatus(options.simulation
              ? QStringLiteral("模拟模式：测试图像将记录为 simulation 来源。")
              : QStringLiteral("准备就绪。打开相机后可采集原始分辨率图片。"));
    if (options.stream) {
        m_stream = new LatestFrameServer(this);
        QString error;
        if (!m_stream->start(options.streamAddress, options.streamPort,
                             options.streamFps, options.streamQuality, error,
                             options.previewFps, options.previewWidth, options.previewQuality,
                             options.trackCenters, options.trackingOutputDir)) {
            qCritical().noquote() << "LIVE_STREAM_ERROR" << error;
            setStatus(QStringLiteral("直播服务启动失败：%1；相机拍照仍可使用。 ").arg(error), true);
            delete m_stream;
            m_stream = nullptr;
        } else {
            qInfo().noquote() << "LIVE_STREAM_LISTEN" << options.streamAddress
                              << options.streamPort << "fps" << options.streamFps;
        }
    }
    if (options.simulation || options.openOnStart || options.captureFirst)
        QTimer::singleShot(0, this, &MainWindow::openCamera);
}

MainWindow::~MainWindow()
{
    saveSettings();
    // Normal window close is deferred until finished(); never destroy a running QThread.
    if (m_worker) {
        m_worker->stop();
        m_worker->wait();
    }
    delete m_stream;
    m_stream = nullptr;
}

void MainWindow::buildUi()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget{background:#0e1624;color:#e5edf8;font-size:14px;}"
        "QFrame#panel{background:#152235;border:1px solid #26394f;border-radius:12px;}"
        "QLabel{background:transparent;border:none;}"
        "QLabel[kind=caption]{color:#8fa5bd;font-size:12px;}"
        "QLabel#title{font-size:25px;font-weight:600;}"
        "QLabel#count{font-size:35px;font-weight:700;color:#4de0bd;}"
        "QLabel#statusLabel{padding:10px;border-radius:8px;background:#18283b;}"
        "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:#0e1929;"
        "border:1px solid #35475e;border-radius:6px;padding:7px;min-height:22px;}"
        "QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus{border-color:#43d4b5;}"
        "QPushButton{background:#23374f;border:1px solid #3a506a;border-radius:8px;"
        "padding:10px 14px;min-height:24px;font-weight:600;}"
        "QPushButton:hover{background:#2e4764;}"
        "QPushButton:pressed{background:#13283c;}"
        "QPushButton#captureButton{background:#28c5a6;color:#071d22;border:0;font-size:19px;min-height:42px;}"
        "QPushButton#captureButton:hover{background:#51dfc0;}"
        "QPushButton#stopBurstButton{color:#ffb3a7;border-color:#7c4a4a;}"
        "QWidget:disabled{color:#596b80;}QPushButton:disabled{background:#1a293a;border-color:#26394c;}"
        "QScrollArea{border:0;background:transparent;}"
        "QScrollBar:vertical{background:#132035;width:10px;}"
        "QScrollBar::handle:vertical{background:#3a526b;border-radius:4px;min-height:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0px;}"));

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *outer = new QVBoxLayout(central);
    outer->setContentsMargins(22, 18, 22, 16);
    outer->setSpacing(14);
    QHBoxLayout *header = new QHBoxLayout;
    QVBoxLayout *branding = new QVBoxLayout;
    QLabel *title = new QLabel(QStringLiteral("视觉数据采集"));
    title->setObjectName("title");
    branding->addWidget(title);
    branding->addWidget(caption(m_options.pcOnly
        ? QStringLiteral("DR1M90 / IMX415     ·     照片直接传到电脑，板端不保存")
        : QStringLiteral("DR1M90 / IMX415     ·     拍摄图片，随后在电脑上标注检测框")));
    header->addLayout(branding);
    header->addStretch();
    QLabel *mode = new QLabel(m_options.simulation ? QStringLiteral("● 模拟模式 / 测试图像")
                                                  : QStringLiteral("● 真实相机 / IMX415"));
    mode->setStyleSheet(m_options.simulation ? "color:#ffc47e;font-weight:600;"
                                             : "color:#4de0bd;font-weight:600;");
    header->addWidget(mode);
    outer->addLayout(header);

    m_guidance = new QLabel;
    m_guidance->setObjectName("guidedCaptureLabel");
    m_guidance->setTextFormat(Qt::PlainText);
    m_guidance->setWordWrap(true);
    m_guidance->setAlignment(Qt::AlignCenter);
    m_guidance->setMinimumHeight(105);
    m_guidance->setMaximumHeight(180);
    m_guidance->hide();
    outer->addWidget(m_guidance);

    QHBoxLayout *body = new QHBoxLayout;
    body->setSpacing(16);
    QFrame *left = panel();
    QVBoxLayout *previewLayout = new QVBoxLayout(left);
    previewLayout->setContentsMargins(14, 14, 14, 14);
    QHBoxLayout *previewHeader = new QHBoxLayout;
    previewHeader->addWidget(caption(QStringLiteral("实时取景")));
    previewHeader->addStretch();
    m_cameraState = new QLabel(QStringLiteral("未连接"));
    m_cameraState->setObjectName("cameraStateLabel");
    previewHeader->addWidget(m_cameraState);
    previewLayout->addLayout(previewHeader);
    m_preview = new PreviewWidget;
    previewLayout->addWidget(m_preview, 1);
    m_frameInfo = caption(QStringLiteral("等待视频帧 · 保存原始分辨率，不含取景辅助线"));
    m_frameInfo->setObjectName("frameInfoLabel");
    m_frameInfo->setWordWrap(true);
    previewLayout->addWidget(m_frameInfo);
    QHBoxLayout *cameraButtons = new QHBoxLayout;
    m_open = new QPushButton(QStringLiteral("打开相机"));
    m_open->setObjectName("openCameraButton");
    m_close = new QPushButton(QStringLiteral("关闭相机"));
    m_close->setObjectName("closeCameraButton");
    cameraButtons->addWidget(m_open);
    cameraButtons->addWidget(m_close);
    previewLayout->addLayout(cameraButtons);
    QHBoxLayout *recent = new QHBoxLayout;
    m_lastImage = new QLabel(QStringLiteral("暂无照片"));
    m_lastImage->setObjectName("lastThumbnailLabel");
    m_lastImage->setAlignment(Qt::AlignCenter);
    m_lastImage->setFixedSize(116, 66);
    m_lastImage->setStyleSheet("background:#0b1421;border-radius:5px;color:#788fa8;");
    recent->addWidget(m_lastImage);
    QVBoxLayout *lastText = new QVBoxLayout;
    lastText->addWidget(caption(m_options.pcOnly ? QStringLiteral("最近传输") : QStringLiteral("最近保存")));
    m_lastPath = new QLabel(m_options.pcOnly ? QStringLiteral("照片仅在电脑保存")
                                            : QStringLiteral("成功拍照后在此显示文件位置"));
    m_lastPath->setObjectName("lastPathLabel");
    m_lastPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lastPath->setWordWrap(true);
    m_lastPath->setMinimumWidth(0);
    m_lastPath->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    lastText->addWidget(m_lastPath);
    recent->addLayout(lastText, 1);
    previewLayout->addLayout(recent);
    body->addWidget(left, 1);

    QFrame *right = panel();
    right->setFixedWidth(350);
    QVBoxLayout *controls = new QVBoxLayout(right);
    controls->setContentsMargins(18, 15, 18, 16);
    controls->setSpacing(8);
    QHBoxLayout *stats = new QHBoxLayout;
    QVBoxLayout *countLayout = new QVBoxLayout;
    countLayout->setSpacing(0);
    countLayout->addWidget(caption(m_options.pcOnly ? QStringLiteral("电脑已接收") : QStringLiteral("本次已保存")));
    m_count = new QLabel("0");
    m_count->setObjectName("count");
    countLayout->addWidget(m_count);
    stats->addLayout(countLayout);
    stats->addStretch();
    m_disk = caption(QStringLiteral("可用空间 —"));
    m_disk->setObjectName("diskSpaceLabel");
    m_disk->setWordWrap(true);
    stats->addWidget(m_disk);
    controls->addLayout(stats);

    // Settings can scroll on a 600px display; capture and stop stay visible below.
    QScrollArea *scroll = new QScrollArea;
    scroll->setObjectName("captureControlsScrollArea");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(60);
    QWidget *settingsContent = new QWidget;
    settingsContent->setObjectName("settingsContent");
    settingsContent->setStyleSheet("QWidget#settingsContent{background:transparent;}");
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 10, 0);
    settingsLayout->setSpacing(8);
    if (!m_options.pcOnly) settingsLayout->addWidget(caption(QStringLiteral("保存位置")));
    QHBoxLayout *rootRow = new QHBoxLayout;
    m_root = new QLineEdit;
    m_root->setObjectName("rootEdit");
    m_browse = new QPushButton(QStringLiteral("选择"));
    m_browse->setObjectName("browseRootButton");
    m_browse->setFixedWidth(68);
    m_browse->setStyleSheet("padding:7px 8px;");
    rootRow->addWidget(m_root, 1);
    rootRow->addWidget(m_browse);
    settingsLayout->addLayout(rootRow);
    m_root->setVisible(!m_options.pcOnly);
    m_browse->setVisible(!m_options.pcOnly);
    QFormLayout *form = new QFormLayout;
    form->setVerticalSpacing(9);
    m_dataset = new QLineEdit;
    m_dataset->setObjectName("datasetEdit");
    m_scene = new QLineEdit;
    m_scene->setObjectName("sceneEdit");
    form->addRow(QStringLiteral("数据集名称"), m_dataset);
    form->addRow(QStringLiteral("场景 / 批次"), m_scene);
    if (m_options.pcOnly) {
        m_dataset->hide(); m_scene->hide();
        form->labelForField(m_dataset)->hide(); form->labelForField(m_scene)->hide();
    }
    m_format = new QComboBox;
    m_format->setObjectName("formatCombo");
    m_format->addItem("JPEG", "jpg");
    m_format->addItem("PNG", "png");
    m_quality = new QSpinBox;
    m_quality->setObjectName("qualitySpin");
    m_quality->setRange(1, 100);
    form->addRow(QStringLiteral("图像格式"), m_format);
    form->addRow(QStringLiteral("JPEG 质量"), m_quality);
    settingsLayout->addLayout(form);
    QLabel *hint = caption(m_options.pcOnly
        ? QStringLiteral("照片先留在内存中，电脑校验并保存后才计数。USB 串口传输一张原图需要等待，请保持连接。")
        : QStringLiteral("场景 / 批次仅用于整理文件夹，不代表目标类别或检测标注。"));
    hint->setWordWrap(true);
    settingsLayout->addWidget(hint);
    settingsLayout->addStretch();
    scroll->setWidget(settingsContent);
    controls->addWidget(scroll, 1);
    m_capture = new QPushButton(m_options.pcOnly ? QStringLiteral("拍一张并传到电脑")
                                               : QStringLiteral("拍一张   ·   Space"));
    m_capture->setObjectName("captureButton");
    controls->addWidget(m_capture);
    QFormLayout *intervalForm = new QFormLayout;
    m_interval = new QDoubleSpinBox;
    m_interval->setObjectName("intervalSpin");
    m_interval->setRange(0.2, 3600.0);
    m_interval->setDecimals(1);
    m_interval->setSingleStep(0.2);
    m_interval->setSuffix(QStringLiteral(" 秒"));
    intervalForm->addRow(QStringLiteral("定时拍摄间隔"), m_interval);
    controls->addLayout(intervalForm);
    QHBoxLayout *burst = new QHBoxLayout;
    m_startBurst = new QPushButton(QStringLiteral("开始连拍"));
    m_startBurst->setObjectName("startBurstButton");
    m_stopBurst = new QPushButton(QStringLiteral("停止连拍"));
    m_stopBurst->setObjectName("stopBurstButton");
    burst->addWidget(m_startBurst);
    burst->addWidget(m_stopBurst);
    controls->addLayout(burst);
    QLabel *burstHint = caption(m_options.pcOnly
        ? QStringLiteral("上一张传输完成后才能拍下一张；板端不积压照片。")
        : QStringLiteral("保存繁忙时跳过当前节拍；停止后完成正在保存的一张。"));
    burstHint->setWordWrap(true);
    controls->addWidget(burstHint);
    body->addWidget(right);
    outer->addLayout(body, 1);
    m_status = new QLabel;
    m_status->setObjectName("statusLabel");
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(22);
    outer->addWidget(m_status);
}

void MainWindow::openCamera()
{
    if (m_worker || m_closing || m_requiresBoardReset) return;
    m_captureErrorLatched = false;
    m_stopping = false;
    m_freshFrame = false;
    m_preview->clearFrame();
    m_worker = new CaptureWorker(m_options.simulation, this);
    if (m_stream) {
        m_stream->beginSession(m_options.simulation);
        connect(m_worker, &CaptureWorker::streamFrameReady, m_stream, &LatestFrameServer::offerFrame,
                Qt::DirectConnection);
    }
    if (!m_options.streamOnly)
        connect(m_worker, &CaptureWorker::frameReady, this, &MainWindow::receiveFrame,
                Qt::QueuedConnection);
    connect(m_worker, &CaptureWorker::photoSaved, this, &MainWindow::photoSaved,
            Qt::QueuedConnection);
    connect(m_worker, &CaptureWorker::statusChanged, this, [this](const QString &message) {
        if (!m_stopping && !m_captureErrorLatched) setStatus(message);
    }, Qt::QueuedConnection);
    connect(m_worker, &CaptureWorker::captureError, this, [this](const QString &message) {
        stopBurst();
        m_startupCapturePending = false;
        m_captureErrorLatched = true;
        if (m_stream) m_stream->stopSession(QStringLiteral("camera_error"));
        if (!m_options.simulation && m_worker && m_worker->requiresHardwareReset())
            m_requiresBoardReset = true;
        m_freshFrame = false;
        setStatus(message, true);
        updateControls();
    }, Qt::QueuedConnection);
    connect(m_worker, &QThread::finished, this, &MainWindow::workerFinished,
            Qt::QueuedConnection);
    m_cameraState->setText(QStringLiteral("正在连接…"));
    setStatus(QStringLiteral("正在打开相机，等待第一帧…"));
    updateControls();
    m_worker->start();
}

void MainWindow::closeCamera()
{
    m_startupCapturePending = false;
    stopBurst();
    if (!m_worker || m_stopping) return;
    if (!m_requiresBoardReset) m_captureErrorLatched = false;
    m_stopping = true;
    if (m_stream) m_stream->stopSession(QStringLiteral("camera_stopped"));
    m_freshFrame = false;
    m_worker->stop();
    m_cameraState->setText(QStringLiteral("正在关闭…"));
    if (!m_requiresBoardReset)
        setStatus(m_photoPending ? QStringLiteral("正在完成照片保存并关闭相机…")
                                 : QStringLiteral("正在关闭相机…"));
    updateControls();
}

void MainWindow::workerFinished()
{
    CaptureWorker *worker = qobject_cast<CaptureWorker *>(sender());
    if (worker != m_worker) return;
    if (m_stream) m_stream->stopSession(QStringLiteral("camera_stopped"));
    m_burstTimer->stop();
    m_worker = nullptr;
    worker->deleteLater();
    m_freshFrame = false;
    m_stopping = false;
    if (m_photoPending && m_pendingImage.isEmpty()) {
        m_photoPending = false;
        if (!m_requiresBoardReset)
            setStatus(QStringLiteral("相机已结束，未收到本次照片的保存结果，请检查目录。"), true);
    }
    m_preview->clearFrame();
    m_cameraState->setText(m_requiresBoardReset ? QStringLiteral("需重启开发板")
                                               : QStringLiteral("未连接"));
    m_frameInfo->setText(m_requiresBoardReset
                        ? QStringLiteral("相机 DMA 状态异常 · 请重启开发板后再采集")
                        : QStringLiteral("相机已关闭 · 点击“打开相机”重新连接"));
    updateControls();
    if (m_closing) QTimer::singleShot(0, this, &QWidget::close);
}

void MainWindow::receiveFrame(const QImage &frame, const QDateTime &utc, quint64 sequence)
{
    CaptureWorker *worker = qobject_cast<CaptureWorker *>(sender());
    if (worker) worker->acknowledgePreview();
    if (!worker || worker != m_worker || m_stopping || m_closing || m_captureErrorLatched || frame.isNull()) return;
    m_freshFrame = true;
    m_preview->setFrame(frame);
    m_cameraState->setText(m_options.simulation ? QStringLiteral("模拟预览") : QStringLiteral("● 已连接"));
    m_frameInfo->setText(QStringLiteral("%1 × %2  ·  帧 %3  ·  %4  ·  原图保存，无辅助线")
                         .arg(frame.width()).arg(frame.height()).arg(sequence)
                         .arg(utc.toLocalTime().toString("HH:mm:ss")));
    updateControls();
    // Request a new frame after startup has produced several complete previews.
    if (m_startupCapturePending && sequence >= 4 && m_capture->isEnabled()) {
        m_startupCapturePending = false;
        capturePhoto();
    }
}

bool MainWindow::validateSettings()
{
    if (m_options.pcOnly) return true;
    if (!QDir::isAbsolutePath(m_root->text().trimmed())) {
        setStatus(QStringLiteral("请为保存位置填写绝对路径，例如 /home/uisrc/datasets。"), true);
        m_root->setFocus();
        return false;
    }
    if (!DatasetStore::validComponent(m_dataset->text())
            || !DatasetStore::validComponent(m_scene->text())) {
        setStatus(QStringLiteral("数据集名称和场景名称必须是有效文件夹名，不能含路径分隔符、. 或 ..。"), true);
        return false;
    }
    return true;
}

void MainWindow::capturePhoto()
{
    if (m_guidanceSeconds > 0) return;
    if (!m_worker || !m_freshFrame || m_photoPending || m_stopping || m_closing) return;
    if (!validateSettings()) { stopBurst(); return; }
    SaveOptions options;
    options.memoryOnly = m_options.pcOnly;
    options.root = m_root->text().trimmed();
    options.dataset = m_dataset->text();
    options.scene = m_scene->text();
    options.format = m_format->currentData().toString();
    options.quality = m_quality->value();
    options.requestedUtc = QDateTime::currentDateTimeUtc();
    if (!m_worker->requestPhoto(options)) {
        setStatus(QStringLiteral("当前没有可保存的新画面，等待下一帧后重试。"), true);
        return;
    }
    m_photoPending = true;
    setStatus(m_options.pcOnly ? QStringLiteral("正在拍照并编码，照片将直接传到电脑…")
                              : QStringLiteral("正在保存原始分辨率照片…"));
    updateControls();
}

void MainWindow::startBurst()
{
    if (!m_worker || !m_freshFrame || m_photoPending || m_stopping || !validateSettings()) return;
    m_skipped = 0;
    m_burstTimer->start(qRound(m_interval->value() * 1000.0));
    capturePhoto();
    updateControls();
}

void MainWindow::stopBurst()
{
    if (!m_burstTimer || !m_burstTimer->isActive()) return;
    m_burstTimer->stop();
    setStatus(m_photoPending ? QStringLiteral("连拍已停止，正在完成最后一张保存…")
                             : QStringLiteral("连拍已停止。"));
    updateControls();
}

void MainWindow::photoSaved(const SaveResult &result)
{
    if (sender() != m_worker) return;
    if (m_options.pcOnly && result.ok) {
        m_pendingImage = result.encodedImage;
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_pendingMetadata = {{"photo_id", id}, {"size", m_pendingImage.size()},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(m_pendingImage, QCryptographicHash::Sha256).toHex())},
            {"width", result.width}, {"height", result.height}, {"format", "jpg"},
            {"source", m_options.simulation ? "simulation" : "imx415"},
            {"frame_sequence", QString::number(result.frameSequence)},
            {"captured_utc", result.capturedUtc.toUTC().toString(Qt::ISODateWithMs)}};
        const QImage image = QImage::fromData(m_pendingImage, "JPG");
        m_lastImage->setPixmap(QPixmap::fromImage(image.scaled(m_lastImage->size(),
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        m_lastPath->setText(QStringLiteral("待传至电脑 · %1 KB · 板端仅占用内存")
                           .arg(m_pendingImage.size() / 1024));
        setStatus(QStringLiteral("照片已拍好，等待电脑接收并确认保存…"));
        updateStorage(); updateControls();
        return;
    }
    m_photoPending = false;
    if (result.ok) {
        qInfo().noquote() << "PHOTO_SAVED=" + result.imagePath;
        ++m_saved;
        m_count->setText(QString::number(m_saved));
        m_lastPath->setText(result.imagePath);
        m_lastPath->setToolTip(result.imagePath);
        QImage image(result.imagePath);
        m_lastImage->setPixmap(QPixmap::fromImage(image.scaled(m_lastImage->size(),
                                  Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        QString message = QStringLiteral("已保存第 %1 张 · %2 × %3").arg(m_saved)
                .arg(result.width).arg(result.height);
        if (m_burstTimer->isActive()) message += QStringLiteral(" · 定时连拍中");
        if (m_skipped) message += QStringLiteral(" · 已跳过 %1 个繁忙节拍").arg(m_skipped);
        setStatus(message);
    } else {
        stopBurst();
        setStatus(QStringLiteral("保存失败：%1").arg(result.error), true);
    }
    updateStorage();
    updateControls();
}

void MainWindow::updateControls()
{
    const bool burst = m_burstTimer && m_burstTimer->isActive();
    const bool ready = m_worker && m_freshFrame && !m_stopping && !m_closing;
    const bool editable = !burst && !m_photoPending && !m_closing;
    m_open->setEnabled(!m_worker && !m_closing && !m_requiresBoardReset);
    m_close->setEnabled(m_worker && !m_stopping && !m_closing);
    m_capture->setEnabled(ready && !m_photoPending && !burst);
    m_startBurst->setEnabled(ready && !m_photoPending && !burst);
    m_stopBurst->setEnabled(burst && !m_closing);
    m_root->setEnabled(editable);
    m_browse->setEnabled(editable);
    m_dataset->setEnabled(editable);
    m_scene->setEnabled(editable);
    m_format->setEnabled(editable && !m_options.pcOnly);
    m_quality->setEnabled(editable && m_format->currentData().toString() == "jpg");
    m_interval->setEnabled(editable);
}

void MainWindow::updateStorage()
{
    if (m_options.pcOnly) {
        m_disk->setText(m_pendingImage.isEmpty() ? QStringLiteral("照片保存到电脑")
                                               : QStringLiteral("等待传输\n%1 KB").arg(m_pendingImage.size()/1024));
        return;
    }
    QString path = m_root->text().trimmed();
    if (!QDir::isAbsolutePath(path)) {
        m_disk->setText(QStringLiteral("可用空间 —"));
        return;
    }
    QFileInfo candidate(path);
    while (!candidate.exists()) {
        const QString parent = candidate.absolutePath();
        if (parent == candidate.absoluteFilePath()) break;
        candidate.setFile(parent);
    }
    QStorageInfo info(candidate.absoluteFilePath());
    info.refresh();
    m_disk->setText(info.isValid() && info.isReady()
                   ? QStringLiteral("可用空间\n%1 GB").arg(info.bytesAvailable() / 1073741824.0, 0, 'f', 1)
                   : QStringLiteral("可用空间\n无法读取"));
}

void MainWindow::setStatus(const QString &message, bool error)
{
    m_status->setText(message);
    m_status->setStyleSheet(error ? "color:#ffb3a7;background:#382331;"
                                 : "color:#b5cce0;background:#18283b;");
}

void MainWindow::saveSettings()
{
    if (m_options.simulation || m_options.pcOnly) return;
    QSettings settings;
    settings.setValue("root", m_root->text());
    settings.setValue("dataset", m_dataset->text());
    settings.setValue("scene", m_scene->text());
    settings.setValue("format", m_format->currentIndex());
    settings.setValue("quality", m_quality->value());
    settings.setValue("interval", m_interval->value());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    if (m_worker) {
        m_closing = true;
        closeCamera();
        setStatus(QStringLiteral("正在安全退出，等待相机释放和当前照片保存完成…"));
        updateControls();
        event->ignore();
        return;
    }
    event->accept();
    if (m_options.streamOnly) QCoreApplication::quit();
}

void MainWindow::updateGuidance()
{
    if (m_guidancePhase.isEmpty()) { m_guidance->hide(); return; }
    const bool hold = m_guidancePhase == "hold" || m_guidancePhase == "capture";
    m_guidance->setStyleSheet(hold
        ? "font-size:30px;font-weight:600;color:#fff2cb;background:#674417;border:2px solid #f2b557;border-radius:10px;padding:12px;"
        : "font-size:30px;font-weight:600;color:#d7fff2;background:#123d38;border:2px solid #4de0bd;border-radius:10px;padding:12px;");
    QString title = m_guidanceTotal > 0
        ? QStringLiteral("第 %1 / %2 张   ").arg(m_guidanceStep).arg(m_guidanceTotal) : QString();
    if (m_guidanceSeconds > 0)
        title += QStringLiteral("倒计时 %1 秒").arg(m_guidanceSeconds);
    m_guidance->setText(title.isEmpty() ? m_guidanceText : title + '\n' + m_guidanceText);
    m_guidance->show();
}

QJsonObject MainWindow::cameraCommand(const QJsonObject &command)
{
    auto fail = [](const char *error) { return QJsonObject{{"ok", false}, {"error", error}}; };
    if (!m_options.pcOnly) return fail("PC transfer mode is not enabled");
    const QString action = command.value("action").toString();
    if (action == "status") {
        QJsonObject result{{"ok", true}, {"protocol", 1}, {"pending", !m_pendingImage.isEmpty()},
            {"busy", m_photoPending}, {"camera_ready", m_freshFrame && !m_stopping},
            {"delivered", QString::number(m_saved)}, {"guidance_protocol", 1},
            {"guidance_phase", m_guidancePhase}, {"guidance_seconds", m_guidanceSeconds}};
        if (!m_pendingImage.isEmpty()) result["photo"] = m_pendingMetadata;
        return result;
    }
    if (action == "guide") {
        const QString phase = command.value("phase").toString();
        const QString message = command.value("text").toString();
        if (!QStringList({"prepare", "hold", "capture", "transfer", "done", "idle"}).contains(phase)
                || !command.value("text").isString() || message.isEmpty() || message.size() > 180)
            return fail("Invalid guidance phase or text");
        auto validInteger = [&command](const char *key, int maximum) {
            const QJsonValue value = command.value(key);
            const double number = value.toDouble(-1);
            return value.isDouble() && std::isfinite(number) && number >= 0
                    && number <= maximum && number == std::floor(number);
        };
        if (!validInteger("seconds", 60) || !validInteger("step", 100) || !validInteger("total", 100)
                || command.value("step").toInt() > command.value("total").toInt())
            return fail("Invalid guidance countdown or progress");
        stopBurst();
        m_guidanceTimer->stop();
        m_guidancePhase = phase;
        m_guidanceText = message;
        m_guidanceSeconds = command.value("seconds").toInt();
        m_guidanceStep = command.value("step").toInt();
        m_guidanceTotal = command.value("total").toInt();
        updateGuidance();
        if (m_guidanceSeconds > 0) m_guidanceTimer->start();
        return QJsonObject{{"ok", true}, {"guidance_protocol", 1}};
    }
    if (action == "capture") {
        if (m_guidanceSeconds > 0) return fail("Guidance countdown has not finished");
        if (!m_worker || !m_freshFrame || m_photoPending || m_stopping || m_closing)
            return fail("Camera is unavailable or a photo is awaiting delivery");
        capturePhoto();
        return QJsonObject{{"ok", m_photoPending}};
    }
    if (action == "close") {
        if (m_photoPending) return fail("Deliver the pending photo before closing");
        QTimer::singleShot(0, this, &QWidget::close);
        return QJsonObject{{"ok", true}};
    }
    const QString id = command.value("photo_id").toString();
    const QString sha = command.value("sha256").toString();
    if (action == "ack" && !id.isEmpty() && id == m_lastAckId && sha == m_lastAckSha)
        return QJsonObject{{"ok", true}, {"already_confirmed", true}};
    if (m_pendingImage.isEmpty() || id != m_pendingMetadata.value("photo_id").toString())
        return fail("Unknown photo ID");
    if (action == "image_chunk") {
        const QJsonValue offsetValue = command.value("offset");
        const double offsetNumber = offsetValue.toDouble(-1);
        if (!offsetValue.isDouble() || !std::isfinite(offsetNumber) || offsetNumber < 0
            || offsetNumber != std::floor(offsetNumber) || offsetNumber >= m_pendingImage.size())
            return fail("Invalid byte offset");
        const int offset = int(offsetNumber);
        return QJsonObject{{"ok", true}, {"offset", offset}, {"size", m_pendingImage.size()},
            {"sha256", m_pendingMetadata.value("sha256")},
            {"data", QString::fromLatin1(m_pendingImage.mid(offset, 1536).toBase64())}};
    }
    if (action == "ack") {
        if (sha != m_pendingMetadata.value("sha256").toString()) return fail("SHA256 mismatch");
        m_lastAckId = id; m_lastAckSha = sha;
        m_pendingImage.clear(); m_pendingMetadata = QJsonObject(); m_photoPending = false;
        ++m_saved; m_count->setText(QString::number(m_saved));
        m_lastPath->setText(QStringLiteral("电脑已确认保存 · %1.jpg").arg(id));
        setStatus(QStringLiteral("第 %1 张照片已传到电脑并通过校验；板端未保存照片。 ").arg(m_saved));
        updateStorage(); updateControls();
        return QJsonObject{{"ok", true}};
    }
    return fail("Unsupported action");
}
