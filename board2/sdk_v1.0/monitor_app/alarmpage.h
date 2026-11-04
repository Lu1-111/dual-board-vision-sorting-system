#ifndef MONITOR_ALARMPAGE_H
#define MONITOR_ALARMPAGE_H

#include "datasource.h"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

class AlarmPage : public QWidget {
public:
    explicit AlarmPage(QWidget *parent = nullptr);

    void alarmRaised(const AlarmEvent &event);
    void alarmCleared(const AlarmEvent &event);
    void resetSession();

private:
    void rebuild();

    QComboBox *filter_;
    QLabel *stats_;
    QTableWidget *table_;
    QVector<AlarmEvent> events_;
};

#endif
