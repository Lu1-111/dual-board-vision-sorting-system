#pragma once

#include <QObject>
#include <QDateTime>
#include <QImage>
#include <QString>
#include <memory>

// start/session/lifetime methods run on the GUI thread. offerFrame is thread-safe
// for a direct capture-thread connection. Both independent encoders have one
// replaceable pending frame; offered QImages must already own their pixels.
class LatestFrameServer : public QObject {
public:
    explicit LatestFrameServer(QObject *parent = nullptr);
    ~LatestFrameServer() override;
    bool start(const QString &address, quint16 port, int fps, int quality, QString &error,
               int previewFps = 15, int previewWidth = 640, int previewQuality = 75,
               bool trackCenters = false, const QString& trackingOutputDir = QStringLiteral("/tmp/triangle-track"));
    void beginSession(bool simulation);
    void stopSession(const QString &reason);
    void offerFrame(const QImage &frame, const QDateTime &utc, quint64 sequence);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};
