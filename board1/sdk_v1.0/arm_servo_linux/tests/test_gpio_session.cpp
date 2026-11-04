#include "gpio_backend.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace arm;

// Independent, time-driven model of the PL handshake and watchdog. Tests use
// the production GPIOBackend/Controller, replacing only Linux I/O and time.
class Transport : public GPIOTransport {
public:
    uint64_t now=0,acceptedAt=0,applyAt=0;
    unsigned lows=0,highs=0,closes=0;
    unsigned failHighAt=0;
    unsigned accepted=0;
    uint32_t highWord=0;
    uint8_t status=0x21;
    bool level=false,failLow=false,failHigh=false,failRead=false,ignore=false;
    bool open(const std::string &,const std::string &,bool,std::string &e) {
        for(unsigned index=0;index<26;++index)applyHigh(highWord&~(1u<<gpioHighCommandLine(index)));
        e.clear();return true;
    }
    void update() {
        if((status&2)&&!(status&1)&&now-acceptedAt>=1000)status|=1;
        if(applyAt&&now>=applyAt&&!(status&1)) {
            status=(status&~4u)|((status&2)?4:0);applyAt=0;
        }
    }
    bool writeLow(uint32_t,std::string &e) {
        ++lows;if(failLow){e="low write failed";return false;}return true;
    }
    bool writeHigh(uint32_t w,std::string &e) {
        ++highs;if(failHigh||highs==failHighAt){e="high write failed";return false;}
        // Stress the non-atomic GPIO fallback in ascending physical order.
        for(unsigned bit=0;bit<32;++bit) {
            if(bit>=18&&bit<=23)continue;
            applyHigh((highWord&~(1u<<bit))|(w&(1u<<bit)));
        }
        return true;
    }
    void applyHigh(uint32_t w) {
        const bool next=(w>>17)&1,en=(w>>16)&1;
        if(next!=level&&(w>>24)==0xa6&&(!(status&1)||!en)&&!ignore) {
            ++accepted;
            status=0x20|(next?8:0)|(status&4)|(en?2:0);
            acceptedAt=now;applyAt=now+20;
        }
        level=next;highWord=w;
    }
    bool readStatus(uint8_t &word,std::string &e) {
        if(failRead){e="status read failed";return false;}
        update();word=status;return true;
    }
    void close(){++closes;}
    uint64_t nowMs() const {return now;}
    void sleepMs(unsigned ms){now+=ms;update();}
};
static Pulses baseline(){Pulses p;p.fill(1500);return p;}
static void enable(Controller &c) {
    assert(c.disable());assert(c.enablePulseDebug(baseline(),true));
    assert(c.enabled()&&c.hardwareOutputActive());
}
int main() {
    Transport io;
    {
        GPIOBackend backend(io);Controller c(backend,ArmConfig::defaults());
        assert(c.initialize()&&c.holdingAfterTimeout());
        assert(c.hardwareStatusAvailable()&&!c.hardwareOutputActive());
        assert(io.lows==0&&io.highs==0); // Opening must preserve existing PWM.
        assert(!c.enablePulseDebug(baseline(),true));assert(io.highs==0);
        enable(c);assert(!c.holdingAfterTimeout());
        for(unsigned i=0;i<11;++i){io.sleepMs(200);assert(c.tick(io.now));}
        assert(io.level); // Force a falling commit edge during next ownership.
        assert(c.hardwareOutputActive()&&!(io.status&1));
        const unsigned sent=io.highs;
        io.sleepMs(750);assert(!c.tick(io.now));
        assert(c.holdingAfterTimeout()&&!c.enabled()&&io.highs==sent);
        assert(c.hardwareOutputActive());
        io.sleepMs(300);assert(c.tick(io.now));
        assert((io.status&5)==5&&io.highs==sent);
        assert(!c.stop()&&!c.enablePulseDebug(baseline(),true));
        assert(io.highs==sent); // Lock cannot be cleared by stop or a new move.
    }
    const unsigned beforeReconnect=io.highs;
    const unsigned acceptedBeforeReconnect=io.accepted;
    assert(io.closes==1&&(io.status&4)); // Destruction must not disable PWM.
    {
        GPIOBackend backend(io);Controller c(backend,ArmConfig::defaults());
        assert(c.initialize()&&c.hardwareOutputActive()&&c.holdingAfterTimeout());
        assert(io.highs==beforeReconnect);
        assert(io.accepted==acceptedBeforeReconnect); // Per-line ownership changes cannot commit.
        enable(c); // Only this explicit disable/re-enable clears the latch.
        const unsigned sent=io.highs;
        io.failLow=true;
        assert(c.movePulses(baseline(),100,io.now+20));
        io.sleepMs(200);assert(!c.tick(io.now));
        assert(c.holdingAfterTimeout()&&io.highs==sent&&(io.status&4));
        io.failLow=false;enable(c);
        const unsigned highBefore=io.highs;
        io.failHigh=true;io.sleepMs(200);assert(!c.tick(io.now));
        assert(c.holdingAfterTimeout()&&io.highs==highBefore+1&&(io.status&4));
        io.failHigh=false;enable(c);
        const unsigned commitBefore=io.highs;
        io.failHighAt=commitBefore+2;io.sleepMs(200);assert(!c.tick(io.now));
        assert(c.holdingAfterTimeout()&&io.highs==commitBefore+2&&(io.status&4));
        io.failHighAt=0;enable(c);
        io.failRead=true;assert(!c.tick(io.now));
        assert(c.holdingAfterTimeout()&&!c.hardwareStatusAvailable());
        io.failRead=false;enable(c);
        const unsigned invalidBefore=io.highs;
        io.sleepMs(750);Pose invalid;invalid.fill(90);invalid[0]=std::numeric_limits<double>::quiet_NaN();
        assert(!c.moveAngles(invalid,100,io.now));
        assert(c.holdingAfterTimeout()&&io.highs==invalidBefore&&(io.status&4));
        enable(c);
        const unsigned resetBefore=io.highs;
        io.status=0x21;assert(!c.tick(io.now)); // Independent PL reset.
        assert(c.holdingAfterTimeout()&&!c.hardwareOutputActive()&&io.highs==resetBefore);
    }
    {
        Transport old;old.status=0;
        GPIOBackend backend(old);std::string error;
        assert(!backend.initialize(error)&&!backend.statusAvailable());
        assert(old.highs==0&&old.lows==0&&old.closes==1);
    }
    {
        Transport silent;silent.ignore=true;
        GPIOBackend backend(silent);Controller c(backend,ArmConfig::defaults());
        assert(c.initialize());const uint64_t start=silent.now;
        assert(!c.disable()&&c.holdingAfterTimeout());
        assert(silent.now-start>=30&&silent.now-start<=35);
        assert(!c.enablePulseDebug(baseline(),true));
    }
    {
        Transport normal;
        {
            GPIOBackend backend(normal);Controller c(backend,ArmConfig::defaults());
            assert(c.initialize());enable(c);
        }
        assert(normal.highs==4&&(normal.status&4));
    }
    std::cout<<"GPIO session/ack/watchdog/restart/I/O/reset/exit tests passed\n";
}
