#include "conveyor.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
using namespace conveyor;
namespace {
#ifndef _WIN32
class StdinMode {
public:
    StdinMode():flags_(fcntl(STDIN_FILENO,F_GETFL)) {
        if(flags_<0 || fcntl(STDIN_FILENO,F_SETFL,flags_|O_NONBLOCK)<0)
            throw std::runtime_error("cannot set nonblocking stdin");
    }
    ~StdinMode() {(void)fcntl(STDIN_FILENO,F_SETFL,flags_);}
private:
    int flags_;
};
#endif
volatile std::sig_atomic_t stopped=0;
void onSignal(int) {stopped=1;}
uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void print(const Snapshot &s) {
    std::cout<<"state="<<stateName(s.state)<<" id="<<s.objectId<<" along="<<s.along
             <<" lateral="<<s.lateral<<" velocity="<<s.velocity<<" duty="<<s.duty
             <<" target_cm_s="<<s.targetSpeed<<" encoder_cm_s="<<s.measuredSpeed
             <<" ready="<<s.ready<<" reason="<<s.reason<<std::endl;
}
void endOfLine(std::istringstream &line) {
    std::string extra; if(line>>extra) throw std::runtime_error("unexpected argument");
}
uint64_t unsignedValue(const std::string &s) {
    if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("expected unsigned integer");
    size_t used=0;const auto v=std::stoull(s,&used);
    if(used!=s.size()) throw std::runtime_error("invalid integer");
    return v;
}
bool execute(const std::string &text,Controller &controller,MockRegisters *mock) {
    std::istringstream line(text);std::string command;
    if(!(line>>command)) return true;
    const auto now=nowMs();
    if(command=="xy") {
        std::string id,sequence,age;double x,y;
        if(!(line>>id>>sequence>>age>>x>>y))
            throw std::runtime_error("xy OBJECT_ID FRAME_SEQUENCE CAPTURE_AGE_MS X Y");
        endOfLine(line);const auto delay=unsignedValue(age);
        if(delay>now) throw std::runtime_error("capture age is too large");
        Observation o{unsignedValue(id),unsignedValue(sequence),now-delay,x,y,true};
        controller.observe(o,now);
    } else if(command=="lost") {
        endOfLine(line);
        // Invalid observation faults the active cycle without refreshing a frame.
        controller.observe(Observation{0,0,now,0,0,false},now);
    } else {
        endOfLine(line);
        if(command=="arm") controller.arm(now);
        else if(command=="feed") controller.feed(now);
        else if(command=="stop") controller.stop(now);
        else if(command=="release") controller.release(now);
        else if(command=="reset") controller.resetFault(now);
        else if(command=="status") {}
        else if(command=="fault-on" && mock) mock->injectFault(true);
        else if(command=="fault-off" && mock) mock->injectFault(false);
        else if(command=="quit") {controller.stop(now);return false;}
        else throw std::runtime_error("unknown command");
    }
    print(controller.snapshot());return true;
}
// Nonblocking stdin: an unfinished line never blocks the 20 ms heartbeat.
int readInput(char *buffer,unsigned capacity) {
#ifdef _WIN32
    const HANDLE handle=GetStdHandle(STD_INPUT_HANDLE);
    if(GetFileType(handle)==FILE_TYPE_PIPE) {
        DWORD available=0,got=0;
        if(!PeekNamedPipe(handle,nullptr,0,nullptr,&available,nullptr)) return -1;
        if(!available) return 0;
        if(!ReadFile(handle,buffer,(available<capacity?available:capacity),&got,nullptr)) return -1;
        return static_cast<int>(got);
    }
    if(GetFileType(handle)==FILE_TYPE_DISK) {
        DWORD got=0;if(!ReadFile(handle,buffer,capacity,&got,nullptr)||!got) return -1;
        return static_cast<int>(got);
    }
    if(!_kbhit()) return 0;
    int ch=_getch();
    if(ch==3||ch==26) return -1;
    if(ch==0||ch==224) { (void)_getch();return 0; }
    if(ch==13) {std::cout<<std::endl;buffer[0]='\n';return 1;}
    if(ch==8) {std::cout<<"\b \b";buffer[0]='\b';return 1;}
    std::cout<<static_cast<char>(ch)<<std::flush;buffer[0]=static_cast<char>(ch);return 1;
#else
    const auto count=read(STDIN_FILENO,buffer,capacity);
    if(count>0) return static_cast<int>(count);
    if(count==0) return -1;
    if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR) return 0;
    return -1;
#endif
}
int demo(Controller &c,MockRegisters &registers,const Config &cfg) {
    double along=cfg.target-(cfg.encoderEnabled?15:450),velocity=0,encoderPosition=0;
    int64_t lastEncoder=0;uint64_t sequence=0;
    c.arm(0);State last=State::Idle;
    for(uint64_t ms=0;ms<60000;ms+=20) {
        // Synthetic plant, never a measured JGB37-520 model. Coast time 80 ms.
        const double gain=cfg.encoderEnabled?cfg.maxSpeed/cfg.maxDuty*1.3:240;
        const double goal=(registers.read(Status)&1)?registers.read(ActiveDuty)/2500.0*gain:0;
        velocity+=(goal-velocity)*0.25;along+=velocity*0.02;
        if(cfg.encoderEnabled) {
            encoderPosition+=velocity*0.02/cfg.cmPerCount*cfg.encoderSign;
            const auto count=static_cast<int64_t>(std::llround(encoderPosition));
            registers.advanceEncoder(static_cast<int32_t>(count-lastEncoder));lastEncoder=count;
        }
        if(ms%100==0) c.observe(Observation{1,++sequence,ms,
            cfg.originX+along*cfg.axisX,cfg.originY+along*cfg.axisY,true},ms);
        c.tick(ms);
        if(c.snapshot().state!=last) {print(c.snapshot());last=c.snapshot().state;}
        if(last==State::Fault) return 1;
        if(last==State::Holding) {std::cout<<"DEMO PASS (synthetic plant only)"<<std::endl;return 0;}
    }
    std::cerr<<"demo did not converge"<<std::endl;return 1;
}
int selfTest(MotorDriver &driver) {
    if(!(driver.features()&1))throw std::runtime_error("self-test requires BENCH_ONLY=1, physically locked outputs");
    driver.clearFault(nowMs());
    driver.command(.1,false,nowMs());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(driver.status(nowMs())&1)throw std::runtime_error("bench motor output is not locked");
    driver.stop(nowMs());
    const auto initial=driver.encoderCount(),errors=driver.encoderErrors();
    for(unsigned ab:{0u,1u,3u,2u,0u}){driver.benchAB(ab);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    if(driver.encoderCount()-initial!=4u || driver.encoderErrors()!=errors)
        throw std::runtime_error("PL encoder forward self-test failed");
    for(unsigned ab:{2u,3u,1u,0u}){driver.benchAB(ab);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    if(driver.encoderCount()!=initial)throw std::runtime_error("PL encoder reverse self-test failed");
    driver.command(.1,false,nowMs());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if(!(driver.status(nowMs())&0x100))throw std::runtime_error("PL watchdog self-test failed");
    driver.clearFault(nowMs());
    std::cout<<"SELF_TEST_PASS: ID, locked output, AXI commands, AB +/-4, watchdog and explicit clear"<<std::endl;
    return 0;
}
// Explicit bounded single-axis test, independent of camera and robot commands.
// Config max_speed_cm_s is the setpoint; the same PI is used by visual sorting.
int speedTest(Controller &c,MotorDriver &driver,MockRegisters *mock,const Config &cfg,uint64_t duration) {
    const uint64_t begin=nowMs();uint64_t previous=begin;
    double velocity=0,positionCounts=0;int64_t lastCount=0;
    c.arm(begin);
    if(c.snapshot().state==State::Fault) throw std::runtime_error(c.snapshot().reason);
    c.feed(begin);
    std::cout<<"elapsed_ms,target_cm_s,measured_cm_s,duty,count,errors"<<std::endl;
    bool failed=false;
    while(!stopped) {
        const uint64_t now=nowMs();
        if(now-begin>=duration) break;
        if(now-previous>100) {failed=true;std::cerr<<"ERROR: speed-test control gap >100ms"<<std::endl;break;}
        if(mock) {
            const double dt=(now-previous)/1000.0;
            const double goal=(mock->read(Status)&1)?mock->read(ActiveDuty)/2500.0*cfg.maxSpeed/cfg.maxDuty*1.3:0;
            velocity+=(goal-velocity)*dt/(0.08+dt);
            positionCounts+=velocity*dt/cfg.cmPerCount*cfg.encoderSign;
            const auto count=static_cast<int64_t>(std::llround(positionCounts));
            mock->advanceEncoder(static_cast<int32_t>(count-lastCount));lastCount=count;
        }
        previous=now;c.tick(now);
        const auto &s=c.snapshot();
        if(s.state==State::Fault) {failed=true;std::cerr<<"ERROR: "<<s.reason<<std::endl;break;}
        std::cout<<now-begin<<','<<s.targetSpeed<<','<<s.measuredSpeed<<','<<s.duty<<','
                 <<driver.encoderCount()<<','<<driver.encoderErrors()<<std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    c.stop(nowMs());
    const auto status=driver.status(nowMs());
    if((status&(FaultMask|1u)) || c.snapshot().state==State::Fault) failed=true;
    std::cout<<"SPEED_TEST_STOP enabled="<<(status&1u)<<" fault="<<(status&FaultMask)
             <<" simulated="<<(mock?1:0)<<std::endl;
    return failed||stopped?1:0;
}
int encoderJog(MotorDriver &driver,unsigned percent,uint64_t duration) {
    if(driver.features()!=EncoderConnected)
        throw std::runtime_error("encoder jog requires exact external-encoder control profile (features=16)");
    const uint64_t begin=nowMs();uint64_t previous=begin;
    const auto initial=driver.encoderCount(),errors=driver.encoderErrors();
    try {
        while(!stopped) {
            const uint64_t now=nowMs();
            if(now-begin>=duration) break;
            if(now-previous>50) throw std::runtime_error("encoder jog scheduling gap >50ms");
            previous=now;
            if(driver.encoderErrors()!=errors) throw std::runtime_error("encoder invalid AB transition");
            driver.command(percent/100.0,false,now);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch(...) {driver.stop(nowMs());throw;}
    driver.stop(nowMs());
    const auto status=driver.status(nowMs()),finalCount=driver.encoderCount();
    const uint32_t raw=finalCount-initial;
    const int64_t delta=raw<=0x7fffffffu?int64_t(raw):int64_t(raw)-4294967296LL;
    std::cout<<"ENCODER_JOG_STOP enabled="<<(status&1u)<<" fault="<<(status&FaultMask)
             <<" before="<<initial<<" after="<<finalCount<<" delta="<<delta
             <<" errors="<<driver.encoderErrors()<<std::endl;
    if(!delta) std::cerr<<"ERROR: no encoder movement observed; check motion and wiring before closed-loop use"<<std::endl;
    return stopped||!delta||(status&(FaultMask|1u))||driver.encoderErrors()!=errors?1:0;
}
}
int main(int argc,char **argv) {
    try {
        bool simulate=true,runDemo=false,encoderMonitor=false,runSelfTest=false;std::string uio,config;
        uint64_t speedTestMs=0;
        uint64_t encoderJogMs=0;unsigned encoderJogPercent=0;
        for(int i=1;i<argc;++i) {
            const std::string a=argv[i];
            if(a=="--simulate") simulate=true;
            else if(a=="--uio" && i+1<argc) {uio=argv[++i];simulate=false;}
            else if(a=="--config" && i+1<argc) config=argv[++i];
            else if(a=="--demo") runDemo=true;
            else if(a=="--encoder-monitor") encoderMonitor=true;
            else if(a=="--self-test") runSelfTest=true;
            else if(a=="--speed-test-ms" && i+1<argc) {
                speedTestMs=unsignedValue(argv[++i]);
                if(speedTestMs<100 || speedTestMs>5000) throw std::runtime_error("speed test duration must be 100..5000 ms");
            }
            else if(a=="--encoder-jog" && i+2<argc) {
                const auto percent=unsignedValue(argv[++i]);encoderJogMs=unsignedValue(argv[++i]);
                if(percent<1 || percent>75 || encoderJogMs<50 || encoderJogMs>500)
                    throw std::runtime_error("encoder jog: duty 1..75 percent, duration 50..500 ms");
                encoderJogPercent=static_cast<unsigned>(percent);
            }
            else throw std::runtime_error("usage: conveyorctl [--simulate | --uio /dev/uioN] [--config FILE] [--demo | --encoder-monitor | --self-test | --speed-test-ms 100..5000]");
        }
        if(runDemo && !simulate) throw std::runtime_error("--demo requires simulation");
        if(encoderJogMs && (runDemo||encoderMonitor||runSelfTest||speedTestMs))
            throw std::runtime_error("encoder jog is an exclusive mode");
        if(speedTestMs && (runDemo||encoderMonitor||runSelfTest)) throw std::runtime_error("speed test is an exclusive mode");
        if(runDemo && encoderMonitor) throw std::runtime_error("choose demo or encoder monitor");
        if(runSelfTest&&(simulate||runDemo||encoderMonitor))throw std::runtime_error("self-test requires only --uio and --self-test");
        if(!simulate && config.empty() && !encoderMonitor && !runSelfTest && !encoderJogMs) throw std::runtime_error("hardware requires --config FILE");
        const Config cfg=config.empty()?Config():loadConfig(config);
        if(speedTestMs && (!cfg.encoderEnabled || speedTestMs>=cfg.maxRunMs))
            throw std::runtime_error("speed test requires calibrated encoder config and duration < max_run_ms");
        if(!simulate && !encoderMonitor && !runSelfTest && !encoderJogMs && !cfg.encoderEnabled)
            throw std::runtime_error("hardware conveyor requires encoder_enabled=1 and measured cm_per_count");
        std::unique_ptr<Registers> registers=simulate?
            std::unique_ptr<Registers>(new MockRegisters()):openUio(uio);
        MotorDriver driver(*registers);driver.initialize(runDemo?0:nowMs());
        if(runSelfTest)return selfTest(driver);
        std::signal(SIGINT,onSignal);std::signal(SIGTERM,onSignal);
        if(encoderJogMs) return encoderJog(driver,encoderJogPercent,encoderJogMs);
        if(encoderMonitor) {
            std::cout<<"ENCODER MONITOR; motor disabled; turn belt manually; Ctrl+C to exit"<<std::endl;
            std::cout<<"features="<<driver.features()<<" (bit4/mask0x10 means external encoder wired, not calibrated)"<<std::endl;
            uint32_t lastCount=driver.encoderCount();int64_t total=0;
            while(!stopped) {
                driver.stop(nowMs());
                const uint32_t count=driver.encoderCount(),raw=count-lastCount;
                const int64_t delta=raw<=0x7fffffffu?int64_t(raw):int64_t(raw)-4294967296LL;
                total+=delta;lastCount=count;
                std::cout<<"ms="<<nowMs()<<" count="<<count<<" delta="<<delta<<" total="<<total
                         <<" errors="<<driver.encoderErrors()<<std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            return 0;
        }
        Controller controller(driver,cfg);
        auto mock=dynamic_cast<MockRegisters*>(registers.get());
        if(speedTestMs) return speedTest(controller,driver,mock,cfg,speedTestMs);
        if(runDemo) return demo(controller,*mock,cfg);
#ifndef _WIN32
        StdinMode stdinMode;
#endif
        std::cout<<(simulate?"SIMULATION":"UIO HARDWARE")
                 <<"; idle. commands: arm, feed, xy ID SEQ AGE_MS X Y, lost, status, stop, release, reset, quit"<<std::endl;
        std::string pending;bool running=true;State last=State::Idle;
        while(running && !stopped) {
            controller.tick(nowMs());
            if(last!=controller.snapshot().state) {print(controller.snapshot());last=controller.snapshot().state;}
            char bytes[256];const int count=readInput(bytes,sizeof(bytes));
            if(count<0) break;
            for(int i=0;i<count && running;++i) {
                if(bytes[i]=='\b') {if(!pending.empty()) pending.pop_back();continue;}
                if(bytes[i]=='\r') continue;
                if(bytes[i]=='\n') {
                    try {running=execute(pending,controller,mock);}
                    catch(const std::exception &e) {controller.stop(nowMs());std::cerr<<"ERROR: "<<e.what()<<std::endl;}
                    pending.clear();
                } else {
                    pending+=bytes[i];
                    if(pending.size()>4096) throw std::runtime_error("input line too long");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        controller.stop(nowMs());
        return 0;
    } catch(const std::exception &e) {std::cerr<<"ERROR: "<<e.what()<<std::endl;return 1;}
}
