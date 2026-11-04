#include "arm_control.hpp"
#include "command_lease.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif
using namespace arm;
static Pulses pulse(double value) { Pulses p;p.fill(value);p[0]=std::min(1500.0,value);return p; }
class HardwareMock : public MockBackend { public: bool hardware() const {return true;} };
static void prepare(Controller &c) {assert(c.initialize());assert(c.acknowledgeInitialPulses(pulse(1500),true));assert(c.enable());}
static void beginDebug(Controller &c,const Pulses &baseline) {
    assert(c.enablePulseDebug(baseline,true));
    assert(c.enabled()&&c.pulseDebugEnabled());
}
class LeaseBackend : public MockBackend {
public:
    uint64_t now=0;
    CommandLease lease;
    bool hardware() const { return true; }
    bool requiresRearm() const { return lease.requiresRearm(); }
    bool send(const ChannelBytes &c,bool enabled,std::string &error) {
        if(enabled&&!lease.check(now)) {error="link timed out, PWM held";return false;}
        if(!MockBackend::send(c,enabled,error))return false;
        lease.committed(now,enabled);return true;
    }
    bool keepAlive(std::string &error) {
        if(!lease.check(now)) {error="link timed out, PWM held";return false;}
        if(lease.heartbeatDue(now))return send(records.back().values,true,error);
        error.clear();return true;
    }
};
static void testCommandLease() {
    CommandLease lease;
    lease.committed(100,true);
    assert(!lease.heartbeatDue(299)&&lease.heartbeatDue(300));
    assert(lease.check(849)&&!lease.check(850));
    assert(!lease.check(851)&&lease.requiresRearm());
    lease.committed(900,false);assert(!lease.requiresRearm());
    lease.committed(901,true);assert(!lease.check(900)); // monotonic time required

    LeaseBackend backend;Controller c(backend,ArmConfig::defaults());
    assert(c.initialize());beginDebug(c,pulse(1500));
    const std::size_t initial=backend.records.size();
    backend.now=199;assert(c.tick(199));assert(backend.records.size()==initial);
    for(uint64_t t=200;t<=2000;t+=200) {backend.now=t;assert(c.tick(t));}
    assert(backend.records.size()==initial+10); // idle holds receive heartbeats
    backend.now=2750;const auto sent=backend.records.size();
    assert(!c.tick(2750));assert(c.holdingAfterTimeout()&&!c.enabled()&&!c.moving());
    assert(backend.records.size()==sent&&backend.records.back().enabled);
    assert(!c.enablePulseDebug(pulse(1500),true));
    assert(!c.movePulses(pulse(1600),100,2750));
    assert(!c.stop()&&c.holdingAfterTimeout());
    assert(backend.records.size()==sent); // fault/stop must not emit a disable
    assert(c.disable()&&!c.holdingAfterTimeout());
    assert(!backend.records.back().enabled);
    beginDebug(c,pulse(1500));assert(c.enabled());
    // A new move before tick() must also reject an expired transport lease.
    assert(c.moveDebugPulses(pulse(1600),100,2750));
    backend.now=3500;assert(!c.tick(3500));assert(c.holdingAfterTimeout());
}
static void testPulseDebug() {
    HardwareMock backend;ArmConfig config=ArmConfig::defaults();
    // Full range uses the channel map without changing saved calibration.
    config.axes[0].channel=5;config.axes[5].channel=0;
    Controller c(backend,config);Pulses baseline=pulse(1500);baseline[0]=1000;
    assert(c.initialize());assert(!backend.records.back().enabled);
    assert(!c.enablePulseDebug(true));assert(!c.enabled());
    assert(c.acknowledgeInitialPulses(baseline,true));assert(!c.enablePulseDebug(false));
    assert(!c.initialPoseAcknowledged()&&!c.pulseDebugEnabled());
    beginDebug(c,baseline);
    assert(!c.config().calibrationConfirmed);
    for(const AxisConfig &a:c.config().axes)assert(!a.calibrated);
    for(unsigned i=0;i<AxisCount;++i)assert(c.debugMinPulses()[i]==PlMinUs&&c.debugMaxPulses()[i]==PlMaxUs);
    Pulses target;target.fill(PlMinUs);
    assert(c.moveDebugPulses(target,1000,0));assert(c.tick(500));
    assert(std::fabs(c.currentPulses()[0]-750)<1e-8);
    assert(c.tick(1000));assert(!c.moving());
    for(unsigned i=0;i<AxisCount;++i)assert(backend.records.back().values[i]==0);
    target.fill(PlMaxUs);assert(c.movePulses(target,20,1000));assert(c.tick(1020));
    for(unsigned i=0;i<AxisCount;++i)assert(backend.records.back().values[i]==255);
    Pose angles;angles.fill(0);assert(c.moveAngles(angles,20,1020));assert(c.tick(1040));
    for(unsigned i=0;i<AxisCount;++i)assert(backend.records.back().values[i]==0);
    angles.fill(180);assert(c.moveAngles(angles,20,1040));assert(c.tick(1060));
    for(unsigned i=0;i<AxisCount;++i)assert(backend.records.back().values[i]==255);
    assert(c.calibrateAxis(0,PlMinUs,20,1060,true));assert(c.tick(1080));
    assert(c.stop()&&c.enabled()&&c.pulseDebugEnabled());
    target[0]=499;assert(!c.moveDebugPulses(target,20,1080));
    assert(!c.enabled()&&!c.pulseDebugEnabled()&&!c.initialPoseAcknowledged());
    assert(!backend.records.back().enabled);
    beginDebug(c,baseline);target=baseline;target[0]=2500;assert(!c.movePulses(target,20,0));
    beginDebug(c,baseline);assert(!c.enable());
    beginDebug(c,baseline);assert(!c.moveDebugPulses(baseline,3600001,0));
    beginDebug(c,baseline);target=baseline;target[0]=std::numeric_limits<double>::quiet_NaN();
    assert(!c.moveDebugPulses(target,1000,0));assert(!c.enabled());
    beginDebug(c,baseline);assert(c.moveDebugPulses(baseline,1000,100));
    assert(!c.tick(99));assert(!c.pulseDebugEnabled());
    beginDebug(c,baseline);assert(c.moveDebugPulses(baseline,1000,0));
    backend.failNext=true;assert(!c.tick(20));assert(!c.enabled()&&!backend.records.back().enabled);
    assert(c.acknowledgeInitialPulses(baseline,true));backend.failNext=true;
    assert(!c.enablePulseDebug(true));assert(!c.enabled()&&!c.pulseDebugEnabled());
    // Even a claw baseline above the old limit is explicitly permitted in debug.
    baseline[0]=PlMaxUs;beginDebug(c,baseline);assert(backend.records.back().values[5]==255);
    assert(c.config().axes[0].maxPulseUs==1500&&c.config().axes[0].maxAngleDeg==90);
    assert(!c.config().geometryConfirmed&&!c.config().calibrationConfirmed);
    assert(std::fabs(c.currentAngles()[0]-180)<1e-8);
    assert(c.disable());assert(!c.enabled()&&!c.pulseDebugEnabled()&&!c.initialPoseAcknowledged());
    assert(!c.enablePulseDebug(true));assert(!c.acknowledgeInitialPulses(baseline,true));
    baseline[0]=1000;assert(c.acknowledgeInitialPulses(baseline,true));
    assert(!c.enable()); // Temporary debugging never grants normal-mode calibration.
    assert(!c.enablePulseDebug(baseline,false));
    beginDebug(c,baseline);
    assert(c.disable());
}
int main(int argc,char **argv) {
    std::string error;
    if(argc==3 && std::string(argv[1])=="--write-default")return ArmConfig::defaults().save(argv[2],error)?0:1;
    testPulseDebug();
    testCommandLease();
    uint8_t q=0;
    assert(quantizePulse(500,q,error)&&q==0);assert(quantizePulse(PlMaxUs,q,error)&&q==255);
    assert(quantizePulse(1500,q,error)&&q==128);assert(std::fabs(decodePulse(q)-1503.52)<1e-8);
    assert(!quantizePulse(2500,q,error));assert(!quantizePulse(std::numeric_limits<double>::quiet_NaN(),q,error));
    const ChannelBytes bytes={{1,2,3,4,5,6}};const CommandWords w=encodeCommand(bytes,true,true);
    assert(w.low==0x04030201u&&w.high==0xA6030605u);
    ArmConfig config=ArmConfig::defaults();assert(config.validate(error));
    assert(config.axes[0].maxAngleDeg==90&&config.axes[0].maxPulseUs==1500);
    const Pulses homes=config.homePulses();const double reference[]={770,1500,640,511,1255,1500};
    for(unsigned i=0;i<AxisCount;++i)assert(std::fabs(homes[i]-reference[i])<1e-8);
    config.axes[0].channel=5;config.axes[5].channel=0;config.axes[0].direction=-1;
    Pose angles;angles.fill(90);angles[0]=0;Pulses p;
    assert(config.anglesToPulses(angles,p,error));assert(std::fabs(p[0]-1500)<1e-8);
    assert(std::fabs(config.pulsesToAngles(p)[0])<1e-8);
    MockBackend backend;Controller c(backend,config);prepare(c);
    assert(c.movePulses(p,20,0));assert(c.tick(19));assert(backend.records.size()==2);
    assert(c.tick(20));assert(backend.records.back().values[5]==127);assert(!c.moving());
    assert(decodePulse(backend.records.back().values[5])<=1500);
    p=pulse(1700);assert(c.movePulses(p,100,20));assert(c.tick(40));
    const double held=c.currentPulses()[1];assert(held!=1700);
    const Pulses old=c.currentPulses();assert(c.stop());assert(c.tick(1000));assert(c.currentPulses()==old);assert(c.enabled());
    assert(c.movePulses(pulse(1600),100,1000));assert(c.tick(1020));
    const double replacementStart=c.currentPulses()[1];assert(c.movePulses(pulse(1800),100,1020));assert(c.tick(1040));
    assert(std::fabs(c.currentPulses()[1]-(replacementStart+(1800-replacementStart)*.2))<1e-8);
    assert(c.movePulses(pulse(1650),1,1040));assert(c.tick(1059));assert(c.moving());assert(c.tick(1060));assert(!c.moving());
    assert(c.disable());assert(!c.enabled());assert(!c.initialPoseAcknowledged());assert(!backend.records.back().enabled);
    assert(!c.enable());assert(!c.enabled());
    assert(c.acknowledgeInitialPulses(pulse(1500),true));assert(c.enable());
    p=pulse(1500);p[2]=2500;assert(!c.movePulses(p,20,0));assert(!c.enabled());assert(!backend.records.back().enabled);
    assert(c.acknowledgeInitialPulses(pulse(1500),true));assert(c.enable());
    p=pulse(1500);p[0]=1500.01;assert(!c.movePulses(p,20,0));assert(!c.enabled());
    angles.fill(90);angles[0]=90.01;assert(!config.anglesToPulses(angles,p,error));
    assert(c.acknowledgeInitialPulses(pulse(1500),true));assert(c.enable());
    p=pulse(1500);p[4]=std::numeric_limits<double>::quiet_NaN();assert(!c.movePulses(p,20,0));assert(!c.enabled());
    assert(c.acknowledgeInitialPulses(pulse(1500),true));assert(c.enable());
    assert(c.movePulses(pulse(1600),100,500));assert(!c.tick(499));assert(!c.enabled());
    assert(c.acknowledgeInitialPulses(pulse(1500),true));assert(c.enable());
    assert(c.movePulses(pulse(1600),100,0));backend.failNext=true;assert(!c.tick(20));
    assert(!c.enabled()&&!c.moving()&&!backend.records.back().enabled);
    HardwareMock hard;Controller h(hard,ArmConfig::defaults());assert(h.initialize());
    assert(!h.acknowledgeInitialPulses(pulse(1500),false));assert(!h.calibrateAxis(0,1450,50,0,true));
    assert(h.acknowledgeInitialPulses(pulse(1500),true));assert(!h.enable());
    assert(h.acknowledgeInitialPulses(pulse(1500),true));assert(h.calibrateAxis(0,1450,50,0,true));assert(h.tick(50));
    for(unsigned i=1;i<AxisCount;++i)assert(h.currentPulses()[i]==1500);
    assert(!h.movePulses(pulse(1600),50,50));assert(!h.enabled());
    ArmConfig tight=ArmConfig::defaults();tight.axes[0].minPulseUs=1001;tight.axes[0].maxPulseUs=1999;
    assert(tight.validate(error));MockBackend bounded;Controller b(bounded,tight);prepare(b);
    p=pulse(1500);p[0]=1001;assert(b.movePulses(p,20,0));assert(b.tick(20));
    assert(decodePulse(bounded.records.back().values[0])>=1001);
    p[0]=1999;assert(b.movePulses(p,20,20));assert(b.tick(40));assert(decodePulse(bounded.records.back().values[0])<=1999);
    config.calibrationConfirmed=true;config.geometryConfirmed=true;config.axes[0].calibrated=true;
    const std::string path="test_joints_config.tmp";assert(config.save(path,error));ArmConfig loaded;
    assert(ArmConfig::load(path,loaded,error));assert(loaded.axes[0].direction==-1&&loaded.axes[0].channel==5&&loaded.calibrationConfirmed&&loaded.geometryConfirmed);
    {std::ofstream o(path.c_str(),std::ios::app);o << "axis0.channel=1\n";}assert(!ArmConfig::load(path,loaded,error));
    {std::ofstream o(path.c_str());o << "format_version=1\n";}assert(!ArmConfig::load(path,loaded,error));
    assert(config.save(path,error));{std::ofstream o(path.c_str(),std::ios::app);o << "typo=1\n";}assert(!ArmConfig::load(path,loaded,error));
    assert(config.save(path,error));
    config.cartesianFrame.enabled=true;config.cartesianFrame.debugMapping=true;
    config.cartesianFrame.originCm={{12.5,-3.25,24.125}};config.cartesianFrame.pitchDeg=-21.5;
    assert(config.save(path,error)&&ArmConfig::load(path,loaded,error));
    assert(loaded.cartesianFrame.enabled&&loaded.cartesianFrame.debugMapping);
    assert(loaded.cartesianFrame.originCm==config.cartesianFrame.originCm&&loaded.cartesianFrame.pitchDeg==-21.5);
    // Old complete version-1 configs without an origin block remain supported.
    std::ifstream legacyInput(path.c_str());std::string legacy,line;
    while(std::getline(legacyInput,line))if(line.find("origin_")!=0)legacy+=line+"\n";
    legacyInput.close();{std::ofstream o(path.c_str());o<<legacy;}
    assert(ArmConfig::load(path,loaded,error)&&!loaded.cartesianFrame.enabled);
    {std::ofstream o(path.c_str(),std::ios::app);o<<"origin_enabled=1\n";}
    assert(!ArmConfig::load(path,loaded,error));
    assert(config.save(path,error));
    ArmConfig modified=config;modified.calibrationConfirmed=false;
#ifdef _WIN32
    assert(SetFileAttributesA(path.c_str(),FILE_ATTRIBUTE_READONLY));
    assert(!modified.save(path,error));assert(ArmConfig::load(path,loaded,error));assert(loaded.calibrationConfirmed);
    assert(SetFileAttributesA(path.c_str(),FILE_ATTRIBUTE_NORMAL));
#else
    // Unprivileged Linux save into a nonwritable directory must preserve data.
    if(geteuid()!=0) {
        const std::string dir="test_joints_readonly.tmp";
        assert(mkdir(dir.c_str(),0700)==0);const std::string readonly=dir+"/arm.conf";
        assert(config.save(readonly,error));assert(chmod(dir.c_str(),0500)==0);
        assert(!modified.save(readonly,error));assert(ArmConfig::load(readonly,loaded,error));assert(loaded.calibrationConfirmed);
        assert(chmod(dir.c_str(),0700)==0);std::remove(readonly.c_str());assert(rmdir(dir.c_str())==0);
    }
#endif
    std::remove(path.c_str());
    std::cout << "joint control tests passed\n";
}
