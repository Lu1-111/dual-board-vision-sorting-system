#ifndef MONITOR_OVERVIEWPAGE_H
#define MONITOR_OVERVIEWPAGE_H

#include "datasource.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QWidget>

class QLabel;
class QListWidget;
class QTimer;

class OverviewPage : public QWidget {
public:
    explicit OverviewPage(const QString &sourceName, QWidget *parent = nullptr);

    void applyHeartbeat(const Heartbeat &beat);
    void applyStatus(const LineStatus &frame);
    void applyCounters(quint32 red, quint32 yellow, quint32 blue,
                       quint32 greenGood, quint32 greenDefect);
    void alarmRaised(const AlarmEvent &event);
    void alarmCleared(const AlarmEvent &event);
    void setCanMode(bool enabled);
    void setLinkState(bool online, const QString &message);
    void resetSession();

private:
    void refreshTick();
    void renderHeartbeat(const Heartbeat &beat);
    void renderStatus(const LineStatus &frame);
    Heartbeat pendingBeat_;
    LineStatus pendingStatus_;
    bool haveBeat_=false, haveStatus_=false, haveCounts_=false, online_=false;
    quint32 pendingCounts_[2]={};
    QString linkMessage_;
    QElapsedTimer statusAge_, countsAge_;
    void rebuildActiveAlarms();
    static QString alarmSummary(const AlarmEvent &event);

    QLabel *stateValue_;
    QLabel *commTitle_ = nullptr;
    bool canMode_ = false;
    QLabel *commValue_;
    QLabel *sourceValue_;
    QLabel *uptimeValue_;
    QLabel *countValues_[2];
    QLabel *currentChip_;
    QLabel *currentName_;
    QLabel *confidenceValue_;
    QLabel *visionValue_;
    QLabel *noAlarmLabel_;
    QListWidget *alarmList_;
    QTimer *timer_;

    QElapsedTimer lastHeartbeatAt_;
    quint8 lastSequence_ = 0;
    QHash<quint32, AlarmEvent> activeAlarms_;
};

#endif
