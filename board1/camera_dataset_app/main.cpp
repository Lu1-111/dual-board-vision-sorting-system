#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLockFile>
#include <QMessageBox>
#include <QTimer>
#include <QTextCodec>
#include <QDebug>
#include <csignal>
#include <cstring>
#include "mainwindow.hpp"
#include "camera_console.hpp"

namespace {
volatile std::sig_atomic_t closeRequested = 0;
void requestClose(int) { closeRequested = 1; }
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--serial-bridge") == 0) {
        QCoreApplication app(argc, argv);
        return CameraConsole::stdioBridge();
    }
    if (argc == 3 && std::strcmp(argv[1], "--camera-command") == 0) {
        QCoreApplication app(argc, argv);
        return CameraConsole::command(QString::fromUtf8(argv[2]));
    }
    // The board rootfs may omit glibc locale/iconv modules. Dataset paths and
    // command-line scene names still have to round-trip as UTF-8.
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    // A shared-camera service must never instantiate a framebuffer platform.
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--stream-only") == 0)
            qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("UISRC");
    QCoreApplication::setApplicationName("CameraDatasetCollector");
    QCoreApplication::setApplicationVersion("1.0");
    qRegisterMetaType<SaveResult>("SaveResult");
    qRegisterMetaType<SaveOptions>("SaveOptions");
    qRegisterMetaType<quint64>("quint64");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DR1M90 相机数据集采集工具"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"simulate", "Explicit simulated camera mode (no hardware)."});
    parser.addOption({"fullscreen", "Show the application in fullscreen mode."});
    parser.addOption({"open-camera", "Open the real camera on startup (no simulation fallback)."});
    parser.addOption({"capture-first", "Open the camera and save one fresh photo after startup."});
    parser.addOption({"pc-only", "Keep each JPEG only in RAM until the computer confirms receipt; never save photos on the board."});
    parser.addOption({"stream", "Enable the read-only latest-frame HTTP service (disabled by default)."});
    parser.addOption({"stream-only", "Serve camera frames without a visible window; requires --stream and --open-camera."});
    parser.addOption({"track-centers", "Overlay blue-object IDs/centers on preview and publish localization-only JSON; requires --stream."});
    parser.addOption({"tracking-output-dir", "Dedicated localization JSON directory.", "directory", "/tmp/triangle-track"});
    parser.addOption({"stream-address", "IPv4 address to bind for the live stream.", "address", "172.22.203.1"});
    parser.addOption({"stream-port", "Live HTTP TCP port.", "port", "47003"});
    parser.addOption({"stream-fps", "Maximum live JPEG encoding rate (1 to 5 FPS).", "fps", "3"});
    parser.addOption({"stream-quality", "Live JPEG quality (1 to 100); photo quality is unchanged.", "quality", "95"});
    parser.addOption({"preview-fps", "Independent preview maximum encoding rate (1 to 30 FPS), only with --stream.", "fps", "15"});
    parser.addOption({"preview-width", "Preview maximum edge (160 to 1280 pixels), preserving aspect ratio.", "pixels", "640"});
    parser.addOption({"preview-quality", "Independent preview JPEG quality (1 to 100); full-resolution stream unchanged.", "quality", "75"});
    parser.addOption({"output", "Dataset root directory.", "path"});
    parser.addOption({"dataset", "Dataset directory name.", "name"});
    parser.addOption({"scene", "Organizational scene/batch directory name.", "name"});
    parser.addOption({"screenshot", "Save a window screenshot for UI validation.", "path"});
    parser.addOption({"quit-after", "Close safely after this many milliseconds.", "milliseconds"});
    parser.process(app);
    AppOptions options;
    options.simulation = parser.isSet("simulate");
    options.openOnStart = parser.isSet("open-camera");
    options.captureFirst = parser.isSet("capture-first");
    options.pcOnly = parser.isSet("pc-only");
    options.stream = parser.isSet("stream");
    options.streamOnly = parser.isSet("stream-only");
    options.trackCenters = parser.isSet("track-centers");
    options.trackingOutputDir = parser.value("tracking-output-dir");
    if(options.trackCenters&&!options.stream){qCritical()<<"--track-centers requires --stream";return 2;}
    if (options.streamOnly && (!options.stream || !options.openOnStart || options.captureFirst || parser.isSet("screenshot"))) {
        qCritical() << "--stream-only requires --stream --open-camera and cannot capture-first/screenshot";
        return 2;
    }
    options.streamAddress = parser.value("stream-address");
    bool portOk = false, fpsOk = false, qualityOk = false;
    const int streamPort = parser.value("stream-port").toInt(&portOk);
    options.streamFps = parser.value("stream-fps").toInt(&fpsOk);
    options.streamQuality = parser.value("stream-quality").toInt(&qualityOk);
    if (!portOk || streamPort < 1 || streamPort > 65535 || !fpsOk || options.streamFps < 1
            || options.streamFps > 5 || !qualityOk || options.streamQuality < 1 || options.streamQuality > 100) {
        qCritical() << "Invalid --stream-port, --stream-fps or --stream-quality";
        return 2;
    }
    bool previewFpsOk = false, previewWidthOk = false, previewQualityOk = false;
    options.previewFps = parser.value("preview-fps").toInt(&previewFpsOk);
    options.previewWidth = parser.value("preview-width").toInt(&previewWidthOk);
    options.previewQuality = parser.value("preview-quality").toInt(&previewQualityOk);
    if (!previewFpsOk || options.previewFps < 1 || options.previewFps > 30
            || !previewWidthOk || options.previewWidth < 160 || options.previewWidth > 1280
            || !previewQualityOk || options.previewQuality < 1 || options.previewQuality > 100) {
        qCritical() << "Invalid --preview-fps, --preview-width or --preview-quality";
        return 2;
    }
    options.streamPort = static_cast<quint16>(streamPort);
    options.output = parser.value("output");
    options.dataset = parser.value("dataset");
    options.scene = parser.value("scene");
    if (options.simulation) {
        // Test mode must never replace the user's actual collection preferences.
        QCoreApplication::setApplicationName("CameraDatasetCollectorSimulation");
    }
    QLockFile guiLock(QDir::tempPath() + "/uisrc-camera-dataset-gui.lock");
    if (!options.simulation && !guiLock.tryLock(0)) {
        const QString message = QStringLiteral("相机采集程序已在运行。请使用已打开的窗口，或等待其退出后重试。");
        qCritical().noquote() << message;
        // Avoid a second framebuffer writer even for an error dialog on the board.
        if (QApplication::platformName() != "linuxfb")
            QMessageBox::warning(nullptr, QStringLiteral("程序已运行"), message);
        return 3;
    }
    const QString latinFontPath = QCoreApplication::applicationDirPath()
            + "/fonts/DejaVuSans.ttf";
    if (QFileInfo::exists(latinFontPath)) QFontDatabase::addApplicationFont(latinFontPath);
    const QString fontPath = QCoreApplication::applicationDirPath()
            + "/fonts/DroidSansFallbackFull.ttf";
    if (QFileInfo::exists(fontPath)) {
        const int id = QFontDatabase::addApplicationFont(fontPath);
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.isEmpty()) app.setFont(QFont(families.first(), 11));
    } else {
        app.setFont(QFont("Noto Sans CJK SC", 11));
    }
    MainWindow window(options);
    CameraConsole console([&window](const QJsonObject &command) {
        QJsonObject result = window.cameraCommand(command);
        result["rpc_id"] = command.value("rpc_id");
        return result;
    });
    if (options.pcOnly) {
        QString error;
        if (!console.start(error)) { qCritical().noquote() << error; return 3; }
    }
    if (options.streamOnly)
        app.setQuitOnLastWindowClosed(false);
    else if (parser.isSet("fullscreen") || QApplication::platformName() == "linuxfb")
        window.showFullScreen();
    else
        window.show();
    std::signal(SIGTERM, requestClose);
    std::signal(SIGINT, requestClose);
    QTimer signalPoll;
    QObject::connect(&signalPoll, &QTimer::timeout, &window, [&window, &app, &options]() {
        if (closeRequested) {
            closeRequested = 0;
            if (window.close() && options.streamOnly) app.quit();
        }
    });
    signalPoll.start(100);
    if (parser.isSet("screenshot")) {
        const QString path = parser.value("screenshot");
        QTimer::singleShot(1600, &window, [&window, path]() {
            QDir().mkpath(QFileInfo(path).absolutePath());
            if (!window.grab().save(path)) qWarning() << "Cannot save screenshot:" << path;
        });
    }
    if (parser.isSet("quit-after")) {
        bool ok = false;
        const int duration = parser.value("quit-after").toInt(&ok);
        if (!ok || duration < 1) parser.showHelp(2);
        QTimer::singleShot(duration, &window, [&window, &app, &options]() {
            if (window.close() && options.streamOnly) app.quit();
        });
    }
    return app.exec();
}
