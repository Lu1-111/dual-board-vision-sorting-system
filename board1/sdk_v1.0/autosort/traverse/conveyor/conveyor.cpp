#include "conveyor.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace conveyor {
namespace {
void require(bool condition,const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
std::string trim(const std::string &s) {
    const auto a=s.find_first_not_of(" \r\n\t");
    return a==std::string::npos ? "" : s.substr(a,s.find_last_not_of(" \r\n\t")-a+1);
}
}
MockRegisters::MockRegisters() {}
void MockRegisters::setNow(uint64_t now) {
    require(now>=now_,"mock clock moved backwards"); now_=now;
    if (enabled_ && now_-last_>=250) { faults_|=0x100; enabled_=false; active_=0; }
}
uint32_t MockRegisters::read(uint32_t offset) {
    switch(offset) {
    case Id:return RegisterId; case Period:return 2500;
    case Duty:return duty_; case Control:return control_;
    case Status:return faults_ | (enabled_?1u:0u) | ((control_&2)?2u:0u);
    case Watchdog:return 12500000; case ActiveDuty:return active_;
    case ClockHz:return 50000000;
    case EncoderCount:return encoder_; case EncoderErrors:return encoderErrors_;
    case Features:return 0;
    default:throw std::runtime_error("invalid register read");
    }
}
void MockRegisters::write(uint32_t offset,uint32_t value) {
    if (offset==Duty) { duty_=value; return; }
    if (offset==Control) { control_=value; return; }
    require(offset==Apply && value==0xc0de0001u,"invalid register write");
    last_=now_;
    if (duty_>2500 || (control_&~7u) || ((control_&4) && ((control_&1)||duty_))) {
        faults_|=0x400; enabled_=false; active_=0;
    } else if (control_&4) {
        if (!external_) faults_=0;
        enabled_=false; active_=0;
    } else if (!(control_&1)) { enabled_=false; active_=0; }
    else if (!faults_ && !external_) { enabled_=duty_!=0; active_=duty_; }
}
void MockRegisters::injectFault(bool active) {
    external_=active;
    if (active) { faults_|=0x200; enabled_=false; active_=0; }
}
void MockRegisters::advanceEncoder(int32_t delta,bool invalid) {
    encoder_+=static_cast<uint32_t>(delta);
    if(invalid) ++encoderErrors_;
}
MotorDriver::MotorDriver(Registers &r):registers_(r) {}
MotorDriver::~MotorDriver() { if(period_) {try { stop(lastNow_); } catch(...) {}} }
void MotorDriver::commit(uint32_t duty,uint32_t control) {
    registers_.write(Duty,duty); registers_.write(Control,control);
    registers_.write(Apply,0xc0de0001u);
    // Completes posted writes before another command is submitted.
    (void)registers_.read(Status);
}
void MotorDriver::initialize(uint64_t now) {
    lastNow_=now; registers_.setNow(now);
    require(registers_.read(Id)==RegisterId,"conveyor PL ID/version mismatch");
    period_=registers_.read(Period);
    require(period_>=2 && period_<=100000000,"invalid PWM period");
    const uint32_t hz=registers_.read(ClockHz), wd=registers_.read(Watchdog);
    require(hz>0 && static_cast<double>(wd)/hz>=0.1,"PL watchdog is too short");
    stop(now); // never clear an inherited fault or start at initialization
}
uint32_t MotorDriver::status(uint64_t now) {
    require(period_!=0,"driver not initialized");
    require(now>=lastNow_,"driver clock moved backwards");
    lastNow_=now; registers_.setNow(now); return registers_.read(Status);
}
uint32_t MotorDriver::encoderCount() {return registers_.read(EncoderCount);}
uint32_t MotorDriver::encoderErrors() {return registers_.read(EncoderErrors);}
uint32_t MotorDriver::features() {return registers_.read(Features);}
void MotorDriver::benchAB(unsigned ab) {
    require((features()&1)&&ab<4,"AB injection requires output-locked PL bench build");
    registers_.write(BenchAB,ab);(void)registers_.read(BenchAB);
}
void MotorDriver::command(double fraction,bool reverse,uint64_t now) {
    require(std::isfinite(fraction) && fraction>=0 && fraction<=1,"duty must be 0..1");
    const auto s=status(now);
    require(!(s&FaultMask),"PL latched fault: "+std::to_string(s&FaultMask));
    commit(static_cast<uint32_t>(std::llround(fraction*period_)),
           (fraction>0?1u:0u)|(reverse?2u:0u));
    require(!(registers_.read(Status)&FaultMask),"PL rejected motor command");
}
void MotorDriver::stop(uint64_t now) {
    require(period_!=0,"driver not initialized");
    require(now>=lastNow_,"driver clock moved backwards");
    lastNow_=now; registers_.setNow(now); commit(0,0);
}
void MotorDriver::clearFault(uint64_t now) {
    stop(now); commit(0,4);
    require(!(registers_.read(Status)&FaultMask),"external fault remains active");
}
void Config::validate() const {
    const double values[]={originX,originY,axisX,axisY,target,lateralMin,lateralMax,
        tolerance,slowDistance,maxDuty,minDuty,leadMs,stableSpeed,cmPerCount,encoderSign,
        maxSpeed,minSpeed,speedKp,speedKi,speedFilterMs,stallMinSpeed,maxEncoderSpeed};
    for(double v:values) require(std::isfinite(v),"non-finite configuration");
    require(std::abs(std::hypot(axisX,axisY)-1)<1e-6,"belt axis must be a unit vector");
    require(lateralMin<lateralMax,"invalid lateral range");
    require(tolerance>0 && slowDistance>tolerance,"invalid stopping distances");
    require(minDuty>0 && minDuty<=maxDuty && maxDuty<=1,"invalid PWM limits");
    require(leadMs>=0 && leadMs<=1000 && stableSpeed>0,"invalid lead/stability limits");
    require(visionTimeoutMs>=40 && visionTimeoutMs<=10000,"invalid vision timeout");
    require(stableMs>=20 && stableMs<=60000 && maxRunMs>=visionTimeoutMs &&
            maxRunMs<=3600000 && stableFrames>=2 && stableFrames<=1000,
            "invalid timing/frame limits");
    require(encoderEnabled<=1 && motorReverse<=1,"encoder_enabled/motor_reverse must be 0 or 1");
    require(cmPerCount>=0 && (encoderSign==1 || encoderSign==-1),"invalid encoder scale/sign");
    require(!encoderEnabled || cmPerCount>0,"measure cm_per_count before enabling encoder speed control");
    require(minSpeed>0 && maxSpeed>=minSpeed && maxEncoderSpeed>maxSpeed,
            "invalid encoder speed limits (cm/s)");
    require(speedKp>=0 && speedKi>=0 && speedFilterMs>=0 && speedFilterMs<=1000 &&
            stallMinSpeed>0 && stallMinSpeed<minSpeed && stallMs>=100 && stallMs<=10000,
            "invalid speed controller/stall settings");
}
Config loadConfig(const std::string &path) {
    Config c;
    std::map<std::string,double*> doubles={
        {"origin_x",&c.originX},{"origin_y",&c.originY},{"axis_x",&c.axisX},
        {"axis_y",&c.axisY},{"target",&c.target},{"lateral_min",&c.lateralMin},
        {"lateral_max",&c.lateralMax},{"tolerance",&c.tolerance},
        {"slow_distance",&c.slowDistance},{"max_duty",&c.maxDuty},
        {"min_duty",&c.minDuty},{"lead_ms",&c.leadMs},{"stable_speed",&c.stableSpeed},
        {"cm_per_count",&c.cmPerCount},{"encoder_sign",&c.encoderSign},
        {"max_speed_cm_s",&c.maxSpeed},{"min_speed_cm_s",&c.minSpeed},
        {"speed_kp",&c.speedKp},{"speed_ki",&c.speedKi},{"speed_filter_ms",&c.speedFilterMs},
        {"stall_min_speed_cm_s",&c.stallMinSpeed},{"max_encoder_speed_cm_s",&c.maxEncoderSpeed}};
    std::map<std::string,uint64_t*> integers={{"vision_timeout_ms",&c.visionTimeoutMs},
        {"stable_ms",&c.stableMs},{"max_run_ms",&c.maxRunMs},
        {"encoder_enabled",&c.encoderEnabled},{"motor_reverse",&c.motorReverse},
        {"stall_ms",&c.stallMs}};
    std::ifstream file(path); require(bool(file),"cannot open config: "+path);
    std::set<std::string> seen; std::string line;
    while(std::getline(file,line)) {
        line=trim(line.substr(0,line.find('#'))); if(line.empty()) continue;
        const auto eq=line.find('='); require(eq!=std::string::npos,"expected key=value");
        const auto key=trim(line.substr(0,eq)), value=trim(line.substr(eq+1));
        require(seen.insert(key).second,"duplicate key: "+key);
        size_t used=0; double v=0;
        try { v=std::stod(value,&used); } catch(...) { throw std::runtime_error("invalid value: "+key); }
        require(used==value.size() && std::isfinite(v),"invalid value: "+key);
        if(doubles.count(key)) *doubles.at(key)=v;
        else if(integers.count(key)||key=="stable_frames") {
            require(v>=0 && v<=3600000 && std::floor(v)==v,"invalid integer: "+key);
            if(key=="stable_frames") c.stableFrames=static_cast<unsigned>(v);
            else *integers.at(key)=static_cast<uint64_t>(v);
        } else throw std::runtime_error("unknown config key: "+key);
    }
    require(file.eof(),"config read failed");
    require(seen.size()==doubles.size()+integers.size()+1,"config must specify every field");
    c.validate(); return c;
}
const char *stateName(State s) {
    switch(s) {case State::Idle:return "idle";case State::Waiting:return "waiting";
    case State::Feeding:return "feeding";
    case State::Tracking:return "tracking";case State::Settling:return "settling";
    case State::Holding:return "holding";case State::Picking:return "picking";
    case State::Fault:return "fault";}
    return "unknown";
}
Controller::Controller(MotorDriver &d,const Config &c):driver_(d),config_(c) {c.validate();}
bool Controller::checkTime(uint64_t now) {
    if(clockSeen_ && now<clock_) {fault("controller clock moved backwards",clock_);return false;}
    clockSeen_=true; clock_=now; return true;
}
void Controller::fault(const std::string &reason,uint64_t now) {
    snapshot_.state=State::Fault; snapshot_.ready=false; snapshot_.duty=0;
    snapshot_.targetSpeed=0;integral_=0;
    snapshot_.reason=reason;
    try {driver_.stop(now);} catch(const std::exception &e) {snapshot_.reason+="; stop: "+std::string(e.what());}
}
void Controller::send(double duty,uint64_t now) {
    try {driver_.command(duty,config_.motorReverse!=0,now); snapshot_.duty=duty;}
    catch(const std::exception &e) {fault(e.what(),now);}
}
void Controller::requestMotion(double ratio,uint64_t now) {
    if(ratio<=0) {snapshot_.targetSpeed=0;integral_=0;stallPending_=false;send(0,now);}
    else if(config_.encoderEnabled)
        snapshot_.targetSpeed=std::max(config_.minSpeed,config_.maxSpeed*std::min(1.0,ratio));
    else send(std::max(config_.minDuty,config_.maxDuty*std::min(1.0,ratio)),now);
}
void Controller::speedTick(uint64_t now) {
    try {
        const auto count=driver_.encoderCount(),errors=driver_.encoderErrors();
        if(!haveEncoder_) {
            encoderCount_=count;encoderErrors_=errors;encoderMs_=now;haveEncoder_=true;
            send(0,now);return;
        }
        if(errors!=encoderErrors_) {fault("encoder invalid AB transition",now);return;}
        if(now==encoderMs_) {send(snapshot_.duty,now);return;}
        const double dt=(now-encoderMs_)/1000.0;
        const uint32_t raw=count-encoderCount_;
        const int64_t delta=raw<=0x7fffffffu?static_cast<int64_t>(raw):
                            static_cast<int64_t>(raw)-4294967296LL;
        const double speed=delta*config_.cmPerCount*config_.encoderSign/dt;
        encoderCount_=count;encoderMs_=now;
        if(!std::isfinite(speed)||std::abs(speed)>config_.maxEncoderSpeed) {
            fault("encoder speed outside configured limit",now);return;
        }
        const double alpha=(dt*1000)/(config_.speedFilterMs+dt*1000);
        snapshot_.measuredSpeed+=alpha*(speed-snapshot_.measuredSpeed);
        if((snapshot_.state==State::Holding || snapshot_.state==State::Picking) &&
           std::abs(snapshot_.measuredSpeed)>config_.stableSpeed) {
            fault("encoder detects held belt movement",now);return;
        }
        if(snapshot_.state==State::Holding || snapshot_.state==State::Picking) {
            const uint32_t rawHeld=count-heldEncoderCount_;
            const int64_t heldDelta=rawHeld<=0x7fffffffu?int64_t(rawHeld):int64_t(rawHeld)-4294967296LL;
            if(std::abs(heldDelta*config_.cmPerCount)>config_.tolerance) {
                fault("encoder held belt displacement exceeded tolerance",now);return;
            }
        }
        if(snapshot_.targetSpeed<=0) {integral_=0;stallPending_=false;send(0,now);return;}
        if(snapshot_.measuredSpeed < -config_.stallMinSpeed) {
            fault("encoder direction disagrees with belt forward",now);return;
        }
        if(snapshot_.measuredSpeed<config_.stallMinSpeed) {
            if(!stallPending_) {stallPending_=true;stallStart_=now;}
            if(now-stallStart_>=config_.stallMs) {fault("encoder stalled or disconnected",now);return;}
        } else stallPending_=false;
        const double error=snapshot_.targetSpeed-snapshot_.measuredSpeed;
        const double candidate=integral_+config_.speedKi*error*dt;
        // Feed-forward + PI; freeze integration while saturation would grow.
        const double ff=config_.minDuty+(config_.maxDuty-config_.minDuty)*
                        snapshot_.targetSpeed/config_.maxSpeed;
        const double unclamped=ff+config_.speedKp*error+candidate;
        if((unclamped>=0 && unclamped<=config_.maxDuty) ||
           (unclamped>config_.maxDuty && error<0) || (unclamped<0 && error>0))
            integral_=candidate;
        const double output=ff+config_.speedKp*error+integral_;
        if(!std::isfinite(output)) {fault("speed controller numeric overflow",now);return;}
        send(std::max(0.0,std::min(config_.maxDuty,output)),now);
    } catch(const std::exception &e) {fault(e.what(),now);}
}
void Controller::arm(uint64_t now) {
    if(!checkTime(now)) return;
    if(snapshot_.state!=State::Idle) throw std::runtime_error("arm requires idle");
    if(driver_.features()&2) {fault("PL is jog-only; automatic conveyor control unavailable",now);return;}
    snapshot_=Snapshot(); snapshot_.state=State::Waiting;
    haveFrame_=false; haveObject_=false; stableCount_=0; started_=now;
    haveEncoder_=false;integral_=0;stallPending_=false;
    send(0,now); // wait for a visible object before moving
}
void Controller::stop(uint64_t now) {
    if(!checkTime(now)) return;
    if(snapshot_.state==State::Fault) {try {driver_.stop(now);} catch(...) {} return;}
    try {driver_.stop(now);snapshot_=Snapshot();haveObject_=false;}
    catch(const std::exception &e) {fault(e.what(),now);}
}
void Controller::feed(uint64_t now) {
    if(!checkTime(now)) return;
    if(snapshot_.state!=State::Waiting) throw std::runtime_error("feed requires waiting");
    snapshot_.state=State::Feeding;started_=now;requestMotion(1,now);
}
void Controller::resetFault(uint64_t now) {
    if(!checkTime(now)) return;
    if(snapshot_.state!=State::Fault && snapshot_.state!=State::Idle)
        throw std::runtime_error("stop before clearing faults");
    try {driver_.clearFault(now);snapshot_=Snapshot();haveObject_=false;}
    catch(const std::exception &e) {fault(e.what(),now);}
}
void Controller::release(uint64_t now) {
    if(!checkTime(now)) return;
    if(snapshot_.state!=State::Holding && snapshot_.state!=State::Picking)
        throw std::runtime_error("release requires holding/picking");
    blockedId_=snapshot_.objectId; blocked_=true;
    snapshot_=Snapshot();snapshot_.state=State::Waiting;
    haveObject_=false;started_=now;stableCount_=0;feed(now);
}
void Controller::beginPick(uint64_t now) {
    if(!checkTime(now)) return;
    if(snapshot_.state!=State::Holding) throw std::runtime_error("pick requires stable holding");
    snapshot_.state=State::Picking;snapshot_.ready=false;started_=now;
    requestMotion(0,now);
}
void Controller::observe(const Observation &o,uint64_t now) {
    if(!checkTime(now)) return;
    if(snapshot_.state==State::Idle || snapshot_.state==State::Fault || snapshot_.state==State::Picking) return;
    // sequence is global for the stream; a detector restart requires stop/arm.
    if(o.captureMs>now || now-o.captureMs>config_.visionTimeoutMs) {
        fault("stale or future visual observation",now);return;
    }
    if(!o.valid) {
        if(!haveObject_ && (snapshot_.state==State::Waiting || snapshot_.state==State::Feeding)) return;
        fault("target not visible",now);return;
    }
    if(haveObject_ && now-lastCapture_>config_.visionTimeoutMs) {
        fault("visual feedback timed out before next observation",now);return;
    }
    if(haveFrame_ && (o.sequence<=lastSequence_ || o.captureMs<=lastCapture_)) return;
    if(!std::isfinite(o.x)||!std::isfinite(o.y)) {fault("invalid visual coordinate",now);return;}
    if(haveObject_ && o.objectId!=snapshot_.objectId) {fault("tracked object changed",now);return;}
    if(blocked_ && o.objectId==blockedId_ && !haveObject_) return;
    const double dx=o.x-config_.originX,dy=o.y-config_.originY;
    const double along=dx*config_.axisX+dy*config_.axisY;
    const double lateral=-dx*config_.axisY+dy*config_.axisX;
    if(!std::isfinite(along)||!std::isfinite(lateral) || lateral<config_.lateralMin ||
       lateral>config_.lateralMax) {fault("object outside belt corridor",now);return;}
    if(haveObject_) snapshot_.velocity=(along-snapshot_.along)*1000.0/(o.captureMs-lastCapture_);
    else {snapshot_.velocity=0; snapshot_.objectId=o.objectId;haveObject_=true;started_=now;}
    haveFrame_=true;lastSequence_=o.sequence;lastCapture_=o.captureMs;
    snapshot_.along=along;snapshot_.lateral=lateral;
    const double error=config_.target-along;
    if(error < -config_.tolerance) {fault("object passed stop tolerance",now);return;}
    if(snapshot_.state==State::Holding) {
        if(std::abs(error)>config_.tolerance || std::abs(snapshot_.velocity)>config_.stableSpeed)
            fault("held object moved",now);
        return;
    }
    if(std::abs(error)<=config_.tolerance) {
        snapshot_.state=State::Settling;requestMotion(0,now);
        if(snapshot_.state==State::Fault) return;
        if(std::abs(snapshot_.velocity)<=config_.stableSpeed &&
           (!config_.encoderEnabled || (haveEncoder_ && std::abs(snapshot_.measuredSpeed)<=config_.stableSpeed))) {
            if(stableCount_==0) stableStart_=o.captureMs;
            ++stableCount_;
            if(stableCount_>=config_.stableFrames && o.captureMs-stableStart_>=config_.stableMs) {
                heldEncoderCount_=encoderCount_;
                snapshot_.state=State::Holding;snapshot_.ready=true;
            }
        } else stableCount_=0;
    } else {
        stableCount_=0;snapshot_.state=State::Tracking;
        const double predictedError=error-std::max(0.0,snapshot_.velocity)*config_.leadMs/1000.0;
        if(predictedError<=config_.tolerance) requestMotion(0,now);
        else requestMotion(predictedError/config_.slowDistance,now);
    }
}
void Controller::tick(uint64_t now) {
    if(!checkTime(now)) return;
    if(snapshot_.state==State::Fault) return;
    try {
        if(driver_.status(now)&FaultMask) {fault("PL motor fault",now);return;}
    } catch(const std::exception &e) {fault(e.what(),now);return;}
    if(snapshot_.state!=State::Idle) {
        if(haveObject_ && snapshot_.state!=State::Picking && now-lastCapture_>config_.visionTimeoutMs) {
            fault("visual feedback timed out",now);return;
        }
        if(snapshot_.state!=State::Holding && now-started_>config_.maxRunMs) {
            fault("cycle/acquisition timed out",now);return;
        }
    }
    if(config_.encoderEnabled) speedTick(now);
    else send(snapshot_.duty,now); // watchdog refreshed only while healthy
}
}
