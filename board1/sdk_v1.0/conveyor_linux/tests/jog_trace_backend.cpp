// Test-only replacement for uio_backend.cpp. No device or memory-map access.
// The production CLI/driver run unchanged and their register traffic is replayed
// through the actual GP0 RTL by test_jog_rtl.py.
#include "conveyor.hpp"
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace conveyor {
namespace {
class TraceRegisters : public Registers {
public:
    TraceRegisters() {
        const char *path=std::getenv("CONVEYOR_TEST_TRACE");
        if(!path) throw std::runtime_error("test trace path required; no hardware backend linked");
        out_.open(path);
        if(!out_) throw std::runtime_error("cannot create test trace");
        const char *mode=std::getenv("CONVEYOR_TEST_FAILURE");
        if(mode) failure_=mode;
    }
    void setNow(uint64_t now) override {
        if(!started_) {epoch_=now;started_=true;}
        if(now<epoch_+now_) throw std::runtime_error("test clock moved backwards");
        now_=now-epoch_;
        if(enabled_ && now_-heartbeat_>=250) {faults_|=0x100;enabled_=false;}
        if(enabled_ && now_-begin_>=1000) {faults_|=0x400;enabled_=false;active_=0;}
        out_<<"T "<<now_<<" 0\n";
    }
    uint32_t read(uint32_t offset) override {
        uint32_t value=0;
        switch(offset) {
        case Id:value=RegisterId;break;
        case Period:value=3750;break;
        case Duty:value=duty_;break;
        case Control:value=control_;break;
        case Watchdog:value=18750000;break;
        case ClockHz:value=75000000;break;
        case Features:value=failure_=="legacy-pl"?2:6;break;
        case EncoderCount:case EncoderErrors:break;
        case ActiveDuty:value=active_;if(enabled_ && failure_=="wrong-duty")value=1;break;
        case Status:
            value=faults_|(reverse_?2u:0u);
            if(enabled_) value|=now_-begin_<1?4u:1u;
            if(enabled_ && failure_=="wrong-direction") value^=2;
            break;
        default:throw std::runtime_error("unexpected test read");
        }
        out_<<"R "<<offset<<" "<<value<<"\n";
        return value;
    }
    void write(uint32_t offset,uint32_t value) override {
        out_<<"W "<<offset<<" "<<value<<"\n";
        if(offset==Duty) {duty_=value;return;}
        if(offset==Control) {control_=value;return;}
        if(offset!=Apply || value!=0xc0de0001u) throw std::runtime_error("invalid test write");
        heartbeat_=now_;
        if(duty_>3750 || control_>7 || ((control_&4) && ((control_&1)||duty_))) {
            faults_|=0x400;enabled_=false;active_=0;
        } else if(control_&4) {faults_=0;enabled_=false;active_=0;}
        else if(!(control_&1)) {enabled_=false;active_=0;}
        else if(!faults_) {
            if(failure_=="dropped-apply") return;
            const bool reverse=(control_&2)!=0;
            if(!enabled_ || reverse_!=reverse) begin_=now_;
            enabled_=duty_!=0;reverse_=reverse;active_=duty_;
        }
    }
private:
    std::ofstream out_;
    std::string failure_;
    uint64_t epoch_=0,now_=0,begin_=0,heartbeat_=0;
    uint32_t duty_=0,control_=0,active_=0,faults_=0;
    bool started_=false,enabled_=false,reverse_=false;
};
}
std::unique_ptr<Registers> openUio(const std::string &) {
    return std::unique_ptr<Registers>(new TraceRegisters());
}
}
