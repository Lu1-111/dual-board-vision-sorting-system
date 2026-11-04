#ifndef SORTING_OBSERVER_HPP
#define SORTING_OBSERVER_HPP
#include <QJsonObject>
#include <QPointF>
#include <QString>
namespace sorting {
QJsonObject readObject(const QString &path,qint64 maximum=65536);
QJsonObject parseBeltStatus(const QByteArray &text);
QString currentMappingId(const QJsonObject &arm);
bool snapshotBound(const QJsonObject &result,const QString &directory);
QJsonObject verifyActions(const QJsonObject &config,const QJsonObject &arm);
// Observation only: this class has no socket, process, device or motion API.
class Observer {
public:
    explicit Observer(const QJsonObject &config):config_(config){}
    QJsonObject sample(qint64 now,const QJsonObject &tracking,const QJsonObject &v28,
                       bool snapshotVerified,const QJsonObject &arm,const QJsonObject &belt);
private:
    QJsonObject config_;
    QString session_,trackId_;
    quint64 sequence_=0;
    bool haveSequence_=false;
    QJsonObject lastTracking_;
    qint64 stoppedObservedAt_=-1;
    qint64 lastNow_=-1;
    qint64 stableBegin_=-1,fence_=-1;
    quint64 fenceSequence_=0;
    int stableFrames_=0;
    QPointF anchor_;
    void resetStable();
};
}
#endif
