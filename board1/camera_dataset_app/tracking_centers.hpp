#pragma once
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <memory>
#include "fast_tracker.hpp"

// Encoder-thread-only localization. It never mutates the full source image.
class TrackingCenters {
public:
    TrackingCenters();
    QJsonObject update(const QImage& full,const QString& session,quint64 sequence);
    static QJsonObject loadReference(const QString& path);
    static QImage overlay(const QImage& preview,const QJsonObject& tracking,const QJsonObject& reference=QJsonObject());
private:
    triangle_tracking::FastTracker tracker_;
    QString session_;
    quint64 sequence_=0;
    qint64 windowMs_=0;
    int windowCount_=0;
    double fps_=0;
};

// Caller serializes publish/invalidate with the preview session mutex.
class TrackingPublisher {
public:
    explicit TrackingPublisher(const QString& directory);
    ~TrackingPublisher();
    bool publish(QJsonObject tracking,const QString& source,qint64 receivedMs,QString* error);
    bool invalidate(const QString& status,const QString& reason,QString* error=nullptr);
private:
    bool write(const QJsonObject& object,QString* error);
    QString directory_;
    int lockFd_=-1;
};
