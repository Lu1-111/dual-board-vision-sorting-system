#include "dataset_store.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUuid>

namespace {

QString utcText(const QDateTime &value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

// Check each child separately: mkpath alone would follow an existing symlink.
bool ensureChild(const QString &parent, const QString &name, QString *path,
                 QString *error)
{
    const QString candidate = QDir(parent).filePath(name);
    QFileInfo info(candidate);
    if (info.isSymLink()) {
        *error = QStringLiteral("Refusing symlink directory: %1").arg(candidate);
        return false;
    }
    if (!info.exists()) {
        if (!QDir(parent).mkdir(name)) {
            *error = QStringLiteral("Cannot create directory: %1").arg(candidate);
            return false;
        }
        info.refresh();
    }
    if (info.isSymLink() || !info.isDir()) {
        *error = QStringLiteral("Not a regular directory: %1").arg(candidate);
        return false;
    }
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()
        || QDir::cleanPath(QFileInfo(canonical).absolutePath())
            != QDir::cleanPath(parent)) {
        *error = QStringLiteral("Directory escaped its parent: %1").arg(candidate);
        return false;
    }
    *path = canonical;
    return true;
}

bool atomicWrite(const QString &path, const QByteArray &contents, QString *error)
{
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
        *error = QStringLiteral("Refusing to overwrite existing file: %1").arg(path);
        return false;
    }
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Cannot open %1: %2").arg(path, output.errorString());
        return false;
    }
    if (output.write(contents) != contents.size()) {
        *error = QStringLiteral("Cannot write %1: %2").arg(path, output.errorString());
        output.cancelWriting();
        return false;
    }
    // UUID naming prevents ordinary collisions; recheck immediately before commit.
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
        output.cancelWriting();
        *error = QStringLiteral("Destination appeared during save: %1").arg(path);
        return false;
    }
    if (!output.commit()) {
        *error = QStringLiteral("Cannot commit %1: %2").arg(path, output.errorString());
        return false;
    }
    return true;
}

} // namespace

bool DatasetStore::validComponent(const QString &value)
{
    if (value.isEmpty() || value.size() > 128 || value != value.trimmed()
        || value == QStringLiteral(".") || value == QStringLiteral("..")
        || value.endsWith(QLatin1Char('.'))) {
        return false;
    }
    for (const QChar character : value) {
        if (character.unicode() < 32 || character.unicode() == 127
            || QStringLiteral("/\\<>:\"|?*").contains(character)) {
            return false;
        }
    }
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    return !reserved.match(value).hasMatch();
}

