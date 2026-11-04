#ifndef CAMERA_PREVIEW_CLIENT_H
#define CAMERA_PREVIEW_CLIENT_H
#include <QObject>
#include <QImage>
#include <QJsonObject>
#include <QByteArray>
#include <QSet>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Move to its own QThread before start(). Every slot runs in that thread.
// There is at most one request OR one delivered image awaiting UI acknowledgement.
class CameraPreviewClient : public QObject {
    Q_OBJECT
public:
    explicit CameraPreviewClient(QString url,int targetFps=10,QObject* parent=nullptr);
public slots:
    void start();
    void stop();
    void acknowledgeFrame();
signals:
    void frameReady(const QImage& image,const QJsonObject& meta);
    void stateChanged(const QString& status,const QString& message);
private slots:
    void requestFrame();
    void readAvailable();
    void requestFinished();
    void requestTimedOut();
    void checkFreshness();
private:
    void state(const QString& status,const QString& message);
    void scheduleRequest(int minimumDelayMs=0);
    void rejectRequest(const QString& status,const QString& message);
    bool inspectHeaders(QString* error);
    bool finishImage(QString* error);
    void clearReply();
    QUrl url_;
    int intervalMs_;
    bool running_=false,waitingAck_=false,rejected_=false,headersValid_=false,headerStale_=false;
    QNetworkAccessManager* manager_=nullptr;
    QNetworkReply* reply_=nullptr;
    QTimer* next_=nullptr;
    QTimer* deadline_=nullptr;
    QTimer* freshness_=nullptr;
    QByteArray bytes_;
    qint64 expectedBytes_=-1,requestStartedMs_=0,startedMs_=0;
    qint64 lastUniqueMs_=-1,lastSourceAgeMs_=0,fpsWindowMs_=0;
    int fpsWindowFrames_=0;
    double fps_=0;
    QString currentSession_,incomingSession_,incomingCaptured_,status_,message_;
    quint64 currentSequence_=0,incomingSequence_=0;
    qint64 incomingAgeMs_=0;
    int incomingWidth_=0,incomingHeight_=0;
    QSet<QString> retiredSessions_;
};
#endif
