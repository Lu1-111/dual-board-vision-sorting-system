#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <condition_variable>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "buffered_executor.hpp"
#include "indexed_executor.hpp"
#include "flow_common_types.hpp"
#include "driver_camera.hpp"
#include "framebuffer_display.hpp"
#include "fast_tracker.hpp"
#include "gray_resize.hpp"
#include "http_publish.hpp"

namespace {
constexpr float threshold = 0.88f;
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
using Clock = std::chrono::steady_clock;
struct Options {
    std::string model, features, image, display, output, latest, preprocess;
    std::string httpSource,httpOutputDir="/tmp/triangle-live";
    std::string camera = "/dev/mipicam";
    int frames = 0;
    int inferIntervalMs = 333;
    int videoFps = 60;
    int trackWidth = 320;
    bool directDisplay = true;
    bool crop = false;
    bool check = false;
    bool quiet = false;
    bool explicitCamera = false;
};
Options parse(int argc, char** argv) {
    Options o;
    for (int i=1; i<argc; ++i) {
        std::string a=argv[i];
        if (a=="--help") {
            std::cout << "triangle_defect_npu --features FP32.onnx --model HEAD_UINT8.onnx [--image FILE | --camera /dev/mipicam] "
                "[--display /dev/fb0] [--frames N] [--annotated FILE] [--latest-json FILE] [--crop-only] "
                "[--infer-interval-ms 333] [--video-fps 60 (0=unlimited)] [--track-width 320] [--copy-display]\n"
                "Preprocess check: --image FILE --preprocess FILE [--crop-only]\n";
            std::cout << "Shared camera: --http-source http://IPv4:port/v1/frame.jpg [--http-output-dir /tmp/triangle-live]\n";
            std::exit(0);
        }
        if (a=="--crop-only") { o.crop=true; continue; }
        if (a=="--check-model") { o.check=true; continue; }
        if (a=="--quiet") { o.quiet=true; continue; }
        if (a=="--direct-display") { o.directDisplay=true; continue; }
        if (a=="--copy-display") { o.directDisplay=false; continue; }
        if (++i>=argc) throw std::runtime_error("missing value for "+a);
        std::string v=argv[i];
        if(a=="--model")o.model=v; else if(a=="--image")o.image=v;
        else if(a=="--features")o.features=v;
        else if(a=="--camera"){o.camera=v;o.explicitCamera=true;} else if(a=="--display")o.display=v;
        else if(a=="--http-source")o.httpSource=v;
        else if(a=="--http-output-dir")o.httpOutputDir=v;
        else if(a=="--annotated")o.output=v; else if(a=="--latest-json")o.latest=v;
        else if(a=="--preprocess")o.preprocess=v; else if(a=="--frames")o.frames=std::stoi(v);
        else if(a=="--infer-interval-ms")o.inferIntervalMs=std::stoi(v);
        else if(a=="--video-fps")o.videoFps=std::stoi(v);
        else if(a=="--track-width")o.trackWidth=std::stoi(v);
        else throw std::runtime_error("unknown option "+a);
    }
    if(o.frames<0)throw std::runtime_error("frames must be nonnegative");
    if(o.inferIntervalMs<1)throw std::runtime_error("infer-interval-ms must be positive");
    if(o.videoFps<0||o.videoFps>240)throw std::runtime_error("video-fps must be between 0 and 240");
    if(o.trackWidth<160||o.trackWidth>640)throw std::runtime_error("track-width must be between 160 and 640");
    if(o.model.empty()&&o.preprocess.empty())throw std::runtime_error("--model is required");
    if(o.features.empty()&&!o.check&&o.preprocess.empty())throw std::runtime_error("--features is required");
    if(o.crop&&o.image.empty())throw std::runtime_error("--crop-only requires --image");
    if(!o.httpSource.empty()) {
        liveio::parseUrl(o.httpSource);
        if(o.explicitCamera||!o.display.empty()||!o.image.empty()||!o.output.empty()||!o.latest.empty()||!o.preprocess.empty()||o.check||o.crop)
            throw std::runtime_error("HTTP mode cannot use camera, display, image, crop, annotated, latest-json, preprocess or check-model options");
    }
    return o;
}
struct Object { cv::Rect box, crop; bool partial=false; float score=0; bool judged=false; uint64_t trackId=0; };
struct Timing {
    double locateMs=0, preprocessMs=0, featuresMs=0, bridgeMs=0, headMs=0;
};
std::vector<Object> locate(const cv::Mat& bgr) {
    cv::Mat hsv,mask;
    cv::cvtColor(bgr,hsv,cv::COLOR_BGR2HSV);
    cv::inRange(hsv,cv::Scalar(100,80,30),cv::Scalar(135,255,255),mask);
    cv::morphologyEx(mask,mask,cv::MORPH_CLOSE,cv::Mat::ones(3,3,CV_8U));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<Object> out;
    for(const auto& c:contours) {
        double area=cv::contourArea(c); cv::Rect b=cv::boundingRect(c);
        if(area<std::max(64.,bgr.total()*.001)||std::min(b.width,b.height)<24)continue;
        std::vector<cv::Point> hull;cv::convexHull(c,hull);
        double aspect=double(b.width)/b.height;
        if(aspect<.45||aspect>2.2||area/std::max(cv::contourArea(hull),1.)<.8||area/b.area()<.5)continue;
        int p=int(std::nearbyint(std::max(b.width,b.height)*.15));
        Object obj;obj.box=b;
        obj.crop=cv::Rect(b.x-p,b.y-p,b.width+2*p,b.height+2*p)&cv::Rect(0,0,bgr.cols,bgr.rows);
        obj.partial=b.x<=1||b.y<=1||b.br().x>=bgr.cols-1||b.br().y>=bgr.rows-1;
        out.push_back(obj);
    }
    std::sort(out.begin(),out.end(),[](const Object&a,const Object&b){
        return a.box.x+a.box.width*.5<b.box.x+b.box.width*.5;
    });
    return out;
}
std::vector<float> input(const cv::Mat& bgr, bool fullframe) {
    // Pillow's RGB -> L integer coefficients, then its antialiased BILINEAR resize.
    std::vector<uint8_t> g(bgr.total());
    for(int y=0;y<bgr.rows;++y)for(int x=0;x<bgr.cols;++x){
        auto p=bgr.at<cv::Vec3b>(y,x);
        g[y*bgr.cols+x]=uint8_t((19595*int(p[2])+38470*int(p[1])+7471*int(p[0])+32768)>>16);
    }
    int w=bgr.cols,h=bgr.rows;
    if(fullframe){g=grayresize::resize(g.data(),w,h,w,224,224);w=h=224;}
    g=grayresize::resize(g.data(),w,h,w,320,320);
    const float mean[]={.485f,.456f,.406f},stdev[]={.229f,.224f,.225f};
    std::vector<float> values(3*320*320);
    for(int c=0;c<3;++c)for(int i=0;i<320*320;++i)
        values[c*320*320+i]=(g[i]/255.f-mean[c])/stdev[c];
    return values;
}
float infer(IndexedExecutor& features,IndexedExecutor& exec,const std::vector<float>& values,Timing* timing=nullptr) {
    auto stageStart=timing?Clock::now():Clock::time_point{};
    auto &ib=features.GetInputBuffer(0,0);
    auto info=features.GetInputTensor(0,0).GetInfo();
    if(info.GetNumElements()!=values.size())throw std::runtime_error("input size mismatch");
    if(info.GetDataType()==armnn::DataType::Float32)
        std::memcpy(ib->virt_addr,values.data(),values.size()*sizeof(float));
    else if(info.GetDataType()==armnn::DataType::QAsymmU8) {
        auto* dest=static_cast<uint8_t*>(ib->virt_addr);float s=info.GetQuantizationScale();int z=info.GetQuantizationOffset();
        if(!(s>0))throw std::runtime_error("invalid input scale");
        for(size_t i=0;i<values.size();++i)dest[i]=uint8_t(std::max(0,std::min(255,int(std::nearbyint(values[i]/s))+z)));
    } else throw std::runtime_error("unsupported input type");
    ib.sync_device();if(!features.Run(0))throw std::runtime_error("feature extraction failed");
    auto &fb=features.GetOutputBuffer(0,0);fb.sync_cpu();
    if(timing){
        const auto now=Clock::now();
        // Includes feature input upload, execution, and output synchronization.
        timing->featuresMs+=std::chrono::duration<double,std::milli>(now-stageStart).count();stageStart=now;
    }
    const auto fi=features.GetOutputTensors(0)[0].second.GetInfo();
    const auto hi=exec.GetInputTensor(0,0).GetInfo();
    if(fi.GetDataType()!=armnn::DataType::Float32||fi.GetNumElements()!=115200||
       hi.GetDataType()!=armnn::DataType::QAsymmU8||hi.GetNumElements()!=115200||!(hi.GetQuantizationScale()>0))
        throw std::runtime_error("feature/head bridge contract mismatch");
    const auto *f=static_cast<const float*>(fb->virt_addr);
    auto &hb=exec.GetInputBuffer(0,0);auto *h=static_cast<uint8_t*>(hb->virt_addr);
    for(unsigned i=0;i<hi.GetNumElements();++i)
        h[i]=uint8_t(std::max(0,std::min(255,int(std::nearbyint(f[i]/hi.GetQuantizationScale()))+hi.GetQuantizationOffset())));
    hb.sync_device();
    if(timing){
        const auto now=Clock::now();
        timing->bridgeMs+=std::chrono::duration<double,std::milli>(now-stageStart).count();stageStart=now;
    }
    if(!exec.Run(0))throw std::runtime_error("NPU head inference failed");
    auto &ob=exec.GetOutputBuffer(0,0);ob.sync_cpu();
    const auto oi=exec.GetOutputTensors(0)[0].second.GetInfo();
    if(oi.GetNumElements()!=1600)throw std::runtime_error("expected 40x40 logits");
    std::vector<float> logits(1600);
    if(oi.GetDataType()==armnn::DataType::QAsymmU8){
        auto*p=static_cast<const uint8_t*>(ob->virt_addr);
        for(size_t i=0;i<logits.size();++i)logits[i]=(int(p[i])-oi.GetQuantizationOffset())*oi.GetQuantizationScale();
    }else if(oi.GetDataType()==armnn::DataType::Float32)
        std::memcpy(logits.data(),ob->virt_addr,logits.size()*sizeof(float));
    else throw std::runtime_error("unsupported output type");
    std::partial_sort(logits.begin(),logits.begin()+4,logits.end(),std::greater<float>());
    const float score=1.f/(1.f+std::exp(-(logits[0]+logits[1]+logits[2]+logits[3])*.25f));
    // Head time includes output synchronization, dequantization, and score reduction.
    if(timing)timing->headMs+=std::chrono::duration<double,std::milli>(Clock::now()-stageStart).count();
    return score;
}
void process(cv::Mat bgr,IndexedExecutor& features,IndexedExecutor& exec,FramebufferDisplay& display,const Options&o,unsigned long long frame) {
    auto start=Clock::now();
    auto objects=locate(bgr);
    if(o.crop){objects.clear();Object object;object.box=object.crop=cv::Rect(0,0,bgr.cols,bgr.rows);objects.push_back(object);}
    for(auto& obj:objects)if(!obj.partial){obj.score=infer(features,exec,input(bgr(obj.crop),!o.crop));obj.judged=true;}
    double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    std::ostringstream json;
    json<<std::setprecision(9)<<"{\"model\":\"V28\",\"threshold\":0.88,\"frame\":"<<frame<<",\"width\":"<<bgr.cols
        <<",\"height\":"<<bgr.rows<<",\"processing_ms\":"<<elapsed<<",\"objects\":[";
    for(size_t i=0;i<objects.size();++i){
        const auto&obj=objects[i];const auto&b=obj.box;
        std::string label=!obj.judged?"REVIEW":obj.score>=threshold?"DEFECT":"NORMAL";
        cv::Scalar color=!obj.judged?cv::Scalar(0,190,255):obj.score>=threshold?cv::Scalar(40,40,240):cv::Scalar(50,215,70);
        cv::rectangle(bgr,b,color,3);cv::Point center(b.x+b.width/2,b.y+b.height/2);
        cv::drawMarker(bgr,center,color,cv::MARKER_CROSS,18,2);
        std::ostringstream text;text<<label;if(obj.judged)text<<" "<<std::fixed<<std::setprecision(3)<<obj.score;
        cv::putText(bgr,text.str(),cv::Point(b.x,std::max(40,b.y-12)),cv::FONT_HERSHEY_SIMPLEX,.9,color,2);
        if(i)json<<',';
        json<<"{\"label\":\""<<label<<"\",\"score\":";
        if(obj.judged)json<<obj.score;else json<<"null";
        json<<",\"bbox_xyxy\":["<<b.x<<','<<b.y<<','<<b.br().x<<','<<b.br().y<<"],\"center_xy\":["<<b.x+b.width*.5<<','<<b.y+b.height*.5<<"]}";
    }
    json<<"]}";
    std::string banner="V28 TRIANGLE DEFECT | "+std::to_string(objects.size())+" BLUE OBJECTS | "+std::to_string(int(elapsed))+" ms";
    cv::rectangle(bgr,cv::Rect(0,0,bgr.cols,std::min(48,bgr.rows)),cv::Scalar(30,25,20),cv::FILLED);
    cv::putText(bgr,banner,cv::Point(12,32),cv::FONT_HERSHEY_SIMPLEX,.7,cv::Scalar(240,240,240),2);
    display.show(bgr);
    if(!o.output.empty()&&!cv::imwrite(o.output,bgr))throw std::runtime_error("cannot save image");
    if(!o.latest.empty()){
        std::string temp=o.latest+".tmp";std::ofstream out(temp);out<<json.str()<<'\n';out.close();
        if(!out||std::rename(temp.c_str(),o.latest.c_str()))throw std::runtime_error("cannot save status");
    }
    if(!o.quiet)std::cout<<json.str()<<std::endl;
}

// HTTP consumes the existing camera owner's unannotated full frame. The same
// locate/input/infer functions and 0.88 threshold are used without ROI changes.
// There is no camera or framebuffer object in this branch and no inference queue.
void httpCamera(IndexedExecutor& features,IndexedExecutor& exec,const Options& o,httplive::Publisher& publisher) {
    const auto url=liveio::parseUrl(o.httpSource);liveio::SequenceGate gate;
    uint64_t completed=0;int64_t lastUnique=liveio::monotonicMs(),lastFinished=0;
    const auto delay=[](int milliseconds){
        const auto until=Clock::now()+std::chrono::milliseconds(milliseconds);
        while(!stopped&&Clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(25));
    };
    while(!stopped&&(o.frames==0||completed<static_cast<uint64_t>(o.frames))) {
        const auto cycleStart=Clock::now();
        bool inferenceStarted=false;
        try {
            liveio::Frame frame=liveio::fetch(url,stopped);
            if(frame.session==gate.session&&frame.sequence==gate.sequence) {
                if(liveio::monotonicMs()-lastUnique>5000)publisher.status("source_stale","No new camera frame for 5 seconds");
                delay(200);continue;
            }
            gate.accept(frame);lastUnique=liveio::monotonicMs();
            const auto started=Clock::now();
            cv::Mat bgr=cv::imdecode(frame.jpeg,cv::IMREAD_COLOR);
            if(bgr.empty()||bgr.cols!=int(frame.width)||bgr.rows!=int(frame.height)||bgr.type()!=CV_8UC3)
                throw std::runtime_error("Camera JPEG decode or dimensions invalid");
            if(frame.ageMs()>5000)throw liveio::Stale("Camera frame too old before inference");
            Timing timing;const auto locating=Clock::now();
            auto objects=locate(bgr);
            timing.locateMs=std::chrono::duration<double,std::milli>(Clock::now()-locating).count();
            if(objects.size()>128)throw std::runtime_error("Too many objects for bounded HTTP inference");
            for(auto& obj:objects)if(!obj.partial) {
                if(stopped)break;
                if(frame.ageMs()>30000)throw liveio::Stale("Inference frame exceeded 30 seconds");
                const auto preparing=Clock::now();
                auto values=input(bgr(obj.crop),true);
                timing.preprocessMs+=std::chrono::duration<double,std::milli>(Clock::now()-preparing).count();
                inferenceStarted=true;obj.score=infer(features,exec,values,&timing);
                if(!std::isfinite(obj.score)||obj.score<0||obj.score>1)throw std::runtime_error("Invalid V28 model probability");
                obj.judged=true;
            }
            if(stopped)break;
            const double processing=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
            std::ostringstream rows;rows<<std::setprecision(9)<<'[';
            for(size_t i=0;i<objects.size();++i) {
                const auto& obj=objects[i];const auto& b=obj.box;
                const char* label=!obj.judged?"REVIEW":obj.score>=threshold?"DEFECT":"NORMAL";
                if(i)rows<<',';
                rows<<"{\"label\":\""<<label<<"\",\"score\":";
                if(obj.judged)rows<<obj.score;else rows<<"null";
                rows<<",\"bbox_xyxy\":["<<b.x<<','<<b.y<<','<<b.br().x<<','<<b.br().y
                    <<"],\"center_xy\":["<<b.x+b.width*.5<<','<<b.y+b.height*.5<<"]}";
            }
            rows<<']';
            std::ostringstream times;times<<std::setprecision(12)<<"{\"locate\":"<<timing.locateMs
                <<",\"preprocess\":"<<timing.preprocessMs<<",\"cpu\":"<<timing.featuresMs
                <<",\"bridge\":"<<timing.bridgeMs<<",\"npu_head\":"<<timing.headMs<<'}';
            const int64_t finished=liveio::monotonicMs();
            const double fps=lastFinished>0&&finished>lastFinished?1000.0/(finished-lastFinished):0;
            publisher.result(frame,rows.str(),processing,times.str(),fps);lastFinished=finished;++completed;
            if(!o.quiet)std::cout<<"HTTP_RESULT session="<<frame.session<<" sequence="<<frame.sequence<<" objects="<<objects.size()
                <<" processing_ms="<<processing<<" source_age_ms="<<frame.ageMs()<<std::endl;
        } catch(const liveio::Stale& error) {
            if(stopped)break;
            publisher.status("source_stale",error.what());delay(500);
        } catch(const std::exception& error) {
            if(stopped)break;
            publisher.status(inferenceStarted?"inference_error":"connecting",error.what());
            // Do not repeatedly reuse an NPU runtime after an inference failure.
            if(inferenceStarted)throw;
            delay(500);
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-cycleStart).count();
        if(elapsed<o.inferIntervalMs)delay(o.inferIntervalMs-int(elapsed));
    }
    publisher.status("stopped",stopped?"V28 HTTP inference stopped":"Requested HTTP frames completed");
}

struct LiveResult {
    std::vector<Object> objects;
    unsigned long long frame=0;
    Clock::time_point captured;
    double processingMs=0, inferenceFps=0;
    Timing timing;
};
struct LiveTask {
    // Owned RAM, never the camera DMA buffer or the framebuffer mapping.
    cv::Mat image;
    unsigned long long frame=0;
    Clock::time_point captured;
    double videoFps=0;
    std::vector<triangle_tracking::Track> tracks;
};
struct LiveState {
    std::mutex mutex;
    std::condition_variable ready;
    bool quit=false, pending=false, busy=false;
    LiveTask task;
    std::shared_ptr<const LiveResult> result;
    std::exception_ptr error;
};
struct Classification {
    float score=0;
    unsigned long long frame=0;
    Clock::time_point captured;
};
struct LiveObject {
    triangle_tracking::Track track;
    bool judged=false;
    Classification classification;
};
void associateSnapshotTracks(std::vector<Object>& objects,const std::vector<triangle_tracking::Track>& tracks) {
    // Match only geometry from the same captured frame. Require a unique strong
    // overlap in both directions; unresolved/merged detections remain unassigned.
    auto overlaps=[](const cv::Rect& a,const cv::Rect& b){
        const double intersection=(a&b).area();
        const double combined=double(a.area())+b.area()-intersection;
        return combined>0&&intersection/combined>=.5;
    };
    for(auto& obj:objects){
        const triangle_tracking::Track* candidate=nullptr;
        int matches=0;
        for(const auto& track:tracks)if(!track.partial&&!track.ambiguous&&overlaps(obj.box,track.box)){
            candidate=&track;++matches;
        }
        if(matches!=1)continue;
        int reverseMatches=0;
        for(const auto& other:objects)if(overlaps(other.box,candidate->box))++reverseMatches;
        if(reverseMatches==1)obj.trackId=candidate->id;
    }
}
std::vector<LiveObject> currentObjects(const std::vector<triangle_tracking::Track>& tracks,
                                      const LiveResult* update,std::map<uint64_t,Classification>& scores) {
    auto active=[&](uint64_t id){
        return std::find_if(tracks.begin(),tracks.end(),[&](const triangle_tracking::Track& track){
            return track.id==id&&!track.partial&&!track.ambiguous;
        })!=tracks.end();
    };
    for(auto i=scores.begin();i!=scores.end();){
        if(!active(i->first))i=scores.erase(i);else ++i;
    }
    if(update)for(const auto& obj:update->objects)if(obj.trackId&&obj.judged&&active(obj.trackId)){
        auto i=scores.find(obj.trackId);
        if(i==scores.end()||i->second.frame<=update->frame)
            scores[obj.trackId]=Classification{obj.score,update->frame,update->captured};
    }
    std::vector<LiveObject> objects;
    objects.reserve(tracks.size());
    for(const auto& track:tracks){
        LiveObject obj;obj.track=track;
        auto i=scores.find(track.id);
        if(i!=scores.end()&&!track.partial&&!track.ambiguous){obj.judged=true;obj.classification=i->second;}
        objects.push_back(obj);
    }
    return objects;
}
const char* label(const Object& obj) {
    return !obj.judged?"REVIEW":obj.score>=threshold?"DEFECT":"NORMAL";
}
double resultAgeMs(const LiveResult& result,Clock::time_point now) {
    return std::chrono::duration<double,std::milli>(now-result.captured).count();
}
void drawInferenceSnapshot(cv::Mat& bgr,const LiveResult* result,double videoFps,Clock::time_point now) {
    if(result)for(const auto& obj:result->objects){
        const auto& b=obj.box;
        cv::Scalar color=!obj.judged?cv::Scalar(0,190,255):obj.score>=threshold?cv::Scalar(40,40,240):cv::Scalar(50,215,70);
        cv::rectangle(bgr,b,color,3);
        cv::drawMarker(bgr,cv::Point(b.x+b.width/2,b.y+b.height/2),color,cv::MARKER_CROSS,18,2);
        std::ostringstream text;text<<label(obj);
        if(obj.judged)text<<" "<<std::fixed<<std::setprecision(3)<<obj.score;
        const int labelX=std::min(b.x,std::max(0,bgr.cols-360));
        const int labelY=std::min(std::max(80,b.y-42),std::max(0,bgr.rows-36));
        cv::Mat labelPixels(36,std::min(360,bgr.cols-labelX),CV_8UC3,cv::Scalar(30,25,20));
        cv::putText(labelPixels,text.str(),cv::Point(3,27),cv::FONT_HERSHEY_SIMPLEX,.9,color,2);
        labelPixels.copyTo(bgr(cv::Rect(labelX,labelY,labelPixels.cols,labelPixels.rows)));
    }
    // Rasterize the banner in normal RAM, then copy its small ROI to the framebuffer.
    cv::Mat banner(std::min(78,bgr.rows),bgr.cols,CV_8UC3,cv::Scalar(30,25,20));
    std::ostringstream first,second;
    first<<std::fixed<<std::setprecision(1)<<"V28 | VIDEO "<<videoFps<<" FPS | INFER "
         <<(result?result->inferenceFps:0)<<" FPS";
    if(result)second<<result->objects.size()<<" BLUE OBJECTS | RESULT FRAME "<<result->frame
                    <<" | AGE "<<int(resultAgeMs(*result,now))<<" ms | PROCESS "<<int(result->processingMs)<<" ms";
    else second<<"WAITING FOR FIRST INFERENCE";
    cv::putText(banner,first.str(),cv::Point(12,30),cv::FONT_HERSHEY_SIMPLEX,.7,cv::Scalar(240,240,240),2);
    cv::putText(banner,second.str(),cv::Point(12,62),cv::FONT_HERSHEY_SIMPLEX,.65,cv::Scalar(240,240,240),2);
    banner.copyTo(bgr(cv::Rect(0,0,banner.cols,banner.rows)));
}
const char* liveLabel(const LiveObject& obj) {
    if(!obj.judged)return obj.track.partial||obj.track.ambiguous?"REVIEW":"PENDING";
    return obj.classification.score>=threshold?"DEFECT":"NORMAL";
}
void drawLive(cv::Mat& bgr,const std::vector<LiveObject>& objects,const LiveResult* result,
              double videoFps,double boxFps,Clock::time_point now) {
    for(const auto& obj:objects){
        const auto& b=obj.track.box;
        const cv::Point center(cvRound(obj.track.center.x),cvRound(obj.track.center.y));
        const cv::Scalar color=!obj.judged?cv::Scalar(0,190,255):obj.classification.score>=threshold?
            cv::Scalar(40,40,240):cv::Scalar(50,215,70);
        cv::rectangle(bgr,b,color,3);cv::drawMarker(bgr,center,color,cv::MARKER_CROSS,18,2);
        std::ostringstream status,position;status<<liveLabel(obj);
        if(obj.judged)status<<" "<<std::fixed<<std::setprecision(3)<<obj.classification.score
            <<" AGE "<<int(std::chrono::duration<double,std::milli>(now-obj.classification.captured).count())<<"ms";
        position<<"ID "<<obj.track.id<<" CENTER ("<<center.x<<", "<<center.y<<")";
        const int labelX=std::min(b.x,std::max(0,bgr.cols-480));
        const int labelY=std::min(std::max(80,b.y-68),std::max(0,bgr.rows-62));
        cv::Mat labelPixels(std::min(62,bgr.rows-labelY),std::min(480,bgr.cols-labelX),CV_8UC3,cv::Scalar(30,25,20));
        cv::putText(labelPixels,status.str(),cv::Point(3,25),cv::FONT_HERSHEY_SIMPLEX,.65,color,2);
        cv::putText(labelPixels,position.str(),cv::Point(3,53),cv::FONT_HERSHEY_SIMPLEX,.65,color,2);
        labelPixels.copyTo(bgr(cv::Rect(labelX,labelY,labelPixels.cols,labelPixels.rows)));
    }
    cv::Mat banner(std::min(78,bgr.rows),bgr.cols,CV_8UC3,cv::Scalar(30,25,20));
    std::ostringstream first,second;
    first<<std::fixed<<std::setprecision(1)<<"V28 | VIDEO "<<videoFps<<" FPS | BOX "<<boxFps
         <<" FPS | DEFECT "<<(result?result->inferenceFps:0)<<" FPS";
    second<<objects.size()<<" BLUE OBJECTS | BOX/CENTER: CURRENT VIDEO FRAME";
    if(result)second<<" | DEFECT RESULT AGE "<<int(resultAgeMs(*result,now))<<" ms";
    else second<<" | WAITING FOR DEFECT RESULT";
    cv::putText(banner,first.str(),cv::Point(12,30),cv::FONT_HERSHEY_SIMPLEX,.7,cv::Scalar(240,240,240),2);
    cv::putText(banner,second.str(),cv::Point(12,62),cv::FONT_HERSHEY_SIMPLEX,.6,cv::Scalar(240,240,240),2);
    banner.copyTo(bgr(cv::Rect(0,0,banner.cols,banner.rows)));
}
std::string liveJson(const std::vector<LiveObject>& objects,const LiveResult* result,unsigned long long frame,int width,int height,
                     double videoFps,double boxFps,double locateHighrateMs,Clock::time_point now) {
    std::ostringstream json;
    json<<std::setprecision(9)<<"{\"model\":\"V28\",\"threshold\":0.88,\"frame\":"<<frame
        <<",\"video_frame\":"<<frame<<",\"inference_frame\":";
    if(result)json<<result->frame;else json<<"null";
    json<<",\"video_fps\":"<<videoFps<<",\"box_frame\":"<<frame<<",\"box_fps\":"<<boxFps
        <<",\"locate_highrate_ms\":"<<locateHighrateMs<<",\"inference_fps\":"<<(result?result->inferenceFps:0)
        <<",\"inference_age_ms\":";
    if(result)json<<resultAgeMs(*result,now);else json<<"null";
    json<<",\"width\":"<<width<<",\"height\":"<<height
        <<",\"processing_ms\":"<<(result?result->processingMs:0)
        <<",\"locate_ms\":"<<(result?result->timing.locateMs:0)
        <<",\"preprocess_ms\":"<<(result?result->timing.preprocessMs:0)
        <<",\"features_ms\":"<<(result?result->timing.featuresMs:0)
        <<",\"bridge_ms\":"<<(result?result->timing.bridgeMs:0)
        <<",\"head_ms\":"<<(result?result->timing.headMs:0)<<",\"objects\":[";
    for(size_t i=0;i<objects.size();++i){
        const auto& obj=objects[i];const auto& b=obj.track.box;
        if(i)json<<',';
        json<<"{\"track_id\":"<<obj.track.id<<",\"label\":\""<<liveLabel(obj)<<"\",\"score\":";
        if(obj.judged)json<<obj.classification.score;else json<<"null";
        json<<",\"score_age_ms\":";
        if(obj.judged)json<<std::chrono::duration<double,std::milli>(now-obj.classification.captured).count();else json<<"null";
        json<<",\"inference_frame\":";
        if(obj.judged)json<<obj.classification.frame;else json<<"null";
        json<<",\"partial\":"<<(obj.track.partial?"true":"false")
            <<",\"ambiguous\":"<<(obj.track.ambiguous?"true":"false");
        json<<",\"bbox_xyxy\":["<<b.x<<','<<b.y<<','<<b.br().x<<','<<b.br().y
            <<"],\"center_xy\":["<<obj.track.center.x<<','<<obj.track.center.y<<"]}";
    }
    json<<"]}";return json.str();
}
void saveLiveStatus(const Options& o,const std::string& json,bool print) {
    if(!o.latest.empty()){
        std::string temp=o.latest+".tmp";std::ofstream out(temp);out<<json<<'\n';out.close();
        if(!out||std::rename(temp.c_str(),o.latest.c_str()))throw std::runtime_error("cannot save status");
    }
    if(print&&!o.quiet)std::cout<<json<<std::endl;
}
void liveCamera(IndexedExecutor& features,IndexedExecutor& exec,FramebufferDisplay& display,const Options& o) {
    cv::setNumThreads(1);
    DriverCamera camera(o.camera);
    triangle_tracking::FastTracker tracker(o.trackWidth);
    std::vector<triangle_tracking::Track> tracks;
    std::vector<LiveObject> objects;
    std::map<uint64_t,Classification> scores;
    LiveState state;
    cv::Mat bgr=o.directDisplay?display.directBgr(camera.width(),camera.height()):cv::Mat();
    const bool direct=!bgr.empty();
    auto nextInference=Clock::now(),nextStatus=nextInference,fpsStart=nextInference,boxFpsStart=nextInference;
    unsigned long long frame=0,windowFrames=0,boxWindowFrames=0;
    double videoFps=0,boxFps=0,locateHighrateMs=0;
    std::shared_ptr<const LiveResult> shown;
    std::exception_ptr loopError;
    std::thread worker([&]{
        try{
            Clock::time_point previousFinished{};
            for(;;){
                LiveTask task;
                {
                    std::unique_lock<std::mutex> lock(state.mutex);
                    state.ready.wait(lock,[&]{return state.quit||state.pending;});
                    if(state.quit&&!state.pending)break;
                    task=std::move(state.task);state.pending=false;state.busy=true;
                }
                auto start=Clock::now();
                auto result=std::make_shared<LiveResult>();
                result->frame=task.frame;result->captured=task.captured;
                const auto locateStart=Clock::now();
                result->objects=locate(task.image);
                result->timing.locateMs=std::chrono::duration<double,std::milli>(Clock::now()-locateStart).count();
                associateSnapshotTracks(result->objects,task.tracks);
                for(auto& obj:result->objects)if(!obj.partial){
                    const auto preprocessStart=Clock::now();
                    auto values=input(task.image(obj.crop),true);
                    result->timing.preprocessMs+=std::chrono::duration<double,std::milli>(Clock::now()-preprocessStart).count();
                    obj.score=infer(features,exec,values,&result->timing);obj.judged=true;
                }
                const auto finished=Clock::now();
                result->processingMs=std::chrono::duration<double,std::milli>(finished-start).count();
                if(previousFinished!=Clock::time_point{})
                    result->inferenceFps=1./std::chrono::duration<double>(finished-previousFinished).count();
                previousFinished=finished;
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.result=result;
                }
                // JPEG encoding runs here only once per new inference result. Its pixels
                // are this task's private snapshot, so neither video nor boxes can race.
                if(!o.output.empty()){
                    drawInferenceSnapshot(task.image,result.get(),task.videoFps,finished);
                    if(!cv::imwrite(o.output,task.image))throw std::runtime_error("cannot save image");
                }
                {
                    std::lock_guard<std::mutex> lock(state.mutex);state.busy=false;
                }
            }
        }catch(...){
            std::lock_guard<std::mutex> lock(state.mutex);
            state.error=std::current_exception();state.quit=true;state.busy=false;
        }
    });
    try{
        for(;!stopped&&(o.frames==0||frame<static_cast<unsigned>(o.frames));++frame){
            const auto frameStart=Clock::now();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if(state.error)std::rethrow_exception(state.error);
            }
            cv::Mat bgra=camera.capture();
            const auto captured=Clock::now();
            // High-rate geometry always comes from this completed, unannotated
            // DMA frame. The low-rate defect result never supplies live boxes.
            const auto locateStart=Clock::now();
            tracks=tracker.update(bgra);
            const auto located=Clock::now();
            locateHighrateMs=std::chrono::duration<double,std::milli>(located-locateStart).count();
            ++boxWindowFrames;
            const double boxElapsed=std::chrono::duration<double>(located-boxFpsStart).count();
            if(boxElapsed>=.5){boxFps=boxWindowFrames/boxElapsed;boxWindowFrames=0;boxFpsStart=located;}
            std::shared_ptr<const LiveResult> result;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if(state.error)std::rethrow_exception(state.error);
                // No queue: busy frames are skipped, and the next idle task takes
                // the newest DMA frame. Convert into owned RAM before any overlays.
                if(!state.busy&&!state.pending&&captured>=nextInference){
                    cv::cvtColor(bgra,state.task.image,cv::COLOR_BGRA2BGR);
                    state.task.frame=frame;state.task.captured=captured;state.task.videoFps=videoFps;
                    state.task.tracks=tracks;
                    state.pending=true;nextInference=captured+std::chrono::milliseconds(o.inferIntervalMs);
                    state.ready.notify_one();
                }
                result=state.result;
            }
            const bool updated=result!=shown;
            objects=currentObjects(tracks,updated?result.get():nullptr,scores);
            cv::cvtColor(bgra,bgr,cv::COLOR_BGRA2BGR);
            auto now=Clock::now();
            ++windowFrames;
            const double elapsed=std::chrono::duration<double>(now-fpsStart).count();
            if(elapsed>=.5){videoFps=windowFrames/elapsed;windowFrames=0;fpsStart=now;}
            drawLive(bgr,objects,result.get(),videoFps,boxFps,now);
            if(!direct)display.show(bgr);
            if(updated||now>=nextStatus){
                saveLiveStatus(o,liveJson(objects,result.get(),frame,bgr.cols,bgr.rows,videoFps,boxFps,locateHighrateMs,now),updated||frame==0);
                nextStatus=now+std::chrono::milliseconds(250);
            }
            shown=result;
            // This is only an upper bound. Displayed FPS is measured from frames,
            // never from this setting or the HDMI mode's refresh frequency.
            if(o.videoFps>0)std::this_thread::sleep_until(frameStart+
                std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1./o.videoFps)));
        }
    }catch(...){loopError=std::current_exception();}
    {
        std::lock_guard<std::mutex> lock(state.mutex);state.quit=true;
    }
    state.ready.notify_one();
    worker.join(); // One join path, before camera, display, and runtime destruction.
    if(loopError)std::rethrow_exception(loopError);
    if(state.error)std::rethrow_exception(state.error);
    objects=currentObjects(tracks,state.result!=shown?state.result.get():nullptr,scores);
    if(frame)saveLiveStatus(o,liveJson(objects,state.result.get(),frame-1,camera.width(),camera.height(),videoFps,boxFps,locateHighrateMs,Clock::now()),true);
}
}
int main(int argc,char**argv){
    std::unique_ptr<httplive::Publisher> httpPublisher;
    try{
        Options o=parse(argc,argv);cv::setNumThreads(2);
        if(!o.httpSource.empty()) {
            std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
            httpPublisher.reset(new httplive::Publisher(o.httpOutputDir));
            httpPublisher->status("connecting","Loading unchanged V28 feature and head models");
        }
        if(o.check){
            auto parser=armnnOnnxParser::IOnnxParser::Create();
            auto network=parser->CreateNetworkFromBinaryFile(o.model.c_str());
            for(const auto& name:parser->GetInputTensorNames()){
                const auto info=parser->GetNetworkInputBindingInfo(name).second;
                std::cout<<"INPUT "<<name<<" type="<<int(info.GetDataType())<<" elements="<<info.GetNumElements()<<std::endl;
            }
            for(const auto& name:parser->GetOutputTensorNames()){
                const auto info=parser->GetNetworkOutputBindingInfo(name).second;
                std::cout<<"OUTPUT "<<name<<" type="<<int(info.GetDataType())<<" elements="<<info.GetNumElements()<<std::endl;
            }
            std::cout<<"ARMNN_PARSE_OK"<<std::endl;return 0;
        }
        if(!o.preprocess.empty()){
            cv::Mat im=cv::imread(o.image);if(im.empty())throw std::runtime_error("cannot read image");
            if(!o.crop){auto boxes=locate(im);if(boxes.size()!=1)throw std::runtime_error("preprocess expects one object");im=im(boxes[0].crop);}
            auto values=input(im,!o.crop);std::ofstream out(o.preprocess,std::ios::binary);out.write((char*)values.data(),values.size()*sizeof(float));
            return out?0:1;
        }
        std::vector<armnn::BackendId> backends{"Alnpu","CpuAcc"};
        std::vector<std::string> models{o.features,o.model};
        BufferedNPUExecutorMultiModel runtime(models,backends);
        IndexedExecutor features(runtime,0),exec(runtime,1);
        if(features.GetInputTensorNum()!=1||features.GetOutputTensorNum()!=1||exec.GetInputTensorNum()!=1||exec.GetOutputTensorNum()!=1)
            throw std::runtime_error("expected one input and one output per network");
        if(httpPublisher){httpCamera(features,exec,o,*httpPublisher);return 0;}
        FramebufferDisplay display(o.display);
        if(!o.image.empty()){cv::Mat im=cv::imread(o.image);if(im.empty())throw std::runtime_error("cannot read image");process(im,features,exec,display,o,0);return 0;}
        std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
        liveCamera(features,exec,display,o);
        return 0;
    }catch(const std::exception&e){
        if(httpPublisher)try{httpPublisher->status(stopped?"stopped":"inference_error",e.what());}catch(...){}
        std::cerr<<"triangle_defect_npu: "<<e.what()<<std::endl;return 1;
    }
}
