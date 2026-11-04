// Passive on-board check: no transmissions and no production control.
#include "transport.h"
#include <chrono>
#include <thread>
#include <cstdio>
int main() {
    LineCan::Transport io; if(!io.openCan("can0")) return 2;
    unsigned heartbeats=0, mask=0, badStates=0; uint32_t first=0,last=0; bool have=false;
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(65);
    while(std::chrono::steady_clock::now()<end) {
        LineCan::Frame f; int n=io.receive(f);
        if(n==-1) return 3;
        if(n==1 && LineCan::valid(f)) {
            if(f.id==LineCan::Heartbeat) {++heartbeats; if(f.data[2]!=1) ++badStates;}
            if(f.id==LineCan::Alarm && f.data[5]) mask|=1u<<((f.data[4]>>4)-1);
            if(f.id==LineCan::CountBase+4) { last=LineCan::u32(f.data+4); if(!have) {first=last;have=true;} }
        } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::printf("HEARTBEATS=%u NON_RUNNING=%u ALARM_TYPES_MASK=%u DEFECT_COUNT=%u->%u\n",heartbeats,badStates,mask,first,last);
    return heartbeats>80 && badStates==0 && mask==7 && have && last>first?0:1;
}
