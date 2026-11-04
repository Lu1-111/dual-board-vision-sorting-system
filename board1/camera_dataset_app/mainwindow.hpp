#pragma once

#include <QMainWindow>
#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include "dataset_store.hpp"

class CaptureWorker;
class LatestFrameServer;
class PreviewWidget;
class QLabel;
class QPushButton;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QTimer;
class QCloseEvent;

struct AppOptions {
    bool simulation = false;
    bool openOnStart = false;
    bool captureFirst = false;
    bool pcOnly = false;
    bool stream = false;
    bool streamOnly = false;
    bool trackCenters = false;
    QString trackingOutputDir = QStringLiteral("/tmp/triangle-track");
    QString streamAddress = QStringLiteral("172.22.203.1");
    quint16 streamPort = 47003;
    int streamFps = 3;
    int streamQuality = 95;
    int previewFps = 15;
    int previewWidth = 640;
    int previewQuality = 75;
    QString output;
    QString dataset;
    QString scene;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const AppOptions &options, QWidget *parent = nullptr);
    ~MainWindow() override;
    QJsonObject cameraCommand(const QJsonObject &command);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void buildUi();
    void openCamera();
    void closeCamera();
    void workerFinished();
    void receiveFrame(const QImage &frame, const QDateTime &utc, quint64 sequence);
    void capturePhoto();
    void startBurst();
    void stopBurst();
    void photoSaved(const SaveResult &result);
    void updateControls();
    void updateStorage();
    bool validateSettings();
    void saveSettings();
    void setStatus(const QString &message, bool error = false);
    void updateGuidance();

    AppOptions m_options;
    CaptureWorker *m_worker = nullptr;
    LatestFrameServer *m_stream = nullptr;
    PreviewWidget *m_preview = nullptr;
    QLabel *m_cameraState = nullptr;
    QLabel *m_frameInfo = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_guidance = nullptr;
    QTimer *m_guidanceTimer = nullptr;
    QString m_guidanceText;
    QString m_guidancePhase;
    int m_guidanceSeconds = 0;
    int m_guidanceStep = 0;
    int m_guidanceTotal = 0;
    QLabel *m_count = nullptr;
    QLabel *m_disk = nullptr;
    QLabel *m_lastImage = nullptr;
    QLabel *m_lastPath = nullptr;
    QPushButton *m_open = nullptr;
    QPushButton *m_close = nullptr;
    QPushButton *m_capture = nullptr;
    QPushButton *m_startBurst = nullptr;
    QPushButton *m_stopBurst = nullptr;
    QPushButton *m_browse = nullptr;
    QLineEdit *m_root = nullptr;
    QLineEdit *m_dataset = nullptr;
    QLineEdit *m_scene = nullptr;
    QComboBox *m_format = nullptr;
    QSpinBox *m_quality = nullptr;
    QDoubleSpinBox *m_interval = nullptr;
    QTimer *m_burstTimer = nullptr;
    QTimer *m_storageTimer = nullptr;
    bool m_freshFrame = false;
    bool m_photoPending = false;
    bool m_stopping = false;
    bool m_closing = false;
    bool m_captureErrorLatched = false;
    bool m_requiresBoardReset = false;
    bool m_startupCapturePending = false;
    QByteArray m_pendingImage;
    QJsonObject m_pendingMetadata;
    QString m_lastAckId;
    QString m_lastAckSha;
    quint64 m_saved = 0;
    quint64 m_skipped = 0;
};
