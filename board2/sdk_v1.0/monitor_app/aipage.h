#ifndef MONITOR_AIPAGE_H
#define MONITOR_AIPAGE_H

#include "datasource.h"

#include <QVector>
#include <QWidget>
#include <QHash>
#include <QJsonObject>

class QLabel;
class QListWidget;
class QPushButton;
class QTextBrowser;

// 离线规则分析；外部模型接口将在后续接入。
class AiPage : public QWidget {
public:
    explicit AiPage(QWidget *parent = nullptr);

    void alarmRaised(const AlarmEvent &event);
    void alarmCleared(const AlarmEvent &event);
    void resetSession();

private:
    void refreshAlarmList();
    void generateAnalysis();
    void publishEvents();
    void refreshEvidence();
    void requestAnalysis();
    QJsonObject selectedEvent() const;

    QLabel *modeBadge_;
    QListWidget *alarmList_;
    QPushButton *generateButton_;
    QTextBrowser *result_;
    QVector<AlarmEvent> events_;
    QLabel *image_;
    QLabel *imageNote_;
    QPushButton *externalButton_;
    QHash<QString,QString> requests_;
    QString shownImage_, shownResult_;
};

#endif
