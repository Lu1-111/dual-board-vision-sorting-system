#ifndef CONTINUOUS_DRIVE_HPP
#define CONTINUOUS_DRIVE_HPP
#include "conveyor/conveyor.hpp"
#include <QJsonObject>
#include <memory>
namespace traverse {
struct EncoderTuning {
    double cmPerCount=0.0005451409279338169;
    double fullSpeed=4.053667940115862, kp=0.08, ki=0.12, filterMs=60;
};
class SpeedSchedule {bool slow_=false;public:int update(double x){if(x>=1100)slow_=true;return slow_?50:75;}};
QJsonObject readProfile(conveyor::Registers&);
bool successfulStop(const QJsonObject&);
class Drive {
    conveyor::Registers&r_;std::unique_ptr<conveyor::MotorDriver>driver_;
    qint64 lastNow_=0,lastBeat_=-1,start_=-1,maxGap_=0,elapsed_=0;bool armed_=false,active_=false,finished_=false;
    EncoderTuning tuning_;bool encoder_=false;uint32_t count_=0;
    qint64 sampledAt_=0;double measured_=0,target_=0,integral_=0,duty_=0;
public:
    explicit Drive(conveyor::Registers&r,const EncoderTuning&t=EncoderTuning()):r_(r),tuning_(t){}
    ~Drive();
    void initialize(qint64 now);
    void tick(qint64 now,int dutyPercent,bool healthy);
    QJsonObject stop(qint64 now);
    QJsonObject diagnostics()const{return {{"active_elapsed_ms",double(elapsed_)},{"max_heartbeat_gap_ms",double(maxGap_)},{"software_active_limit_ms",7500},{"heartbeat_gap_limit_ms",100},{"slowdown_x",1100},{"encoder_control",encoder_},{"target_cm_s",target_},{"measured_cm_s",measured_},{"duty",duty_},{"encoder_count",QString::number(count_)},{"cm_per_count",tuning_.cmPerCount}};}
    bool active()const{return active_;}
    qint64 activeElapsed(qint64 now)const{return start_<0?0:now-start_;}
};
}
#endif
