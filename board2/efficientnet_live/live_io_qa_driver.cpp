// Pure IO/gate test driver; no ArmNN, board, camera or worker process access.
#include "live_io.hpp"
#include <iostream>
volatile sig_atomic_t stopped=0;
void stopHandler(int){stopped=1;}
int main(int argc,char**argv){
    signal(SIGTERM,stopHandler);signal(SIGINT,stopHandler);
    liveio::SequenceGate gate;
    for(int i=1;i<argc;++i){
        try{
            auto f=liveio::fetch(liveio::parseUrl(argv[i]),stopped);
            gate.accept(f);
            std::cout<<"OK "<<f.session<<" "<<f.sequence<<" "<<f.width<<" "<<f.height<<std::endl;
        }catch(const std::exception&e){std::cout<<"ERROR "<<e.what()<<std::endl;}
    }
    return 0;
}
