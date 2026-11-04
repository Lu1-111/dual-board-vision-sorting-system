#include "drive.hpp"
#include <stdexcept>
#include <cmath>
namespace traverse {
static void need(bool value,const char*s){if(!value)throw std::runtime_error(s);}
bool successfulStop(const QJsonObject&p){return p.value("status").toDouble(-1)==0&&p.value("active_duty").toDouble(-1)==0&&p.value("encoder_errors").toDouble(-1)==0;}
QJsonObject readProfile(conveyor::Registers&r){
    using namespace conveyor;
    need(r.read(Id)==RegisterId,"conveyor ID mismatch");
    // Reject legacy6 before attempting its unsupported0x34 register or any write.
    const auto features=r.read(Features);need(features==14||features==30,"requires bounded8s profile (Features14 or encoder Features30)");
    const auto limit=r.read(RunLimitMs),clock=r.read(ClockHz),watchdog=r.read(Watchdog),period=r.read(Period);
    need(limit==8000&&clock==75000000&&watchdog==18750000&&period==3750,"continuous profile limit/clock/watchdog/PWM mismatch");
    return {{"features",int(features)},{"run_limit_ms",int(limit)},{"clock_hz",int(clock)},{"watchdog_ticks",int(watchdog)},{"period",int(period)},
        {"status",double(r.read(Status))},{"active_duty",double(r.read(ActiveDuty))},{"encoder_errors",double(r.read(EncoderErrors))}};
}
Drive::~Drive(){if(armed_){try{stop(lastNow_);}catch(...){}}}
void Drive::initialize(qint64 now){
    need(!armed_&&!finished_,"drive cannot be restarted");need(now>=0,"monotonic clock invalid");r_.setNow(uint64_t(now));const auto p=readProfile(r_);
    need(p.value("status").toDouble()==0&&p.value("active_duty").toDouble()==0&&p.value("encoder_errors").toDouble()==0,"initial drive not disabled fault-free");
    driver_.reset(new conveyor::MotorDriver(r_));driver_->initialize(uint64_t(now));lastNow_=now;armed_=true;
    encoder_=(p.value("features").toInt()&16)!=0;
    count_=r_.read(conveyor::EncoderCount);sampledAt_=now;
}
void Drive::tick(qint64 now,int percent,bool healthy){
    need(armed_&&!finished_,"drive is not armed or already stopped");
    if(now<lastNow_){stop(lastNow_);throw std::runtime_error("monotonic clock regressed");}lastNow_=now;
    if(!healthy||(percent!=50&&percent!=75)){stop(now);throw std::runtime_error("control health/duty invalid; stopped");}
    if(active_){elapsed_=now-start_;maxGap_=qMax(maxGap_,now-lastBeat_);
        if(elapsed_>=7500){stop(now);throw std::runtime_error("software active7500ms deadline exceeded");}
        if(now-lastBeat_>100){stop(now);throw std::runtime_error("heartbeat100ms deadline exceeded");}
    }
    if((driver_->status(uint64_t(now))&conveyor::FaultMask)||r_.read(conveyor::EncoderErrors)){stop(now);throw std::runtime_error("hardware fault/encoder error; no clear or restart");}
    const double feedforward=percent/100.0;
    duty_=feedforward;
    if(encoder_){
        const auto count=r_.read(conveyor::EncoderCount);const uint32_t raw=count-count_;
        const int64_t delta=raw<=0x7fffffffu?int64_t(raw):int64_t(raw)-4294967296LL;
        const double dt=(now-sampledAt_)/1000.0;
        target_=tuning_.fullSpeed*feedforward;
        if(dt>0){
            // Existing feeding is forward-only; A/B polarity changes the count
            // sign, not the magnitude of the belt speed being controlled.
            const double speed=std::abs(double(delta))*tuning_.cmPerCount/dt;
            measured_+=(speed-measured_)*(dt*1000)/(tuning_.filterMs+dt*1000);
            if(active_){
                const double error=target_-measured_;
                const double next=integral_+tuning_.ki*error*dt;
                const double output=feedforward+tuning_.kp*error+next;
                if((output>=0&&output<=1)||(output>1&&error<0)||(output<0&&error>0))integral_=next;
                duty_=qBound(0.0,feedforward+tuning_.kp*error+integral_,1.0);
            }
        }
        count_=count;sampledAt_=now;
    }
    if(!active_){start_=now;active_=true;}driver_->command(duty_,false,uint64_t(now));lastBeat_=now;
    const auto status=r_.read(conveyor::Status),duty=r_.read(conveyor::ActiveDuty);const unsigned expected=unsigned(std::llround(3750*duty_));
    if(now-start_>5&&(bool(status&1)!=(expected!=0)||(status&2)||duty!=expected)){stop(now);throw std::runtime_error("enabled/direction/duty readback mismatch");}
}
QJsonObject Drive::stop(qint64 now){
    if(armed_){lastNow_=qMax(lastNow_,now);driver_->stop(uint64_t(lastNow_));active_=false;finished_=true;}
    const auto p=readProfile(r_);need(!(quint32(p.value("status").toDouble())&1)&&p.value("active_duty").toDouble()==0,"stop readback still enabled/duty nonzero");return p;
}
}
