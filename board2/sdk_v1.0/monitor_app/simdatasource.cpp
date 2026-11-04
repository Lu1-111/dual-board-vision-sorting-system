#include "simdatasource.h"
#include <QDateTime>
#include <QRandomGenerator>

SimDataSource::SimDataSource(int tickMs, QObject *parent) : IDataSource(parent)
{
    sessionId_ = QRandomGenerator::global()->generate() | 1u;
    timer_.setInterval(qMax(20, tickMs));
    connect(&timer_, &QTimer::timeout, this, &SimDataSource::tick);
    timer_.start();
}

void SimDataSource::setScenario(int scenario)
{
    if (scenario < -1 || scenario > 3) return;
    scenario_ = scenario;
    tick();
}

void SimDataSource::tick()
{
    ++elapsedS_;
    ++sequence_;
    const int phase = elapsedS_ % 60;
    int scene = scenario_;
    if (scene < 0)
        scene = phase >= 10 && phase < 18 ? 1
              : phase >= 26 && phase < 36 ? 2
              : phase >= 44 && phase < 52 ? 3 : 0;
    const QDateTime now = QDateTime::currentDateTime();
    if (scene != activeScenario_) {
        if (activeScenario_ != 0) {
            activeEvent_.active = false;
            activeEvent_.recoveredAt = now;
            activeEvent_.durationS = qMin<qint64>(65535, qMax<qint64>(0, activeEvent_.detectedAt.secsTo(now)));
            emit alarmCleared(activeEvent_);
        }
        activeScenario_ = scene;
        if (scene != 0) {
            activeEvent_ = AlarmEvent();
            activeEvent_.sessionId = sessionId_;
            activeEvent_.eventId = nextEventId_++;
            activeEvent_.type = static_cast<AlarmType>(scene);
            activeEvent_.color = scene == 1 ? MaterialColor::GreenDefect
                               : scene == 2 ? MaterialColor::Red : MaterialColor::None;
            activeEvent_.detectedAt = now;
            emit alarmRaised(activeEvent_);
        }
    }
    const int colorIndex = (elapsedS_ / 2) % 4;
    const bool moving = scene < 2 && elapsedS_ % 2 == 0;
    if (moving && scene == 0) ++counts_[colorIndex];
    if (moving && scene == 1) ++counts_[4];
    LineStatus frame;
    // Visual evidence alone does not establish a stopped/faulted production line.
    frame.runState = RunState::Running;
    frame.armState = moving ? 1 : 0;
    frame.visionState = 1;
    frame.currentColor = scene ? activeEvent_.color : static_cast<MaterialColor>(colorIndex + 1);
    frame.confidence100 = frame.currentColor == MaterialColor::None ? 0 : 92;
    emit status(frame);
    emit counters(counts_[0], counts_[1], counts_[2], counts_[3], counts_[4]);
    Heartbeat beat;
    beat.sequence = sequence_;
    beat.runState = frame.runState;
    beat.activeAlarmCount = scene != 0 ? 1 : 0;
    beat.uptimeS = elapsedS_;
    emit heartbeat(beat);
}
