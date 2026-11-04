#pragma once

#include "dataset_store.hpp"
#include <QDateTime>
#include <QImage>
#include <QMutex>
#include <QThread>
#include <atomic>

// All public methods are safe to call from the UI thread. Capture and file I/O
// happen exclusively in run(); QThread's QObject itself remains in the UI thread.
class CaptureWorker : public QThread
{
    Q_OBJECT
public:
    explicit CaptureWorker(bool simulation, QObject *parent = nullptr);
    ~CaptureWorker() override;
    bool requestPhoto(const SaveOptions &options);
    void stop();
    void acknowledgePreview();
    bool simulation() const { return m_simulation; }
    bool requiresHardwareReset() const { return m_requiresHardwareReset.load(); }

signals:
    // Direct connection only: consumer performs bounded cache offering, no JPEG/UI work.
    void streamFrameReady(const QImage &image, const QDateTime &capturedUtc, quint64 frameId);
    void frameReady(const QImage &image, const QDateTime &capturedUtc, quint64 frameId);
    void photoSaved(const SaveResult &result);
    void statusChanged(const QString &message);
    void captureError(const QString &message);

protected:
    void run() override;

private:
    bool pause(unsigned milliseconds);
    void completePhoto(const SaveResult &result);
    void cancelPending(const QString &reason);
    const bool m_simulation;
    std::atomic_bool m_requiresHardwareReset { false };
    QMutex m_mutex;
    bool m_accepting = false;
    bool m_photoBusy = false;
    bool m_pending = false;
    bool m_previewOutstanding = false;
    SaveOptions m_options;
};
