#include "split_runtime.hpp"
#include "live_io.hpp"
#include "preprocess.hpp"
#include "result_deadlines.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <deque>
#include <thread>
#include <sys/stat.h>
#include <limits.h>

static volatile sig_atomic_t stopping=0;
static void stopHandler(int){stopping=1;}
static std::string canonicalDirectory(const std::string& path) {
    if(path.empty()||path[0]!='/')throw std::runtime_error("Output directory must be absolute");
    for(size_t i=1;i<=path.size();++i)if(i==path.size()||path[i]=='/') {
        std::string part=path.substr(0,i);if(part.empty())continue;
        if(mkdir(part.c_str(),0755)!=0&&errno!=EEXIST)throw std::runtime_error("Cannot create output directory");
        struct stat st;if(stat(part.c_str(),&st)!=0||!S_ISDIR(st.st_mode))throw std::runtime_error("Output path is not a directory");
    }
    char resolved[PATH_MAX];if(!realpath(path.c_str(),resolved))throw std::runtime_error("Cannot resolve output directory");return resolved;
}
static void atomicBytes(const std::string& path,const void* bytes,size_t n) {
    const std::string temp=path+".tmp."+std::to_string(getpid());
    int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)throw std::runtime_error("Cannot create atomic output temporary file");
    try {
        const char* p=static_cast<const char*>(bytes);size_t offset=0;
        while(offset<n){ssize_t w=write(fd,p+offset,n-offset);if(w<0&&errno==EINTR)continue;if(w<=0)throw std::runtime_error("Output write failed");offset+=static_cast<size_t>(w);}
        if(close(fd)!=0){fd=-1;throw std::runtime_error("Output close failed");}fd=-1;
        if(rename(temp.c_str(),path.c_str())!=0)throw std::runtime_error("Atomic output rename failed");
    }catch(...){if(fd>=0)close(fd);unlink(temp.c_str());throw;}
}
struct Publisher {
    std::string dir,hash,framePrefix;float threshold;
    uint64_t serial=0;std::deque<std::string> owned;
    Publisher(std::string directory,std::string digest,float t):dir(std::move(directory)),hash(std::move(digest)),threshold(t) {
        framePrefix=dir+"/efflive_"+std::to_string(liveio::monotonicMs())+"_"+std::to_string(getpid())+"_";
    }
    std::string common(const std::string& status,const std::string& message,const liveio::Frame* f) {
        std::ostringstream s;s << std::setprecision(9)
          << "{\"schema\":1,\"status\":" << json(status) << ",\"message\":" << json(message)
          << ",\"published_monotonic_ms\":" << liveio::monotonicMs()
          << ",\"source_session\":" << json(f?f->session:"") << ",\"frame_sequence\":" << json(f?std::to_string(f->sequence):"")
          << ",\"class_names\":[\"empty\",\"object_present\",\"hand_present\"]"
          << ",\"threshold\":" << threshold << ",\"model_id\":\"effnetb0v3\",\"model_sha256\":" << json(hash)
          << ",\"validation_status\":" << json(hash=="unverified"?"unverified":"board_regression_21_ok") << ",\"captured_utc\":" << json(f?f->capturedUtc:"")
          << ",\"source\":" << json(f?f->source:"imx415") << ",\"source_frame_age_ms\":";
        if(f)s << f->ageMs();else s << "null";
        return s.str();
    }
    void publishText(const std::string& s) {atomicBytes(dir+"/latest.json",s.data(),s.size());std::cout << "LIVE_JSON " << s << std::endl;}
    void cleanupOwned(){while(!owned.empty()){unlink(owned.front().c_str());owned.pop_front();}}
    void unavailable(const std::string& status,const std::string& message,const liveio::Frame* f=nullptr) {
        publishText(common(status,message,f)+",\"valid\":false,\"probabilities\":[],\"class_index\":-1,\"confidence\":null,\"state\":\"unavailable\",\"frame_path\":\"\",\"timing_ms\":{\"preprocess\":null,\"cpu\":null,\"npu\":null,\"head\":0,\"total\":null},\"fps\":0}");
    }
    void result(const liveio::Frame& f,const float* probabilities,int prediction,double preprocess,double cpu,double npu,double total,size_t clipped,size_t cma) {
        if(!efficientnet::publishableResult(f.ageMs()))throw liveio::Stale("Inference completed after 15000 ms result deadline");
        const std::string image=dir+"/frame_"+f.session+"_"+std::to_string(f.sequence)+".jpg";
        atomicBytes(image,f.jpeg.data(),f.jpeg.size());
        try {
            if(!efficientnet::publishableResult(f.ageMs()))throw liveio::Stale("Frame exceeded 15000 ms result deadline before publishing");
            const char* names[]={"empty","object_present","hand_present"};
            const bool historical=efficientnet::historicalResult(f.ageMs());
            std::ostringstream s;s << std::setprecision(9) << common("ok",historical?"Historical inference result; capture older than 5 seconds":"",&f)
              << ",\"historical\":" << (historical?"true":"false")
              << ",\"valid\":true,\"probabilities\":[" << probabilities[0] << ',' << probabilities[1] << ',' << probabilities[2] << ']'
              << ",\"class_index\":" << prediction << ",\"confidence\":" << probabilities[prediction]
              << ",\"state\":" << json(probabilities[prediction]>=threshold?names[prediction]:"uncertain")
              << ",\"frame_path\":" << json(image) << ",\"timing_ms\":{\"preprocess\":" << preprocess << ",\"cpu\":" << cpu << ",\"npu\":" << npu << ",\"head\":0,\"total\":" << total << '}'
              << ",\"fps\":" << (total>0?1000.0/total:0) << ",\"clipped_feature_values\":" << clipped << ",\"cma_bytes\":" << cma << '}';
            if(!efficientnet::publishableResult(f.ageMs()))throw liveio::Stale("Frame exceeded 15000 ms result deadline before commit");
            publishText(s.str());
        } catch(...) {unlink(image.c_str());throw;}
        owned.push_back(image);
        // Delete only exact paths created by this worker, never scan/remove user files.
        while(owned.size()>2){unlink(owned.front().c_str());owned.pop_front();}
    }
};
static void backoff(unsigned delayMs) {for(unsigned elapsed=0;elapsed<delayMs&&!stopping;elapsed+=50)std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50u,delayMs-elapsed)));}
int main(int argc,char** argv) {
    std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);
    std::string url="http://172.22.203.1:47003/v1/frame.jpg",modelDir="/opt/efficientnet_b0_split_deploy",outputDir="/tmp/monitor-live",modelHash="unverified";
    unsigned maxFrames=0;float threshold=0.8f;std::unique_ptr<Publisher> publisher;
    try {
        for(int i=1;i<argc;++i) {
            std::string key=argv[i];if(i+1>=argc)throw std::runtime_error("Missing CLI value");std::string value=argv[++i];
            if(key=="--url")url=value;else if(key=="--model-dir")modelDir=value;else if(key=="--output-dir")outputDir=value;
            else if(key=="--model-sha256")modelHash=value;else if(key=="--threshold")threshold=std::stof(value);
            else if(key=="--max-frames"){auto count=liveio::decimal(value);if(count>1000000)throw std::runtime_error("max-frames too large");maxFrames=static_cast<unsigned>(count);}
            else throw std::runtime_error("Unknown argument "+key);
        }
        if(!std::isfinite(threshold)||threshold<0||threshold>1)throw std::runtime_error("Threshold must be between 0 and 1");
        if(modelHash!="unverified"&&(modelHash.size()!=64||!std::all_of(modelHash.begin(),modelHash.end(),[](unsigned char c){return std::isxdigit(c); })))throw std::runtime_error("Model hash must be 64 hex characters");
        const auto endpoint=liveio::parseUrl(url);
        outputDir=canonicalDirectory(outputDir);publisher.reset(new Publisher(outputDir,modelHash,threshold));
        struct sigaction action{};action.sa_handler=stopHandler;sigemptyset(&action.sa_mask);sigaction(SIGTERM,&action,nullptr);sigaction(SIGINT,&action,nullptr);signal(SIGPIPE,SIG_IGN);
        publisher->unavailable("connecting","Loading EfficientNet split models");
        if(std::fesetround(FE_TONEAREST)!=0)throw std::runtime_error("Cannot set ties-to-even rounding");
        armnn::ConfigureLogging(true,false,armnn::LogSeverity::Info);
        CmaZone zone;auto runtime=makeRuntime();
        Model cpu(*runtime,modelDir+"/cpu_front_fp32.onnx",{armnn::BackendId("CpuAcc")},outputDir+"/efficientnet_cpu",false);
        Model tail(*runtime,modelDir+"/tail_uint8_sigmoid256.onnx",{armnn::BackendId("Alnpu"),armnn::BackendId("CpuRef")},outputDir+"/efficientnet_tail",true);
        cpu.enableProfiling(false);tail.enableProfiling(true);
        armnn::ConfigureLogging(true,false,armnn::LogSeverity::Error);
        liveio::SequenceGate gate;unsigned completed=0,tailSamples=0,errors=0;bool tailProfileSaved=false;
        while(!stopping&&(!maxFrames||completed<maxFrames)) {
            liveio::Frame frame;bool fetched=false;std::string failureStatus="connecting";
            try {
                frame=liveio::fetch(endpoint,stopping);fetched=true;gate.accept(frame);
                const auto begin=Clock::now();
                cv::Mat encoded(1,static_cast<int>(frame.jpeg.size()),CV_8UC1,frame.jpeg.data());
                cv::Mat bgr=cv::imdecode(encoded,cv::IMREAD_COLOR);
                if(bgr.empty()||bgr.type()!=CV_8UC3||bgr.cols!=static_cast<int>(frame.width)||bgr.rows!=static_cast<int>(frame.height))throw std::runtime_error("JPEG decode dimensions/type mismatch");
                cv::Mat rgb;cv::cvtColor(bgr,rgb,cv::COLOR_BGR2RGB);
                auto normalized=efficientnet::prepareRgb(rgb.data,rgb.cols,rgb.rows,rgb.step);
                if(normalized.size()!=3*224*224)throw std::runtime_error("Preprocessing output size mismatch");
                std::memcpy(cpu.input->data.virt_addr,normalized.data(),normalized.size()*sizeof(float));
                const double preMs=ms(begin,Clock::now());failureStatus="inference_error";
                if(stopping)break;if(!efficientnet::freshInput(frame.ageMs()))throw liveio::Stale("Frame became stale during preprocessing");
                const size_t cmaBefore=cma_mem_count();const double cpuMs=cpu.run();const size_t cmaAfterCpu=cma_mem_count();
                if(stopping)break;
                const float* features=static_cast<const float*>(cpu.output->data.virt_addr);uint8_t* quantized=static_cast<uint8_t*>(tail.input->data.virt_addr);
                size_t clipped=0;const float scale=tail.in.second.GetQuantizationScale();const int offset=tail.in.second.GetQuantizationOffset();
                for(size_t i=0;i<320*7*7;++i){if(!std::isfinite(features[i]))throw std::runtime_error("CPU feature is not finite");float q=std::nearbyint(features[i]/scale)+float(offset);if(q<0||q>255)++clipped;quantized[i]=static_cast<uint8_t>(std::max(0.0f,std::min(255.0f,q)));}
                if(!efficientnet::publishableResult(frame.ageMs()))throw liveio::Stale("Frame exceeded 15000 ms result deadline after CPU inference");
                const double npuMs=tail.run();++tailSamples;
                if(tailSamples>=2&&!tailProfileSaved){tail.saveProfile();tailProfileSaved=true;}
                if(stopping)break;
                const uint8_t* raw=static_cast<const uint8_t*>(tail.output->data.virt_addr);float logits[3],probabilities[3];
                for(int i=0;i<3;++i)logits[i]=(int(raw[i])-tail.out.second.GetQuantizationOffset())*tail.out.second.GetQuantizationScale();
                float max=*std::max_element(logits,logits+3),sum=0;for(int i=0;i<3;++i){probabilities[i]=std::exp(logits[i]-max);sum+=probabilities[i];}for(float& p:probabilities)p/=sum;
                int prediction=static_cast<int>(std::max_element(logits,logits+3)-logits);
                const double totalMs=ms(begin,Clock::now());const size_t cmaAfterTail=cma_mem_count();
                publisher->result(frame,probabilities,prediction,preMs,cpuMs,npuMs,totalMs,clipped,cmaAfterTail);
                ++completed;errors=0;
                std::cout << "LIVE_FRAME seq=" << frame.sequence << " completed=" << completed << " CMA=" << cmaBefore << ',' << cmaAfterCpu << ',' << cmaAfterTail << std::endl;
            }catch(const liveio::Stale& e) {
                if(stopping)break;publisher->unavailable("source_stale",e.what(),fetched?&frame:nullptr);errors=std::min(errors+1,4u);backoff(std::min(2000u,250u<<(errors-1)));
            }catch(const std::exception& e) {
                if(stopping)break;publisher->unavailable(failureStatus,e.what(),fetched?&frame:nullptr);errors=std::min(errors+1,4u);backoff(std::min(2000u,250u<<(errors-1)));
            }
        }
        if(tailSamples&&!tailProfileSaved)tail.saveProfile();
        publisher->unavailable("stopped",stopping?"Worker stopped by signal":"Requested frame count completed");
        publisher->cleanupOwned();
        std::cout << "LIVE_STOPPED completed=" << completed << " CMA_BYTES=" << cma_mem_count() << std::endl;return 0;
    }catch(const std::exception& e) {
        std::cerr << "LIVE_FATAL " << e.what() << std::endl;
        if(publisher){try{publisher->unavailable(stopping?"stopped":"inference_error",e.what());publisher->cleanupOwned();}catch(...){}}
        return 1;
    }
}
