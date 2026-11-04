// Explicit, finite commissioning commands. Never starts from a config file.
#include "conveyor.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace conveyor;
namespace {
volatile std::sig_atomic_t interrupted=0;
void interrupt(int) { interrupted=1; }
uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
unsigned integer(const std::string &text) {
    if(text.empty() || text.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("expected unsigned integer");
    const auto value=std::stoul(text);
    if(value>1000000) throw std::runtime_error("argument out of range");
    return static_cast<unsigned>(value);
}
void status(Registers &r) {
    if(r.read(Id)!=RegisterId) throw std::runtime_error("conveyor PL ID mismatch");
    std::cout<<"ID=0x"<<std::hex<<r.read(Id)<<" features=0x"<<r.read(Features)
             <<" status=0x"<<r.read(Status)<<std::dec
             <<" duty_ticks="<<r.read(ActiveDuty)<<" period="<<r.read(Period)
             <<" clock_hz="<<r.read(ClockHz)<<" watchdog_ticks="<<r.read(Watchdog)
             <<" encoder="<<r.read(EncoderCount)<<" encoder_errors="<<r.read(EncoderErrors)<<std::endl;
}
}
int main(int argc,char **argv) {
    try {
        if(argc<4) throw std::runtime_error("usage: conveyor-jog (--uio /dev/uioN | --simulate test) (--status | --stop | --clear-fault | --jog PERCENT MS forward|reverse)");
        const std::string backend=argv[1], device=argv[2], action=argv[3];
        const bool simulate=backend=="--simulate" && device=="test";
        if(!simulate && backend!="--uio") throw std::runtime_error("explicit --uio or --simulate test required");
        const bool jog=action=="--jog";
        if((jog && argc!=7)||(!jog && argc!=4)) throw std::runtime_error("wrong argument count");
        if(!jog && action!="--status" && action!="--stop" && action!="--clear-fault")
            throw std::runtime_error("unknown action");
        unsigned percent=0,duration=0;bool reverse=false;
        if(jog) {
            percent=integer(argv[4]); duration=integer(argv[5]);
            if(percent<1 || percent>100 || duration<20 || duration>900)
                throw std::runtime_error("jog limits: 1..100 percent, 20..900 ms");
            const std::string direction=argv[6];
            if(direction!="forward" && direction!="reverse") throw std::runtime_error("direction must be forward or reverse");
            reverse=direction=="reverse";
        }
        std::unique_ptr<Registers> registers=simulate?
            std::unique_ptr<Registers>(new MockRegisters()):openUio(device);
        // Status only reads: it never acknowledges faults or modifies motor state.
        status(*registers);
        if(action=="--status") return 0;
        const uint32_t features=registers->read(Features);
        if(jog && !simulate && features!=2 && features!=6)
            throw std::runtime_error("requires JOG_ONLY=1 and BENCH_ONLY=0; no movement sent");
        if(jog && !simulate && features==2 && percent>25)
            throw std::runtime_error("legacy PL limits jog to 25 percent; load full-duty jog bitstream first; no movement sent");
        std::signal(SIGINT,interrupt); std::signal(SIGTERM,interrupt);
        MotorDriver driver(*registers); driver.initialize(nowMs());
        if(action=="--clear-fault") driver.clearFault(nowMs());
        if(jog) {
            if(driver.status(nowMs())&FaultMask) throw std::runtime_error("latched fault; inspect before explicit --clear-fault");
            const uint32_t period=registers->read(Period);
            // Legacy PL still floors its 25% ceiling; full-duty PL accepts PERIOD.
            const uint32_t ceiling=(!simulate && features==2)?period/4:period;
            const uint32_t ticks=std::min<uint32_t>((uint64_t(percent)*period+50)/100,ceiling);
            if(ticks==0) throw std::runtime_error("PWM period too short for requested duty");
            const double duty=static_cast<double>(ticks)/period;
            std::cout<<(simulate?"SIMULATED_JOG":"HARDWARE_JOG")<<" percent="<<percent
                     <<" duty_ticks="<<ticks<<" duration_ms="<<duration<<" direction="<<(reverse?"reverse":"forward")<<std::endl;
            const auto start=nowMs();
            bool observedEnable=false;
            unsigned activeSamples=0;
            uint64_t firstActiveMs=0,lastActiveMs=0;
            while(!interrupted && nowMs()-start<duration) {
                const auto elapsed=nowMs()-start;
                const auto s=driver.status(nowMs());
                if(s&FaultMask) throw std::runtime_error("PL fault during jog");
                if(elapsed>=10) {
                    if(!(s&1) || bool(s&2)!=reverse) throw std::runtime_error("motor enable/direction readback mismatch");
                    if(registers->read(ActiveDuty)!=ticks)
                        throw std::runtime_error("active duty readback mismatch");
                    if(!observedEnable) firstActiveMs=elapsed;
                    lastActiveMs=elapsed;
                    ++activeSamples;
                    observedEnable=true;
                }
                driver.command(duty,reverse,nowMs());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            driver.stop(nowMs());
            if(!observedEnable && !interrupted) throw std::runtime_error("no enabled status observed");
            // Print after disabling: console/serial output must not delay heartbeats.
            // This is register evidence, not an electrical measurement at the pins.
            std::cout<<"REGISTER_READBACK samples="<<activeSamples
                     <<" first_active_ms="<<firstActiveMs<<" last_active_ms="<<lastActiveMs
                     <<" expected_duty_ticks="<<ticks<<" period_ticks="<<period
                     <<" pwm_hz="<<static_cast<double>(registers->read(ClockHz))/period
                     <<" pin_voltage_verified=no"<<std::endl;
        }
        driver.stop(nowMs());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto finalStatus=driver.status(nowMs());
        if((finalStatus&1) || registers->read(ActiveDuty)!=0) throw std::runtime_error("stop readback failed");
        status(*registers);
        std::cout<<"STOP_CONFIRMED"<<std::endl;
        return interrupted?130:0;
    } catch(const std::exception &e) {
        std::cerr<<"ERROR: "<<e.what()<<std::endl;
        return 1;
    }
}
