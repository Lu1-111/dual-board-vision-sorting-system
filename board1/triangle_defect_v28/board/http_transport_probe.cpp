// Hardware-free transport/publisher checks. No model, driver, camera, or OpenCV.
#include "http_publish.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
volatile sig_atomic_t probeStop=0;
void stopProbe(int){probeStop=1;}
int main(int argc,char** argv) {
    ::signal(SIGTERM,stopProbe);
    try {
        if(argc==2) {
            auto f=liveio::fetch(liveio::parseUrl(argv[1]),probeStop);
            liveio::SequenceGate gate;gate.accept(f);
            std::cout<<"FRAME "<<f.session<<' '<<f.sequence<<' '<<f.width<<' '<<f.height<<' '<<f.jpeg.size()<<'\n';
            return 0;
        }
        if(argc!=3||std::string(argv[1])!="--self-test")return 2;
        const std::string directory=argv[2];
        liveio::Frame f;f.requestStartedMs=liveio::monotonicMs();f.session="qa-a";f.sequence=100;
        f.width=1920;f.height=1080;f.source="imx415";f.capturedUtc="2026-11-03T00:00:00Z";f.jpeg={0xff,0xd8,0xff,0xd9};
        liveio::SequenceGate gate;gate.accept(f);
        bool rejected=false;try{gate.accept(f);}catch(const liveio::Stale&){rejected=true;}if(!rejected)return 3;
        f.sequence=99;rejected=false;try{gate.accept(f);}catch(const liveio::Stale&){rejected=true;}if(!rejected)return 4;
        f.session="qa-b";f.sequence=1;gate.accept(f);
        f.session="qa-a";f.sequence=101;rejected=false;try{gate.accept(f);}catch(const liveio::Stale&){rejected=true;}if(!rejected)return 5;
        {
            httplive::Publisher publisher(directory);
            bool locked=false;try{httplive::Publisher other(directory);}catch(const std::exception&){locked=true;}if(!locked)return 6;
            publisher.status("connecting","quote\" slash\\ newline\n");
            const std::string objects="[{\"label\":\"NORMAL\",\"score\":0.003,\"bbox_xyxy\":[1,2,30,40],\"center_xy\":[15.5,21]}]";
            for(unsigned seq=1;seq<=3;++seq){f.sequence=seq;publisher.result(f,objects,1.25,"{}",0.5);}
            if(access((directory+"/frame_qa-a_1.jpg").c_str(),F_OK)==0)return 7;
            for(int seq=2;seq<=3;++seq)if(access((directory+"/frame_qa-a_"+std::to_string(seq)+".jpg").c_str(),F_OK)!=0)return 8;
            std::ifstream stream(directory+"/latest.json");std::string json((std::istreambuf_iterator<char>(stream)),{});
            std::cout<<"RESULT "<<json<<'\n';
            if(json.find("frame_qa-a_3.jpg")==std::string::npos)return 9;
            publisher.status("source_stale","test failure invalidates current fields");
            std::ifstream invalid(directory+"/latest.json");json.assign(std::istreambuf_iterator<char>(invalid),{});
            std::cout<<"INVALID "<<json<<'\n';
            if(json.find("\"frame_path\":\"\"")==std::string::npos||json.find("\"objects\":[]")==std::string::npos)return 10;
            publisher.status("stopped","test completed");
        }
        if(access((directory+"/frame_qa-a_2.jpg").c_str(),F_OK)==0||access((directory+"/frame_qa-a_3.jpg").c_str(),F_OK)==0)return 11;
        std::cout<<"SELF_TEST_PASS\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
