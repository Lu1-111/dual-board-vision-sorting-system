#include "mainwindow.hpp"
#include "capture_worker.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextCodec>
#include <cstdio>

static QStringList photos(const QString &root)
{
    QStringList result;
    QDirIterator it(root, {"*.png", "*.jpg"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) result << it.next();
    return result;
}

int main(int argc, char **argv)
{
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("CameraDatasetTests");
    QCoreApplication::setApplicationName("SmokeSimulation");
    const QString fonts = qEnvironmentVariable("CAMERA_TEST_FONT_DIR");
    QFontDatabase::addApplicationFont(fonts + "/DejaVuSans.ttf");
    const int fontId = QFontDatabase::addApplicationFont(fonts + "/DroidSansFallbackFull.ttf");
    if (fontId >= 0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 11));
    qRegisterMetaType<SaveResult>("SaveResult");
    qRegisterMetaType<quint64>("quint64");
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    AppOptions options;
    options.simulation = true;
    options.output = temporary.path();
    options.dataset = "smoke";
    options.scene = QString::fromUtf8("桌面场景");
    MainWindow window(options);
    const bool compact = qEnvironmentVariableIntValue("CAMERA_SMOKE_COMPACT") != 0;
    if (compact) window.resize(1024, 600);
    window.show();
    auto button = [&window](const char *name) { return window.findChild<QPushButton *>(name); };
    auto format = window.findChild<QComboBox *>("formatCombo");
    auto interval = window.findChild<QDoubleSpinBox *>("intervalSpin");
    auto root = window.findChild<QLineEdit *>("rootEdit");
    auto count = window.findChild<QLabel *>("count");
    if (!format || !interval || !root || !count || !button("captureButton")) return 3;
    int phase = 0, priorCount = 0;
    const int configuredTimeout = qEnvironmentVariableIntValue("CAMERA_SMOKE_TIMEOUT_MS");
    const int timeoutMs = configuredTimeout > 0 ? configuredTimeout : 20000;
    bool failed = false;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    auto fail = [&](const QString &message) {
        std::fprintf(stderr, "SMOKE FAIL: %s\n", qPrintable(message));
        failed = true;
        poll.stop();
        window.close();
        app.exit(1);
    };
    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (elapsed.elapsed() > timeoutMs) { fail("timeout in phase " + QString::number(phase)); return; }
        const QStringList images = photos(temporary.path());
        if (phase == 0 && button("captureButton")->isEnabled()) {
            if (compact && window.size() != QSize(1024, 600)) {
                fail("minimum layout exceeds 1024x600"); return;
            }
            for (const char *name : {"captureButton", "startBurstButton", "stopBurstButton"}) {
                auto action = button(name);
                const QRect area(action->mapTo(&window, QPoint(0, 0)), action->size());
                if (!action->isVisible() || !window.rect().contains(area)) {
                    fail(QString("primary action clipped: ") + name); return;
                }
            }
            format->setCurrentIndex(1);
            button("captureButton")->click();
            if (root->isEnabled()) { fail("settings not frozen during capture"); return; }
            phase = 1;
        } else if (phase == 1 && images.size() == 1 && button("captureButton")->isEnabled()) {
            if (!images.first().endsWith(".png")) { fail("PNG choice ignored"); return; }
            format->setCurrentIndex(0);
            interval->setValue(0.2);
            button("startBurstButton")->click();
            if (!button("stopBurstButton")->isEnabled() || root->isEnabled()) {
                fail("burst controls or settings state incorrect"); return;
            }
            phase = 2;
        } else if (phase == 2 && images.size() >= 4) {
            button("stopBurstButton")->click();
            if (argc > 1 && !window.grab().save(QString::fromLocal8Bit(argv[1]))) {
                fail("cannot save UI screenshot"); return;
            }
            button("closeCameraButton")->click();
            phase = 3;
        } else if (phase == 3 && button("openCameraButton")->isEnabled()) {
            priorCount = images.size();
            if (count->text().toInt() != priorCount) { fail("saved count differs from disk"); return; }
            button("openCameraButton")->click();
            phase = 4;
        } else if (phase == 4 && button("captureButton")->isEnabled()) {
            button("captureButton")->click();
            phase = 5;
        } else if (phase == 5 && images.size() > priorCount && button("captureButton")->isEnabled()) {
            // Close with a fresh photo request pending: shutdown must settle it,
            // never destroy a running thread or leave a half-written image pair.
            button("captureButton")->click();
            window.close();
            phase = 6;
        } else if (phase == 6 && !window.isVisible()) {
            const auto workers = window.findChildren<CaptureWorker *>();
            for (auto worker : workers) if (worker->isRunning()) { fail("thread alive after close"); return; }
            for (const auto &imagePath : images) {
                QImage image(imagePath);
                QFile metadata(imagePath.left(imagePath.lastIndexOf('.')) + ".json");
                if (image.size() != QSize(1920, 1080) || !metadata.open(QIODevice::ReadOnly)) {
                    fail("missing original-resolution image/metadata pair"); return;
                }
                const auto data = QJsonDocument::fromJson(metadata.readAll()).object();
                if (data.value("source").toString() != "simulation"
                    || data.value("annotation_status").toString() != "unannotated"
                    || !QFileInfo(imagePath).fileName().startsWith("SIM_")
                    || data.value("width").toInt() != 1920 || data.value("height").toInt() != 1080
                    || QDateTime::fromString(data.value("captured_utc").toString(), Qt::ISODateWithMs)
                       < QDateTime::fromString(data.value("requested_utc").toString(), Qt::ISODateWithMs)) {
                    fail("incorrect image provenance, annotation status or fresh-frame timestamp"); return;
                }
            }
            std::printf("APP_SMOKE_PASS photos=%d phases=7 PNG/JPEG/burst/reopen/close-pending verified\n", images.size());
            poll.stop();
            app.exit(0);
        }
    });
    poll.start(40);
    const int result = app.exec();
    return failed ? 1 : result;
}
