#include "arm_actions.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QCryptographicHash>
#include <cmath>
#include <limits>

namespace {
QString mappingId(const arm::ArmConfig &mapping) {
    QJsonArray axes;
    for(const auto &axis:mapping.axes)axes.append(QJsonArray{
        int(axis.channel),axis.direction,axis.minAngleDeg,axis.maxAngleDeg,
        axis.minPulseUs,axis.maxPulseUs,axis.offsetUs});
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(axes).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
}
void ArmActions::bindAngleMapping(const arm::ArmConfig &mapping) {angleMappingId=mappingId(mapping);}
bool ArmActions::matchesAngleMapping(const arm::ArmConfig &mapping) const {
    return angleMappingId.isEmpty()||angleMappingId==mappingId(mapping);
}

bool ArmActions::load(const QByteArray &data, const std::array<double,6> &minimum,
                      const std::array<double,6> &maximum, QString &error) {
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QStringLiteral("动作 JSON 格式错误：") + parse.errorString(); return false;
    }
    const QJsonObject root = doc.object();
    const auto mappingValue=root.value("angleMappingId");
    QString nextMapping;
    if(!mappingValue.isUndefined()) {
        nextMapping=mappingValue.toString();
        bool valid=mappingValue.isString()&&nextMapping.size()==64;
        for(QChar ch:nextMapping)valid=valid&&((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f'));
        if(!valid){error=QStringLiteral("动作角度映射标识无效");return false;}
    }
    const double version=root.value("version").toDouble(-1);
    if ((version!=1&&version!=2) || !root.value("frames").isArray()) {
        error = QStringLiteral("动作文件须包含 version=1 或 2 和 frames 数组"); return false;
    }
    arm::CartesianFrame nextFrame;
    if(version==2) {
        const QJsonObject frame=root.value("coordinateFrame").toObject();
        const QJsonArray origin=frame.value("originBaseCm").toArray();
        if(!root.value("coordinateFrame").isObject()||origin.size()!=3||
           !frame.value("enabled").isBool()||!frame.value("enabled").toBool()||
           !frame.value("debugMapping").isBool()||!frame.value("referencePitchDeg").isDouble()) {
            error=QStringLiteral("坐标动作须记录 coordinateFrame 的原点、映射模式和参考俯仰");return false;
        }
        nextFrame.enabled=true;nextFrame.debugMapping=frame.value("debugMapping").toBool();
        nextFrame.pitchDeg=frame.value("referencePitchDeg").toDouble();
        for(int i=0;i<3;++i){
            if(!origin[i].isDouble()){error=QStringLiteral("动作原点须为有限数值");return false;}
            nextFrame.originCm[i]=origin[i].toDouble();
        }
        if(!nextFrame.valid()){error=QStringLiteral("动作原点或参考俯仰无效");return false;}
    }
    const QJsonArray arr = root.value("frames").toArray();
    if (arr.isEmpty() || arr.size() > 1000) {
        error = QStringLiteral("动作须包含 1 至 1000 帧"); return false;
    }
    QVector<ArmActionFrame> next;
    for (int n = 0; n < arr.size(); ++n) {
        if (!arr[n].isObject()) { error = QStringLiteral("动作帧须为对象"); return false; }
        const QJsonObject obj = arr[n].toObject();
        ArmActionFrame frame;
        const bool coordinate=version==2&&obj.value("type").toString()=="coordinate";
        if(version==2&&!coordinate&&obj.value("type").toString()!="angles") {
            error=QStringLiteral("第 %1 帧类型须为 angles 或 coordinate").arg(n+1);return false;
        }
        if(obj.contains(coordinate?"anglesDeg":"coordinate")) {
            error=QStringLiteral("第 %1 帧不能混合坐标和六轴角度目标").arg(n+1);return false;
        }
        frame.kind=coordinate?ArmActionFrame::Kind::Coordinate:ArmActionFrame::Kind::Angles;
        const QString angleKey=coordinate?QStringLiteral("toolAnglesDeg"):QStringLiteral("anglesDeg");
        const QJsonArray angles=obj.value(angleKey).toArray();
        const int angleCount=coordinate?2:6;
        if(!obj.value(angleKey).isArray()||angles.size()!=angleCount) {
            error=QStringLiteral("第 %1 帧须有 %2 个角度").arg(n+1).arg(angleCount);return false;
        }
        for (int axis = 0; axis < angleCount; ++axis) {
            const double angle = angles[axis].toDouble(std::numeric_limits<double>::quiet_NaN());
            if (!angles[axis].isDouble() || !std::isfinite(angle) ||
                angle < minimum[axis] || angle > maximum[axis]) {
                error = QStringLiteral("第 %1 帧轴 %2 角度越限或不是有限数字").arg(n+1).arg(axis+1);
                return false;
            }
            frame.angles[axis] = angle;
        }
        if(coordinate) {
            const QJsonArray values=obj.value("coordinate").toArray();
            if(!obj.value("coordinate").isArray()||values.size()!=4){error=QStringLiteral("第 %1 帧须有 [X,Y,Z,俯仰角]").arg(n+1);return false;}
            for(int i=0;i<4;++i) {
                const double value=values[i].toDouble(std::numeric_limits<double>::quiet_NaN());
                if(!values[i].isDouble()||!std::isfinite(value)){error=QStringLiteral("第 %1 帧坐标须为有限数值").arg(n+1);return false;}
                frame.coordinate[i]=value;
            }
            if(frame.coordinate[3]<-180||frame.coordinate[3]>360){error=QStringLiteral("动作俯仰超出关节角度总范围");return false;}
        }
        const double duration = obj.value("durationMs").toDouble(std::numeric_limits<double>::quiet_NaN());
        const double hold = obj.value("holdMs").toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!obj.value("durationMs").isDouble() || !obj.value("holdMs").isDouble() ||
            !std::isfinite(duration) || !std::isfinite(hold) ||
            duration < 20 || duration > 60000 || hold < 0 || hold > 60000 ||
            std::floor(duration) != duration || std::floor(hold) != hold) {
            error = QStringLiteral("第 %1 帧时间须为整数：运动 20–60000 ms，停留 0–60000 ms").arg(n+1);
            return false;
        }
        frame.durationMs = qint64(duration); frame.holdMs = qint64(hold); next.append(frame);
    }
    frames = next;coordinateFrame=nextFrame;angleMappingId=nextMapping; error.clear(); return true;
}
bool ArmActions::usesCoordinates() const {
    for(const auto &frame:frames)if(frame.kind==ArmActionFrame::Kind::Coordinate)return true;
    return false;
}
bool ArmActions::matchesCoordinateFrame(const arm::CartesianFrame &frame) const {
    if(!usesCoordinates())return true;
    if(!frame.enabled||!coordinateFrame.enabled||frame.debugMapping!=coordinateFrame.debugMapping)return false;
    for(int i=0;i<3;++i)if(std::fabs(frame.originCm[i]-coordinateFrame.originCm[i])>1e-6)return false;
    return std::fabs(frame.pitchDeg-coordinateFrame.pitchDeg)<=1e-6;
}
QByteArray ArmActions::json() const {
    const bool coordinates=usesCoordinates();
    QJsonArray arr;
    for (const ArmActionFrame &frame : frames) {
        QJsonObject obj;
        if(frame.kind==ArmActionFrame::Kind::Coordinate) {
            QJsonArray values;for(double value:frame.coordinate)values.append(value);
            obj.insert("type","coordinate");obj.insert("coordinate",values);
            obj.insert("toolAnglesDeg",QJsonArray{frame.angles[0],frame.angles[1]});
        } else {
            QJsonArray angles;for(double value:frame.angles)angles.append(value);
            obj.insert("anglesDeg",angles);if(coordinates)obj.insert("type","angles");
        }
        obj.insert("durationMs", double(frame.durationMs)); obj.insert("holdMs", double(frame.holdMs));
        arr.append(obj);
    }
    QJsonObject root; root.insert("version",coordinates?2:1); root.insert("frames", arr);
    if(!angleMappingId.isEmpty())root.insert("angleMappingId",angleMappingId);
    if(coordinates)root.insert("coordinateFrame",QJsonObject{
        {"enabled",coordinateFrame.enabled},{"originBaseCm",QJsonArray{coordinateFrame.originCm[0],coordinateFrame.originCm[1],coordinateFrame.originCm[2]}},
        {"debugMapping",coordinateFrame.debugMapping},{"referencePitchDeg",coordinateFrame.pitchDeg}});
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
bool ArmActions::save(const QString &path, QString &error) const {
    QSaveFile file(path);
    const QByteArray data = json();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString(); return false;
    }
    error.clear(); return true;
}
void ArmActionPlayer::play(const ArmActions &actions) {
    stop(); frames_ = actions.frames; active_ = !frames_.isEmpty(); error_.clear();
}
void ArmActionPlayer::stop() {
    active_ = false; waitingMotion_ = false; waitingHold_ = false;
    frames_.clear(); index_ = 0; holdUntil_ = 0; error_.clear();
}
void ArmActionPlayer::tick(qint64 nowMs, bool moving, const QString &motionError, const Start &start) {
    if (!active_) return;
    if (!motionError.isEmpty()) { stop(); error_ = motionError; return; }
    if (waitingMotion_) {
        if (moving) return;
        waitingMotion_ = false; waitingHold_ = true;
        holdUntil_ = nowMs + frames_[index_].holdMs;
    }
    if (waitingHold_) {
        if (nowMs < holdUntil_) return;
        waitingHold_ = false; ++index_;
    }
    if (index_ >= frames_.size()) { stop(); return; }
    if (!waitingMotion_) {
        QString failure;
        if (!start(frames_[index_], failure)) {
            stop(); error_ = failure.isEmpty() ? QStringLiteral("动作帧启动失败") : failure; return;
        }
        waitingMotion_ = true;
    }
}
