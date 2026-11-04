#ifndef MONITOR_LIVEVISIONPAGE_H
#define MONITOR_LIVEVISIONPAGE_H

#include <QPixmap>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QResizeEvent;
class QTimer;
class QThread;
class CameraPreviewClient;

// Read-only view of the background worker's atomic image/result publication.
class LiveVisionPage : public QWidget {
public:
    explicit LiveVisionPage(QWidget *parent = nullptr, const QString &directory = QString());
    ~LiveVisionPage() override;
    void refresh();
    void receivePreview(const QImage &image, const QJsonObject &meta);
    void receivePreviewState(const QString &status, const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void unavailable(const QString &status, const QString &message);
    void updatePreview();
    void renderLastResult(const QString &updateStatus, const QString &message, bool retained);
    void clearPreview(const QString &message);
    void expirePreview();
    QString directory_;
    QString previewKey_, previewSession_;
    double previewPublishedMs_ = 0, previewSourceAgeMs_ = 0;
    qint64 previewFirstSeenMs_ = 0;
    QThread *previewThread_ = nullptr;
    CameraPreviewClient *previewClient_ = nullptr;
    QPixmap frame_;
    QJsonObject lastResult_; // Replaced only after the whole new result validates.
    QLabel *status_;
    QLabel *message_;
    QLabel *preview_;
    QLabel *prediction_;
    QLabel *confidence_;
    QLabel *state_;
    QLabel *resultInfo_;
    QLabel *performance_;
    QLabel *scope_;
    QProgressBar *probabilities_[3];
    QTimer *timer_;
};

#endif
