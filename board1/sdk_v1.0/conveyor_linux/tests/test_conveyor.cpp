#include "conveyor.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace conveyor;
namespace {
unsigned checks=0;
void check(bool value,const char *what) {
    ++checks;if(!value) throw std::runtime_error(what);
}
template<typename F> void rejects(F f,const char *what) {
    bool rejected=false;try {f();} catch(const std::exception &) {rejected=true;}
    check(rejected,what);
}
struct Rig {
    MockRegisters registers;
    MotorDriver driver;
    Config cfg;
    Controller control;
    Rig():driver(registers),cfg(),control(driver,cfg) {driver.initialize(0);}
    void begin() {control.arm(0);control.observe(Observation{7,1,0,50,0},0);}
    void faulted(const char *what) {
        check(control.snapshot().state==State::Fault,what);
        check(!(registers.read(Status)&1),"fault must disable motor");
    }
};
void duplicates() {
    Rig r;r.begin();
    for(uint64_t t=20;t<=400;t+=20) {
        r.control.observe(Observation{7,1,t,60,0},t);r.control.tick(t);
    }
    r.control.tick(401);r.faulted("duplicate sequence must not refresh freshness");
}
void badFrames() {
    {Rig r;r.begin();r.control.observe(Observation{8,2,100,60,0},100);r.faulted("ID switch");}
    {Rig r;r.begin();r.control.observe(Observation{7,2,100,60,101},100);r.faulted("lateral corridor");}
    {Rig r;r.begin();r.control.observe(Observation{7,2,100,504,0},100);r.faulted("overshoot");}
    {Rig r;r.begin();r.control.observe(Observation{7,2,100,std::numeric_limits<double>::quiet_NaN(),0},100);r.faulted("NaN");}
    {Rig r;r.begin();r.control.observe(Observation{7,2,101,60,0},100);r.faulted("future timestamp");}
    {Rig r;r.begin();r.control.observe(Observation{7,2,0,60,0},500);r.faulted("stale timestamp");}
    {Rig r;r.begin();r.control.observe(Observation{7,2,500,60,0},500);r.faulted("gap must not be hidden by new frame");}
    {Rig r;r.begin();r.control.observe(Observation{0,0,100,0,0,false},100);r.faulted("lost target notification");}
    {Rig r;r.begin();r.control.tick(100);r.control.tick(99);r.faulted("clock reversal");}
    {Rig r;r.begin();r.control.observe(Observation{7,0,100,60,0},100);
     check(r.control.snapshot().along==50,"out-of-order frame ignored");}
}
void holding() {
    Rig r;r.control.arm(0);
    r.control.observe(Observation{7,1,0,500,0},0);
    check(r.control.snapshot().state==State::Settling,"first at-line observation must settle");
    check(!r.control.snapshot().ready,"one observation is not ready");
    for(uint64_t t=20;t<=400;t+=20) {
        if(t%100==0) r.control.observe(Observation{7,t/100+1,t,500,0},t);
        r.control.tick(t);
    }
    check(r.control.snapshot().state==State::Holding && r.control.snapshot().ready,"stable line must hold");
    check(!(r.registers.read(Status)&1),"holding disables output");
    r.control.release(400);
    r.control.observe(Observation{7,6,500,500,0},500);
    check(r.control.snapshot().state==State::Feeding,"released ID must not retrigger");
    check(r.control.snapshot().duty==r.cfg.maxDuty,"release resumes feeding");
    r.control.tick(500);
    r.control.observe(Observation{8,7,600,50,0},600);
    check(r.control.snapshot().state==State::Tracking,"new ID starts next cycle");
    check(r.control.snapshot().objectId==8,"new object is locked");
    Rig drift;drift.control.arm(0);
    for(uint64_t t=0;t<=400;t+=100) {
        drift.control.observe(Observation{1,t/100+1,t,500,0},t);drift.control.tick(t);
    }
    drift.control.observe(Observation{1,6,500,498,0},500);
    drift.faulted("held object movement invalidates ready");
}
void driverFaults() {
    {Rig r;r.begin();r.control.tick(250);r.faulted("PL watchdog timeout");
     rejects([&]{r.control.arm(250);},"fault cannot auto rearm");
     r.control.resetFault(250);check(r.control.snapshot().state==State::Idle,"clear leaves idle");
     check(!(r.registers.read(Status)&1),"clear must not enable");r.control.arm(250);}
    {Rig r;r.begin();r.registers.injectFault(true);r.control.tick(20);r.faulted("external fault");
     r.control.resetFault(20);r.faulted("active external fault cannot clear");
     r.registers.injectFault(false);r.control.resetFault(20);
     check(r.control.snapshot().state==State::Idle,"released fault can clear explicitly");}
    {Rig r;r.registers.write(Duty,2501);r.registers.write(Control,1);r.registers.write(Apply,0xc0de0001u);
     check((r.registers.read(Status)&0x400)!=0,"invalid PL duty must latch fault");}
    {Rig r;rejects([&]{r.driver.command(-0.1,false,0);},"negative duty");
     rejects([&]{r.driver.command(std::numeric_limits<double>::infinity(),false,0);},"infinite duty");}
    {MockRegisters reg;{MotorDriver d(reg);d.initialize(0);d.command(.2,false,0);}
     check(!(reg.read(Status)&1),"driver destructor attempts disable");}
}
void projectionAndConfig() {
    Config c;c.axisX=0;c.axisY=1;c.originX=10;c.originY=20;
    MockRegisters reg;MotorDriver d(reg);d.initialize(0);Controller ctl(d,c);ctl.arm(0);
    ctl.observe(Observation{1,1,0,10,70},0);
    check(ctl.snapshot().along==50 && ctl.snapshot().lateral==0,"Y axis projection");
    c.axisY=2;rejects([&]{c.validate();},"axis must be normalized");
    const auto loaded=loadConfig("conveyor.example.ini");
    check(loaded.target==500 && loaded.stableFrames==3,"config loads all fields");
    rejects([]{loadConfig("tests/does-not-exist.ini");},"missing configuration");
    {std::ofstream bad("build/bad-config.ini");bad<<"target=500\ntarget=501\n";}
    rejects([]{loadConfig("build/bad-config.ini");},"duplicate configuration");
    {std::ofstream bad("build/bad-config.ini");bad<<"unknown=1\n";}
    rejects([]{loadConfig("build/bad-config.ini");},"unknown configuration");
    {std::ofstream bad("build/bad-config.ini");bad<<"target=nan\n";}
    rejects([]{loadConfig("build/bad-config.ini");},"nonfinite configuration");
    {std::ofstream bad("build/bad-config.ini");bad<<"target=500\n";}
    rejects([]{loadConfig("build/bad-config.ini");},"incomplete configuration");
}
void timeLimitsAndTransport() {
    {Rig r;r.control.arm(0);
     for(uint64_t t=20;t<=15020;t+=20) r.control.tick(t);
     r.faulted("acquisition timeout");}
    {Rig r;r.begin();
     for(uint64_t t=20;t<=15020;t+=20) {
         if(t%100==0) r.control.observe(Observation{7,t/100+1,t,50,0},t);
         r.control.tick(t);
     }
     r.faulted("nonmoving object must hit maximum cycle duration");}
    {struct WrongId : MockRegisters {
        uint32_t read(uint32_t offset) override {return offset==Id?0:MockRegisters::read(offset);}
     } regs;
     MotorDriver d(regs);rejects([&]{d.initialize(0);},"wrong PL version rejected");}
    {struct BrokenBus : MockRegisters {
        bool broken=false;
        void write(uint32_t offset,uint32_t value) override {
            if(broken) throw std::runtime_error("injected MMIO failure");
            MockRegisters::write(offset,value);
        }
     } regs;
     MotorDriver d(regs);d.initialize(0);Controller c(d,Config());c.arm(0);
     c.observe(Observation{7,1,0,50,0},0);regs.broken=true;c.tick(20);
     check(c.snapshot().state==State::Fault && !c.snapshot().ready,"transport failure faults PS");
     check(c.snapshot().reason.find("stop:")!=std::string::npos,"failed stop is reported");
     regs.setNow(300);check(!(regs.read(Status)&1),"PL watchdog stops after bus failure");}
}
void encoderSpeedLoop() {
    Config cfg;cfg.encoderEnabled=1;cfg.cmPerCount=.01;cfg.speedFilterMs=0;
    {MockRegisters regs;MotorDriver d(regs);d.initialize(0);Controller c(d,cfg);
     regs.advanceEncoder(-2);c.arm(0);c.observe(Observation{7,1,0,50,0},0);c.tick(0);
     check(c.snapshot().targetSpeed==cfg.maxSpeed,"vision sets cm/s target");
     regs.advanceEncoder(4);c.tick(20);
     check(std::abs(c.snapshot().measuredSpeed-2)<1e-9,"encoder delta wraps and converts to cm/s");
     check(c.snapshot().duty>0 && c.snapshot().duty<=cfg.maxDuty,"PI PWM is bounded");
     c.stop(20);check(!(regs.read(Status)&1),"speed-loop stop disables");}
    {MockRegisters regs;MotorDriver d(regs);d.initialize(0);Controller c(d,cfg);
     c.arm(0);c.observe(Observation{7,1,0,50,0},0);c.tick(0);
     for(uint64_t t=20;t<=1100;t+=20) {
         if(t%100==0) c.observe(Observation{7,t/100+1,t,50,0},t);
         c.tick(t);
     }
     check(c.snapshot().state==State::Fault && c.snapshot().reason.find("stalled")!=std::string::npos,
           "missing encoder pulses fault on stall timeout");
     check(!(regs.read(Status)&1),"stall disables motor");}
    {MockRegisters regs;MotorDriver d(regs);d.initialize(0);Controller c(d,cfg);
     c.arm(0);c.observe(Observation{7,1,0,50,0},0);c.tick(0);
     regs.advanceEncoder(-4);c.tick(20);
     check(c.snapshot().state==State::Fault,"wrong encoder sign faults");}
    {Config reverse=cfg;reverse.encoderSign=-1;reverse.motorReverse=1;
     MockRegisters regs;MotorDriver d(regs);d.initialize(0);Controller c(d,reverse);
     c.arm(0);c.observe(Observation{7,1,0,50,0},0);c.tick(0);
     regs.advanceEncoder(-4);c.tick(20);
     check(c.snapshot().state==State::Tracking && c.snapshot().measuredSpeed==2,"encoder polarity config");
     check((regs.read(Status)&2)!=0,"BTS motor direction config");}
    {MockRegisters regs;MotorDriver d(regs);d.initialize(0);Controller c(d,cfg);
     c.arm(0);c.observe(Observation{7,1,0,50,0},0);c.tick(0);
     regs.advanceEncoder(0,true);c.tick(20);
     check(c.snapshot().state==State::Fault,"AB transition error faults PS");}
    {MockRegisters regs;MotorDriver d(regs);d.initialize(0);Controller c(d,cfg);
     c.arm(0);c.observe(Observation{7,1,0,50,0},0);c.tick(0);
     regs.advanceEncoder(1000);c.tick(20);
     check(c.snapshot().state==State::Fault,"implausible encoder speed faults");}
    {Config stable=cfg;stable.stableSpeed=.08;
     MockRegisters regs;MotorDriver d(regs);d.initialize(0);Controller c(d,stable);
     c.arm(0);c.tick(0);
     for(uint64_t t=20;t<=600;t+=20) {
         regs.advanceEncoder(2);c.tick(t);
         if(t%100==0) c.observe(Observation{7,t/100+1,t,500,0},t);
     }
     check(!c.snapshot().ready,"moving encoder blocks visually stable ready");
     for(uint64_t t=620;t<=1200;t+=20) {
         c.tick(t);if(t%100==0) c.observe(Observation{7,t/100+1,t,500,0},t);
     }
     check(c.snapshot().ready,"vision and encoder stability permit holding");
     regs.advanceEncoder(2);c.tick(1220);
     check(c.snapshot().state==State::Fault,"encoder detects held belt movement");}
    rejects([]{loadConfig("conveyor.hardware.template.ini");},"unmeasured encoder scale rejected");
}
void explicitFeeding() {
    Rig r;r.control.arm(0);r.control.feed(0);
    check(r.control.snapshot().state==State::Feeding,"explicit feed starts feeding");
    check(r.control.snapshot().duty==r.cfg.maxDuty,"feeding requests cruise");
    r.control.observe(Observation{0,1,100,0,0,false},100);r.control.tick(100);
    check(r.control.snapshot().state==State::Feeding,"empty frame during feed is allowed");
    r.control.observe(Observation{1,2,200,50,0},200);
    check(r.control.snapshot().state==State::Tracking,"feed locks next object");
}
void pickingInterlock() {
    Rig r;r.control.arm(0);
    for(uint64_t t=0;t<=400;t+=100){r.control.observe(Observation{7,t/100+1,t,500,0},t);r.control.tick(t);}
    check(r.control.snapshot().ready,"pick starts only from optical hold");
    r.control.beginPick(400);
    check(r.control.snapshot().state==State::Picking&&!r.control.snapshot().ready,"pick consumes ready and locks belt");
    for(uint64_t t=420;t<=1400;t+=20)r.control.tick(t);
    check(r.control.snapshot().state==State::Picking,"moving arm camera does not trigger visual timeout during pick");
    check(!(r.registers.read(Status)&1),"pick keeps motor disabled");
    r.control.observe(Observation{999,99,1400,999,999},1400);
    check(r.control.snapshot().state==State::Picking,"camera motion ignored while picking");
    r.control.release(1400);check(r.control.snapshot().state==State::Feeding,"pick completion releases next feed");
    rejects([&]{r.control.beginPick(1400);},"cannot pick while feeding");
    Rig timeout;timeout.control.arm(0);
    for(uint64_t t=0;t<=400;t+=100){timeout.control.observe(Observation{7,t/100+1,t,500,0},t);timeout.control.tick(t);}
    timeout.control.beginPick(400);
    for(uint64_t t=420;t<=15420;t+=20)timeout.control.tick(t);
    timeout.faulted("pick maximum duration is bounded");
    Config cfg;cfg.encoderEnabled=1;cfg.cmPerCount=.001;cfg.speedFilterMs=0;
    cfg.target=1;cfg.tolerance=.02;cfg.slowDistance=.2;cfg.stableSpeed=.08;cfg.stableMs=250;
    MockRegisters regs;MotorDriver driver(regs);driver.initialize(0);Controller creep(driver,cfg);
    creep.arm(0);creep.tick(0);
    for(uint64_t t=20;t<=400;t+=20){creep.tick(t);if(t%100==0)creep.observe(Observation{1,t/100,t,1,0},t);}
    check(creep.snapshot().state==State::Holding,"encoder creep test reaches holding");
    creep.beginPick(400);
    for(uint64_t t=420;t<=3400&&creep.snapshot().state!=State::Fault;t+=20){
        if(t%100==0)regs.advanceEncoder(1); // each pulse is below the speed limit
        creep.tick(t);
    }
    check(creep.snapshot().state==State::Fault&&creep.snapshot().reason.find("displacement")!=std::string::npos,
          "slow cumulative creep during picking faults even below stable speed");
    check(!(regs.read(Status)&1),"creep fault keeps belt disabled");
}
}
int main() {
    try {
        for(unsigned feature: {0u,1u,6u,14u,17u,22u}) {
            struct OldRegisters : MockRegisters {
                unsigned feature;
                explicit OldRegisters(unsigned f):feature(f){}
                uint32_t read(uint32_t offset) override {return offset==Features?feature:MockRegisters::read(offset);}
            } regs(feature);
            Config cfg;cfg.encoderEnabled=1;cfg.cmPerCount=.01;
            MotorDriver driver(regs);driver.initialize(0);Controller control(driver,cfg);
            control.arm(0);
            check(control.snapshot().state==State::Fault,"old/bench/jog image cannot arm encoder control");
            check(!(regs.read(Status)&1),"unsupported image stays disabled");
        }
        {struct JogRegisters : MockRegisters {
             uint32_t read(uint32_t offset) override {return offset==Features?2u:MockRegisters::read(offset);}
         } regs;
         MotorDriver driver(regs);driver.initialize(0);Controller control(driver,Config());
         control.arm(0);
         check(control.snapshot().state==State::Fault,"jog-only PL rejects automatic arm");
         check(!(regs.read(Status)&1),"jog-only arm refusal keeps motor disabled");}
        // Inline the ordinary timeout scenario so arm's rejection is asserted.
        {Rig r;check(!(r.registers.read(Status)&1),"initialization must not enable");
         r.control.arm(0);check(r.control.snapshot().state==State::Waiting,"arm waits for coordinates");
         check(r.control.snapshot().duty==0,"waiting must not move");
         r.control.observe(Observation{7,1,0,50,0},0);
         check(r.control.snapshot().duty==r.cfg.maxDuty,"cruise PWM limit");
         for(uint64_t t=20;t<=400;t+=20) r.control.tick(t);
         r.control.tick(401);r.faulted("camera timeout");
         rejects([&]{r.control.arm(401);},"fault requires reset");}
        duplicates();badFrames();holding();driverFaults();projectionAndConfig();timeLimitsAndTransport();encoderSpeedLoop();explicitFeeding();pickingInterlock();
        std::cout<<"PASS "<<checks<<" checks"<<std::endl;return 0;
    } catch(const std::exception &e) {std::cerr<<"FAIL: "<<e.what()<<std::endl;return 1;}
}
