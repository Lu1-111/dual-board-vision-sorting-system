#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <array>
#include <functional>
#include "../arm_servo_linux/arm_control.hpp"

struct ArmActionFrame {
    enum class Kind { Angles, Coordinate };
    Kind kind=Kind::Angles;
    std::array<double, 6> angles{{0,0,0,0,0,0}};
    std::array<double, 4> coordinate{{0,0,0,0}};
    qint64 durationMs = 1000;
    qint64 holdMs = 0;
};
class ArmActions {
public:
    QVector<ArmActionFrame> frames;
    arm::CartesianFrame coordinateFrame;
    QString angleMappingId;
    void bindAngleMapping(const arm::ArmConfig &mapping);
    bool matchesAngleMapping(const arm::ArmConfig &mapping) const;
    bool usesCoordinates() const;
    bool matchesCoordinateFrame(const arm::CartesianFrame &frame) const;
    bool load(const QByteArray &json, const std::array<double,6> &minimum,
              const std::array<double,6> &maximum, QString &error);
    QByteArray json() const;
    bool save(const QString &path, QString &error) const;
};
// Start callback must issue a nonblocking move. A failed frame cancels the queue.
class ArmActionPlayer {
public:
    using Start = std::function<bool(const ArmActionFrame &, QString &)>;
    void play(const ArmActions &actions);
    void stop();
    void tick(qint64 nowMs, bool moving, const QString &motionError, const Start &start);
    bool active() const { return active_; }
    int index() const { return index_; }
    QString error() const { return error_; }
private:
    QVector<ArmActionFrame> frames_;
    bool active_ = false, waitingMotion_ = false, waitingHold_ = false;
    int index_ = 0;
    qint64 holdUntil_ = 0;
    QString error_;
};
