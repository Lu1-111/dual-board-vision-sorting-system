#ifndef MONITOR_DATASOURCE_H
#define MONITOR_DATASOURCE_H

#include <QColor>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QtGlobal>

// 界面内部数据结构；当前分类为演示值，最终类别与 CAN 编码需联调时确认。
// 不可将含 QDateTime 的结构直接当作 CAN 帧发送。
enum class MaterialColor : quint8 {
    None = 0,
    Red = 1,
    Yellow = 2,
    Blue = 3,
    GreenGood = 4,
    GreenDefect = 5,
};

enum class RunState : quint8 {
    Idle = 0,
    Running = 1,
    Paused = 2,
    Fault = 3,
};

enum class AlarmType : quint8 {
    ColorMismatch = 1,
    MaterialStuck = 2,
    MaterialMissing = 3,
};

struct Heartbeat {
    quint8 version = 1;
    quint8 sequence = 0;
    RunState runState = RunState::Idle;
    quint8 activeAlarmCount = 0;
    quint32 uptimeS = 0;
};

struct LineStatus {
    RunState runState = RunState::Idle;
    quint8 armState = 0;     // 0 空闲 1 运动中 2 故障
    quint8 visionState = 0;  // 0 关闭 1 正常 2 降级 3 故障
    MaterialColor currentColor = MaterialColor::None;
    quint8 confidence100 = 0;  // 置信度 x100
};

struct AlarmEvent {
    quint32 sessionId = 0;
    bool simulated = true;
    quint32 eventId = 0;
    AlarmType type = AlarmType::ColorMismatch;
    MaterialColor color = MaterialColor::None;
    quint16 durationS = 0;  // 激活时为 0（由界面按 detectedAt 计时），恢复时为总持续秒数
    bool active = true;
    bool recoveryUnknown = false; // Later snapshot proved recovery, exact time unavailable.
    QDateTime detectedAt;
    QDateTime recoveredAt;
};

// 监控界面的统一数据入口。SimDataSource 用本地剧本驱动界面联调；
// 后续 CAN 接入时实现 CanDataSource 替换，界面代码不变。
class IDataSource : public QObject {
    Q_OBJECT
public:
    explicit IDataSource(QObject *parent = nullptr) : QObject(parent) {}
    ~IDataSource() override = default;
    virtual QString sourceName() const = 0;
    virtual bool isCan() const { return false; }
    virtual bool isSimulation() const { return true; }

signals:
    void linkState(bool online, const QString &message);
    void sessionReset();
    void heartbeat(const Heartbeat &beat);
    void status(const LineStatus &frame);
    void counters(quint32 red, quint32 yellow, quint32 blue,
                  quint32 greenGood, quint32 greenDefect);
    void alarmRaised(const AlarmEvent &event);
    void alarmCleared(const AlarmEvent &event);
};

inline QString materialColorName(MaterialColor color)
{
    switch (color) {
    case MaterialColor::Red: return QStringLiteral("红");
    case MaterialColor::Yellow: return QStringLiteral("黄");
    case MaterialColor::Blue: return QStringLiteral("蓝");
    case MaterialColor::GreenGood: return QStringLiteral("绿(完好)");
    case MaterialColor::GreenDefect: return QStringLiteral("绿(缺陷)");
    case MaterialColor::None: break;
    }
    return QStringLiteral("无");
}

inline QString materialColorHex(MaterialColor color)
{
    switch (color) {
    case MaterialColor::Red: return QStringLiteral("#e05252");
    case MaterialColor::Yellow: return QStringLiteral("#e0c341");
    case MaterialColor::Blue: return QStringLiteral("#4f86e0");
    case MaterialColor::GreenGood: return QStringLiteral("#3f9d63");
    case MaterialColor::GreenDefect: return QStringLiteral("#8f5bb5");
    case MaterialColor::None: break;
    }
    return QStringLiteral("#5a6b80");
}

// Legacy CAN colors 1..3 do not carry quality labels. Keep wire codes intact.
inline QString materialQualityName(MaterialColor color)
{
    if (color == MaterialColor::GreenGood) return QStringLiteral("完好");
    if (color == MaterialColor::GreenDefect) return QStringLiteral("缺陷");
    return color == MaterialColor::None ? QStringLiteral("—") : QStringLiteral("未判定");
}

inline QString materialQualityHex(MaterialColor color)
{
    if (color == MaterialColor::GreenGood) return QStringLiteral("#3f9d63");
    if (color == MaterialColor::GreenDefect) return QStringLiteral("#e05252");
    return QStringLiteral("#5a6b80");
}

inline QString alarmTypeName(AlarmType type)
{
    switch (type) {
    case AlarmType::ColorMismatch: return QStringLiteral("颜色不符");
    case AlarmType::MaterialStuck: return QStringLiteral("物料滞留");
    case AlarmType::MaterialMissing: return QStringLiteral("缺料");
    }
    return QStringLiteral("未知");
}

inline QString runStateName(RunState state)
{
    switch (state) {
    case RunState::Idle: return QStringLiteral("待机");
    case RunState::Running: return QStringLiteral("运行中");
    case RunState::Paused: return QStringLiteral("已暂停");
    case RunState::Fault: return QStringLiteral("故障");
    }
    return QStringLiteral("未知");
}

inline QString formatHms(quint32 totalSeconds)
{
    const quint32 hours = totalSeconds / 3600;
    const quint32 minutes = (totalSeconds % 3600) / 60;
    const quint32 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

#endif
