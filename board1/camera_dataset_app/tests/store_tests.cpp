#include "dataset_store.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTextCodec>
#include <limits>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
int failures = 0;
void check(bool condition, const QString &message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << '\n';
        ++failures;
    }
}
QJsonObject readMetadata(const QString &path)
{
    QFile input(path);
    check(input.open(QIODevice::ReadOnly), QStringLiteral("metadata opens"));
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(input.readAll(), &error);
    check(error.error == QJsonParseError::NoError && doc.isObject(), QStringLiteral("metadata is a JSON object"));
    return doc.object();
}
}

int main(int argc, char **argv)
{
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), QStringLiteral("temporary output exists"));
    if (!temporary.isValid())
        return 1;
    QImage image(37, 23, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(x * 5, y * 9, (x + y) * 3));
    SaveOptions options;
    options.root = temporary.path();
    options.dataset = QString::fromUtf8("视觉数据集");
    options.scene = QString::fromUtf8("场景 A");
    check(QFile::encodeName(options.dataset) == options.dataset.toUtf8(), QStringLiteral("filesystem names use UTF-8 even with a minimal locale"));
    options.format = QStringLiteral("png");
    options.requestedUtc = QDateTime::fromString(QStringLiteral("2026-10-18T10:11:12.123Z"), Qt::ISODateWithMs);
    const QDateTime captured = options.requestedUtc.addMSecs(42);
    const quint64 sequence = std::numeric_limits<quint64>::max();
    const SaveResult first = DatasetStore::save(image, options, captured, sequence, QStringLiteral("imx415"));
    check(first.ok, QStringLiteral("PNG save succeeds: ") + first.error);
    check(first.width == 37 && first.height == 23, QStringLiteral("result has original dimensions"));
    const QImage restored(first.imagePath);
    check(restored.size() == image.size(), QStringLiteral("saved dimensions match original"));
    check(restored.convertToFormat(QImage::Format_RGB32) == image, QStringLiteral("PNG pixels match exactly"));
    const QJsonObject metadata = readMetadata(first.metadataPath);
    check(metadata.value(QStringLiteral("schema_version")).toInt() == 1, QStringLiteral("schema version"));
    check(metadata.value(QStringLiteral("width")).toInt() == 37 && metadata.value(QStringLiteral("height")).toInt() == 23, QStringLiteral("metadata dimensions"));
    check(metadata.value(QStringLiteral("requested_utc")).toString() == options.requestedUtc.toString(Qt::ISODateWithMs), QStringLiteral("request timestamp preserved"));
    check(metadata.value(QStringLiteral("captured_utc")).toString() == captured.toString(Qt::ISODateWithMs), QStringLiteral("capture timestamp preserved"));
    check(metadata.value(QStringLiteral("frame_sequence")).toString() == QString::number(sequence), QStringLiteral("full quint64 sequence preserved"));
    check(metadata.value(QStringLiteral("annotation_status")).toString() == QStringLiteral("unannotated"), QStringLiteral("never claims bounding boxes exist"));
    check(metadata.value(QStringLiteral("source")).toString() == QStringLiteral("imx415"), QStringLiteral("source recorded"));
    check(metadata.value(QStringLiteral("dataset")).toString() == options.dataset && metadata.value(QStringLiteral("scene")).toString() == options.scene, QStringLiteral("Chinese dataset and scene preserved"));
    check(metadata.value(QStringLiteral("format")).toString() == QStringLiteral("png"), QStringLiteral("format recorded"));
    const QString relative = metadata.value(QStringLiteral("image")).toString();
    check(relative.contains(options.dataset) && relative.contains(options.scene), QStringLiteral("actual filesystem names preserve Chinese text"));
    check(!QDir::isAbsolutePath(relative) && !relative.contains(QStringLiteral("..")) && QFileInfo(QDir(options.root).filePath(relative)).canonicalFilePath() == QFileInfo(first.imagePath).canonicalFilePath(), QStringLiteral("relative image path resolves correctly"));
    const SaveResult second = DatasetStore::save(image, options, captured, sequence, QStringLiteral("imx415"));
    check(second.ok && second.imagePath != first.imagePath, QStringLiteral("same timestamp produces unique names"));
    check(QFileInfo::exists(first.imagePath), QStringLiteral("previous image remains"));
    const SaveResult simulated = DatasetStore::save(image, options, captured, 3, QStringLiteral("simulation"));
    check(simulated.ok && QFileInfo(simulated.imagePath).fileName().startsWith(QStringLiteral("SIM_")), QStringLiteral("simulation visibly marked in filename"));
    check(readMetadata(simulated.metadataPath).value(QStringLiteral("source")).toString() == QStringLiteral("simulation"), QStringLiteral("simulation source marked"));

    const QStringList invalidNames = {QString(), QStringLiteral("."), QStringLiteral(".."), QStringLiteral("../escape"), QStringLiteral("a/b"), QStringLiteral("a\\b"), QStringLiteral("C:escape"), QStringLiteral("bad?name"), QStringLiteral("trailing."), QStringLiteral(" leading"), QStringLiteral("CON"), QStringLiteral("nul.txt"), QString(QChar(1))};
    for (const QString &name : invalidNames) {
        check(!DatasetStore::validComponent(name), QStringLiteral("reject invalid component: ") + name);
        SaveOptions invalid = options;
        invalid.scene = name;
        check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject invalid save scene"));
        invalid = options;
        invalid.dataset = name;
        check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject invalid save dataset"));
    }
    SaveOptions invalid = options;
    invalid.format = QStringLiteral("gif");
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject unsupported encoding"));
    check(!DatasetStore::save(QImage(), options, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject null image"));
    check(!DatasetStore::save(image, options, QDateTime(), 0, QStringLiteral("imx415")).ok, QStringLiteral("reject invalid timestamp"));
    check(!DatasetStore::save(image, options, captured, 0, QStringLiteral("unknown")).ok, QStringLiteral("reject unknown source"));
    invalid = options;
    invalid.quality = 101;
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject invalid quality"));
    QFile blocker(QDir(temporary.path()).filePath(QStringLiteral("blocker")));
    check(blocker.open(QIODevice::WriteOnly), QStringLiteral("create file obstruction"));
    blocker.write("untouched");
    blocker.close();
    invalid = options;
    invalid.dataset = QStringLiteral("blocker");
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject file as dataset directory"));
    invalid = options;
    invalid.root = blocker.fileName();
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject file as root directory"));
    blocker.open(QIODevice::ReadOnly);
    check(blocker.readAll() == QByteArray("untouched"), QStringLiteral("obstruction file preserved"));
    blocker.close();
    const QString imagesBlockDirectory = QDir(temporary.path()).filePath(QStringLiteral("blocked_images"));
    check(QDir().mkpath(imagesBlockDirectory), QStringLiteral("create images obstruction parent"));
    QFile imagesBlock(QDir(imagesBlockDirectory).filePath(QStringLiteral("images")));
    check(imagesBlock.open(QIODevice::WriteOnly), QStringLiteral("create images obstruction"));
    imagesBlock.close();
    invalid = options;
    invalid.dataset = QStringLiteral("blocked_images");
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject file as images directory"));
    QFile sceneBlock(QDir(temporary.path()).filePath(options.dataset + QStringLiteral("/images/blocked_scene")));
    check(sceneBlock.open(QIODevice::WriteOnly), QStringLiteral("create scene obstruction"));
    sceneBlock.close();
    invalid = options;
    invalid.scene = QStringLiteral("blocked_scene");
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject file as scene directory"));
#ifdef Q_OS_UNIX
    // QFile::link creates shortcuts on Windows; exercise real symlinks on the target Linux OS.
    QTemporaryDir outside;
    check(outside.isValid(), QStringLiteral("outside directory exists"));
    const QString linkPath = QDir(temporary.path()).filePath(QStringLiteral("escape"));
    check(::symlink(QFile::encodeName(outside.path()).constData(), QFile::encodeName(linkPath).constData()) == 0, QStringLiteral("create dataset symlink"));
    invalid = options;
    invalid.dataset = QStringLiteral("escape");
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject dataset symlink escape"));
    const QString childLink = QDir(temporary.path()).filePath(options.dataset + QStringLiteral("/images/escape_scene"));
    check(::symlink(QFile::encodeName(outside.path()).constData(), QFile::encodeName(childLink).constData()) == 0, QStringLiteral("create scene symlink"));
    invalid = options;
    invalid.scene = QStringLiteral("escape_scene");
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject scene symlink escape"));
    const QString imagesLinkParent = QDir(temporary.path()).filePath(QStringLiteral("linked_images"));
    check(QDir().mkpath(imagesLinkParent), QStringLiteral("create images link parent"));
    const QString imagesLink = QDir(imagesLinkParent).filePath(QStringLiteral("images"));
    check(::symlink(QFile::encodeName(outside.path()).constData(), QFile::encodeName(imagesLink).constData()) == 0, QStringLiteral("create images symlink"));
    invalid = options;
    invalid.dataset = QStringLiteral("linked_images");
    check(!DatasetStore::save(image, invalid, captured, 0, QStringLiteral("imx415")).ok, QStringLiteral("reject images symlink escape"));
    check(QDir(outside.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(), QStringLiteral("symlink target untouched"));
#else
    QTextStream(stdout) << "SKIP: real symlink checks require Linux.\n";
#endif
    QTextStream(stdout) << (failures ? "FAILED: " : "PASSED: ") << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
