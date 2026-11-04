#ifndef CAMERA_DATASET_STORE_HPP
#define CAMERA_DATASET_STORE_HPP

#include <QDateTime>
#include <QImage>
#include <QMetaType>
#include <QString>

struct SaveOptions {
    bool memoryOnly = false;
    QString root;
    QString dataset;
    QString scene;
    QString format;
    int quality = 95;
    QDateTime requestedUtc;
};

Q_DECLARE_METATYPE(SaveOptions)

struct SaveResult {
    bool ok = false;
    QString imagePath;
    QString metadataPath;
    QString error;
    int width = 0;
    int height = 0;
    QByteArray encodedImage;
    QDateTime capturedUtc;
    quint64 frameSequence = 0;
};

Q_DECLARE_METATYPE(SaveResult)

class DatasetStore {
public:
    static bool validComponent(const QString &value);
    static SaveResult save(const QImage &image, const SaveOptions &options,
                           const QDateTime &capturedUtc, quint64 frameSequence,
                           const QString &source);
};

#endif
