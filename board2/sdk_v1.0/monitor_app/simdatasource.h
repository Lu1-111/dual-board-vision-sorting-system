#ifndef MONITOR_SIMDATASOURCE_H
#define MONITOR_SIMDATASOURCE_H

#include "datasource.h"

#include <QTimer>
#include <QVector>

// 本地循环或手动场景模拟；不操作真实设备。
class SimDataSource : public IDataSource {
    Q_OBJECT
public:
    explicit SimDataSource(int tickMs = 1000, QObject *parent = nullptr);
    QString sourceName() const override { return QStringLiteral("模拟数据"); }
    // -1 自动；0 正常；1 颜色不符；2 滞留；3 缺料。仅用于演示。
    void setScenario(int scenario);

private:
    void tick();

    QTimer timer_;
    quint32 elapsedS_ = 0;
    quint8 sequence_ = 0;
    quint32 nextEventId_ = 1;
    quint32 sessionId_ = 0;
    quint32 counts_[5] = {0, 0, 0, 0, 0};  // 红 黄 蓝 绿好 绿缺陷
    int scenario_ = -1;
    int activeScenario_ = 0;
    AlarmEvent activeEvent_;
};

#endif
