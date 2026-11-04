#include "capture_worker.hpp"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QTextCodec>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("capture_once");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"simulate", "Explicit artificial test image."});
    parser.addOption({"output", "Dataset root.", "path", "/home/uisrc/datasets"});
    parser.addOption({"dataset", "Dataset name.", "name", "vision_dataset"});
    parser.addOption({"scene", "Scene/batch.", "name", "unlabeled"});
    parser.process(app);
    SaveOptions options;
    options.root = parser.value("output");
    options.dataset = parser.value("dataset");
    options.scene = parser.value("scene");
    options.format = "jpg";
    options.quality = 95;
    CaptureWorker worker(parser.isSet("simulate"));
    int result = 1, previews = 0;
    bool requested = false;
    QObject::connect(&worker, &CaptureWorker::frameReady, &app,
        [&](const QImage &, const QDateTime &, quint64) {
            worker.acknowledgePreview();
            // Allow several complete fresh frames before the single shutter.
            if (++previews >= 4 && !requested) {
                options.requestedUtc = QDateTime::currentDateTimeUtc();
                requested = worker.requestPhoto(options);
            }
        }, Qt::QueuedConnection);
    QObject::connect(&worker, &CaptureWorker::photoSaved, &app,
        [&](const SaveResult &saved) {
            if (saved.ok) {
                QTextStream(stdout) << "CAPTURE_OK\nIMAGE=" << saved.imagePath
                    << "\nMETADATA=" << saved.metadataPath << "\nSIZE="
                    << saved.width << 'x' << saved.height << '\n';
                result = 0;
            } else QTextStream(stderr) << "CAPTURE_FAILED=" << saved.error << '\n';
            worker.stop();
        }, Qt::QueuedConnection);
    QObject::connect(&worker, &CaptureWorker::captureError, &app,
        [&](const QString &error) { QTextStream(stderr) << "CAMERA_ERROR=" << error << '\n'; },
        Qt::QueuedConnection);
    QObject::connect(&worker, &QThread::finished, &app, [&]() { app.exit(result); });
    QTimer::singleShot(15000, &app, [&]() {
        if (worker.isRunning()) {
            QTextStream(stderr) << "Capture deadline exceeded; stopping safely.\n";
            worker.stop();
        }
    });
    worker.start();
    const int exitCode = app.exec();
    worker.stop();
    worker.wait();
    return exitCode;
}