SaveResult DatasetStore::save(const QImage &image, const SaveOptions &options,
                              const QDateTime &capturedUtc, quint64 frameSequence,
                              const QString &source)
{
    SaveResult result;
    if (image.isNull()) {
        result.error = QStringLiteral("Cannot save an empty frame.");
        return result;
    }
    if (!options.memoryOnly && (!validComponent(options.dataset) || !validComponent(options.scene))) {
        result.error = QStringLiteral("Dataset and scene must be simple directory names.");
        return result;
    }
    if (source != QStringLiteral("imx415") && source != QStringLiteral("simulation")) {
        result.error = QStringLiteral("Unknown image source: %1").arg(source);
        return result;
    }
    if (!capturedUtc.isValid()) {
        result.error = QStringLiteral("Capture timestamp is invalid.");
        return result;
    }
    QString format = options.format.trimmed().toLower();
    if (format == QStringLiteral("jpeg"))
        format = QStringLiteral("jpg");
    if (format != QStringLiteral("jpg") && format != QStringLiteral("png")) {
        result.error = QStringLiteral("Unsupported format; choose jpg or png.");
        return result;
    }
    if (options.quality < 0 || options.quality > 100) {
        result.error = QStringLiteral("JPEG quality must be between 0 and 100.");
        return result;
    }
    if (options.memoryOnly) {
        QBuffer buffer(&result.encodedImage);
        if (!buffer.open(QIODevice::WriteOnly)) {
            result.error = QStringLiteral("Cannot allocate photo buffer.");
            return result;
        }
        QImageWriter writer(&buffer, "jpg");
        writer.setQuality(options.quality);
        if (!writer.write(image)) {
            result.error = QStringLiteral("Cannot encode photo: %1").arg(writer.errorString());
            return result;
        }
        if (result.encodedImage.size() > 8 * 1024 * 1024) {
            result.encodedImage.clear();
            result.error = QStringLiteral("Photo exceeds the 8 MiB transfer limit.");
            return result;
        }
        result.width = image.width();
        result.height = image.height();
        result.capturedUtc = capturedUtc;
        result.frameSequence = frameSequence;
        result.ok = true;
        return result;
    }
    if (options.root.trimmed().isEmpty()) {
        result.error = QStringLiteral("Choose a dataset output directory.");
        return result;
    }
    if (!QDir().mkpath(options.root)) {
        result.error = QStringLiteral("Cannot create output root: %1").arg(options.root);
        return result;
    }
    const QString root = QFileInfo(options.root).canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        result.error = QStringLiteral("Output root is not a directory: %1").arg(options.root);
        return result;
    }
    QString datasetPath, imagesPath, scenePath;
    if (!ensureChild(root, options.dataset, &datasetPath, &result.error)
        || !ensureChild(datasetPath, QStringLiteral("images"), &imagesPath, &result.error)
        || !ensureChild(imagesPath, options.scene, &scenePath, &result.error)) {
        return result;
    }

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("Cannot allocate the image encoding buffer.");
        return result;
    }
    QImageWriter writer(&buffer, format.toLatin1());
    if (format == QStringLiteral("jpg"))
        writer.setQuality(options.quality);
    if (!writer.write(image)) {
        result.error = QStringLiteral("Cannot encode image: %1").arg(writer.errorString());
        return result;
    }
    buffer.close();

    QStorageInfo storage(scenePath);
    storage.refresh();
    const qint64 reserve = 1024 * 1024;
    if (!storage.isValid() || !storage.isReady() || storage.bytesAvailable() < 0) {
        result.error = QStringLiteral("Cannot determine free space for: %1").arg(scenePath);
        return result;
    }
    if (storage.isReadOnly()) {
        result.error = QStringLiteral("Output filesystem is read-only: %1").arg(scenePath);
        return result;
    }
    if (storage.bytesAvailable() < static_cast<qint64>(encoded.size()) + reserve) {
        result.error = QStringLiteral("Insufficient free space for the image and metadata.");
        return result;
    }

    const QString prefix = source == QStringLiteral("simulation")
        ? QStringLiteral("SIM_") : QString();
    const QString stem = prefix + capturedUtc.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'_"))
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    result.imagePath = QDir(scenePath).filePath(stem + QLatin1Char('.') + format);
    result.metadataPath = QDir(scenePath).filePath(stem + QStringLiteral(".json"));
    // Check both names before creating either half of the pair.
    if (QFileInfo::exists(result.imagePath) || QFileInfo(result.imagePath).isSymLink()
        || QFileInfo::exists(result.metadataPath) || QFileInfo(result.metadataPath).isSymLink()) {
        result.error = QStringLiteral("Destination already exists; retry the capture.");
        return result;
    }

    QJsonObject metadata;
    metadata.insert(QStringLiteral("schema_version"), 1);
    metadata.insert(QStringLiteral("image"), QDir::fromNativeSeparators(QDir(root).relativeFilePath(result.imagePath)));
    metadata.insert(QStringLiteral("width"), image.width());
    metadata.insert(QStringLiteral("height"), image.height());
    metadata.insert(QStringLiteral("requested_utc"), utcText(options.requestedUtc.isValid() ? options.requestedUtc : capturedUtc));
    metadata.insert(QStringLiteral("captured_utc"), utcText(capturedUtc));
    metadata.insert(QStringLiteral("scene"), options.scene);
    metadata.insert(QStringLiteral("dataset"), options.dataset);
    metadata.insert(QStringLiteral("source"), source);
    // A decimal string retains all 64 bits in readers that use IEEE-754 JSON numbers.
    metadata.insert(QStringLiteral("frame_sequence"), QString::number(frameSequence));
    metadata.insert(QStringLiteral("annotation_status"), QStringLiteral("unannotated"));
    metadata.insert(QStringLiteral("format"), format);
    const QByteArray json = QJsonDocument(metadata).toJson(QJsonDocument::Indented);

    if (!atomicWrite(result.imagePath, encoded, &result.error))
        return result;
    if (!atomicWrite(result.metadataPath, json, &result.error)) {
        QFile newlyCreatedImage(result.imagePath);
        if (!newlyCreatedImage.remove()) {
            result.error += QStringLiteral("; could not remove orphan image %1: %2")
                .arg(result.imagePath, newlyCreatedImage.errorString());
        }
        return result;
    }
    result.width = image.width();
    result.height = image.height();
    result.ok = true;
    return result;
}
