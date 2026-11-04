#include "transport.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <climits>
#include <sys/stat.h>
using namespace LineCan;
static volatile sig_atomic_t stopping=0;
static void stop(int) { stopping=1; }
struct State { unsigned run=1,arm=0,vision=1,color=1,confidence=92,counts[5]={},alarms=0,uptime=0; bool hasUptime=false; };
static bool readState(const std::string &path, State &out) {
    std::ifstream file(path); unsigned long long v[11];
    for(auto &n:v) if(!(file>>n)) return false;
    out.hasUptime=false;
    file >> std::ws;
    if(!file.eof()) {
        unsigned long long uptime;
        if(!(file>>uptime) || uptime>UINT_MAX) return false;
        out.uptime=unsigned(uptime); out.hasUptime=true;
        std::string extra; if(file>>extra) return false;
    }
    if(v[0]>3 || v[1]>2 || v[2]>3 || v[3]>5 || v[4]>100 || (!v[3] && v[4]) || v[10]>7) return false;
    for(int i=5;i<10;++i) if(v[i]>UINT_MAX) return false;
    out.run=v[0]; out.arm=v[1]; out.vision=v[2]; out.color=v[3]; out.confidence=v[4]; out.alarms=v[10];
    for(int i=0;i<5;++i) out.counts[i]=v[i+5];
    return true;
}
static unsigned number(const char *s,unsigned max) {
    char *end=nullptr; errno=0; unsigned long n=std::strtoul(s,&end,10);
    if(errno || !*s || *end || *s=='-' || n>max) { std::fprintf(stderr,"invalid number: %s\n",s); std::exit(2); }
    return unsigned(n);
}
int main(int argc,char **argv) {
    std::string iface,statePath; unsigned port=0,interval=2000,limit=0; bool demo=false; uint32_t session=0;
    for(int i=1;i<argc;++i) {
        std::string a=argv[i];
        if(a=="--demo") demo=true;
        else if(i+1<argc && a=="--can") iface=argv[++i];
        else if(i+1<argc && a=="--udp") port=number(argv[++i],65535);
        else if(i+1<argc && a=="--state") statePath=argv[++i];
        else if(i+1<argc && a=="--interval-ms") interval=number(argv[++i],60000);
        else if(i+1<argc && a=="--ticks") limit=number(argv[++i],UINT_MAX);
        else if(i+1<argc && a=="--session") session=number(argv[++i],UINT_MAX);
        else { std::fprintf(stderr,"Usage: can_sender (--can can0 | --udp PORT) (--demo | --state FILE) [--interval-ms 2000] [--ticks N]\n"); return 2; }
    }
    if((iface.empty()==(port==0)) || demo==!statePath.empty() || interval<20) return 2;
    if(!session) { std::ifstream random("/dev/urandom",std::ios::binary); random.read(reinterpret_cast<char*>(&session),4); if(!session) session=uint32_t(time(nullptr))^uint32_t(getpid()); if(!session) session=1; }
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
    Transport io; State state; uint32_t nextEvent=1,events[3]={}; bool active[3]={}; uint32_t started[3]={}; Frame snapshots[3];
    uint64_t tx=0,acks=0,errors=0; unsigned tick=0; const auto start=std::chrono::steady_clock::now();
    auto lastStateUpdate=start; struct stat previousState={}; bool stateSeen=false;
    bool previousReady=true;
    std::printf("sender session=%u source=%s transport=%s\n",session,demo?"SIMULATED":"state-file",port?"LOCAL-UDP-SIMULATION":"SocketCAN"); std::fflush(stdout);
    while(!stopping && (!limit || tick<limit)) {
        const auto next=std::chrono::steady_clock::now()+std::chrono::milliseconds(interval);
        uint32_t uptime=uint32_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count());
        bool ready=true;
        if(demo) {
            // Scenario time advances by tick for fast tests; uptime remains real monotonic time.
            unsigned p=(tick/2)%60; unsigned scene=p>=10&&p<18?1:p>=26&&p<36?2:p>=44&&p<52?3:0;
            // A visual alarm is not a controller stop/fault signal. Demo belt keeps running.
            // Real --state mode forwards the independent run field without overriding it.
            state.alarms=scene?1u<<(scene-1):0; state.run=1;
            state.color=scene==3?0:scene==1?5:scene==2?1:(tick/4)%4+1;
            state.confidence=state.color?92:0; state.arm=scene<2 && tick%2==0;
            if(scene<2 && tick%2==0) ++state.counts[state.color-1];
        } else {
            struct stat currentState={};
            ready=stat(statePath.c_str(),&currentState)==0 && readState(statePath,state);
            if(ready && (!stateSeen || currentState.st_ino!=previousState.st_ino || currentState.st_mtim.tv_sec!=previousState.st_mtim.tv_sec || currentState.st_mtim.tv_nsec!=previousState.st_mtim.tv_nsec)) {
                previousState=currentState; stateSeen=true; lastStateUpdate=std::chrono::steady_clock::now();
            }
            if(std::chrono::steady_clock::now()-lastStateUpdate>std::chrono::seconds(3)) ready=false;
        }
        if(io.fd()<0 && !(port?io.openUdp(uint16_t(port),false):io.openCan(iface))) {
            if(tick%10==0) std::fprintf(stderr,"open failed: %s\n",io.error().c_str());
            ++errors;
        }
        if(ready && io.fd()>=0) {
            auto send=[&](const Frame &f){
                if(io.send(f)) ++tx; else { ++errors; std::fprintf(stderr,"send failed: %s\n",io.error().c_str()); }
                // BSP TX slot can be overwritten by bursts. Leave room for completion IRQ.
                // This is telemetry (normally 2 cycles/sec), not a high-rate control loop.
                if(!port) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            };
            unsigned activeCount=0;
            for(int i=0;i<3;++i) {
                bool now=state.alarms&(1u<<i); activeCount+=now;
                if(now && !active[i]) { events[i]=nextEvent++; started[i]=uptime; }
                if(events[i] && (now || active[i])) snapshots[i]=alarm(events[i],i+1,now?state.color:(snapshots[i].data[4]&15),now,uint16_t(std::min(uint32_t(65535),uptime-started[i])));
                active[i]=now;
            }
            send(hello(session,demo)); send(heartbeat(uint8_t(tick),state.run,activeCount,state.hasUptime?state.uptime:uptime));
            send(status(uint8_t(tick),state.run,state.arm,state.vision,state.color,state.confidence));
            for(int i=0;i<5;++i) send(counter(uint8_t(tick),i,state.counts[i]));
            // Periodic latest-state snapshots recover a lost activation/clear or receiver restart.
            for(int i=0;i<3;++i) if(events[i]) send(snapshots[i]);
        } else if(!ready && previousReady) std::fprintf(stderr,"state file missing/invalid/stale; heartbeat withheld; waiting for fresh data\n");
        if(ready && !previousReady) std::fprintf(stderr,"fresh state data available; telemetry resumed\n");
        previousReady=ready;
        while(!stopping && std::chrono::steady_clock::now()<next) {
            Frame f; int r=io.fd()<0?0:io.receive(f);
            if(r==1 && valid(f) && f.id==Ack) {
                for(int i=0;i<3;++i) if(events[i] && !std::memcmp(f.data,snapshots[i].data,8)) { ++acks; break; }
            } else if(r==-1) { ++errors; io.close(); break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(tick%10==0 && (demo || limit)) { std::printf("TX=%llu ACK=%llu ERR=%llu tick=%u\n",(unsigned long long)tx,(unsigned long long)acks,(unsigned long long)errors,tick); std::fflush(stdout); }
        ++tick;
    }
    std::printf("STOP TX=%llu ACK=%llu ERR=%llu\n",(unsigned long long)tx,(unsigned long long)acks,(unsigned long long)errors);
    return errors?1:0;
}
