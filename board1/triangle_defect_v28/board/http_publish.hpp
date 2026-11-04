#pragma once
#include "http_frame_io.hpp"
#include <deque>
#include <iomanip>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>

namespace httplive {
inline std::string jsonString(const std::string& value) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:value){
        if(c=='"'||c=='\\')out<<'\\'<<char(c);
        else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else out<<char(c);
    }
    out<<'"';return out.str();
}
inline void atomicFile(const std::string& path,const void* bytes,size_t size) {
    const std::string temporary=path+".tmp."+std::to_string(getpid());
    int fd=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)throw std::runtime_error("Cannot create atomic output: "+temporary);
    const char* p=static_cast<const char*>(bytes);size_t offset=0;
    while(offset<size){ssize_t n=write(fd,p+offset,size-offset);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);unlink(temporary.c_str());throw std::runtime_error("Cannot write output");}offset+=size_t(n);}
    if(close(fd)!=0){unlink(temporary.c_str());throw std::runtime_error("Cannot close output");}
    if(rename(temporary.c_str(),path.c_str())!=0){unlink(temporary.c_str());throw std::runtime_error("Cannot publish output");}
}
class Publisher {
    std::string directory_;int lock_=-1;
    std::deque<std::string> owned_;
public:
    explicit Publisher(std::string directory):directory_(std::move(directory)) {
        if(directory_.empty()||directory_[0]!='/'||directory_=="/"||directory_.find("/../")!=std::string::npos)
            throw std::runtime_error("HTTP output directory must be an absolute dedicated directory");
        while(directory_.size()>1&&directory_.back()=='/')directory_.pop_back();
        if(mkdir(directory_.c_str(),0700)!=0&&errno!=EEXIST)throw std::runtime_error("Cannot create HTTP output directory");
        struct stat st{};
        if(lstat(directory_.c_str(),&st)!=0||!S_ISDIR(st.st_mode))throw std::runtime_error("HTTP output path must be a real directory");
        lock_=open((directory_+"/.writer.lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        if(lock_<0||flock(lock_,LOCK_EX|LOCK_NB)!=0){if(lock_>=0)close(lock_);lock_=-1;throw std::runtime_error("Another triangle HTTP publisher holds the directory lock");}
    }
    ~Publisher(){for(const auto& p:owned_)unlink(p.c_str());if(lock_>=0)close(lock_);}
    Publisher(const Publisher&)=delete;Publisher& operator=(const Publisher&)=delete;
    void status(const std::string& status,const std::string& message) {
        std::ostringstream s;s<<"{\"schema\":1,\"model\":\"V28\",\"threshold\":0.88,\"status\":"<<jsonString(status)
            <<",\"message\":"<<jsonString(message)<<",\"published_monotonic_ms\":"<<liveio::monotonicMs()
            <<",\"source_frame_age_ms\":null,\"source_session\":\"\",\"frame_sequence\":\"\",\"frame_path\":\"\","
            <<"\"width\":1920,\"height\":1080,\"objects\":[],\"processing_ms\":0}";
        const auto text=s.str();atomicFile(directory_+"/latest.json",text.data(),text.size());
    }
    void result(const liveio::Frame& frame,const std::string& objects,double processingMs,
                const std::string& timing,double fps) {
        if(frame.ageMs()>30000)throw liveio::Stale("Completed result exceeds 30 seconds; discarding");
        const std::string path=directory_+"/frame_"+frame.session+"_"+std::to_string(frame.sequence)+".jpg";
        atomicFile(path,frame.jpeg.data(),frame.jpeg.size());
        const uint64_t age=frame.ageMs();
        if(age>30000){unlink(path.c_str());throw liveio::Stale("Frame expired during image publication");}
        std::ostringstream s;s<<std::setprecision(12)
            <<"{\"schema\":1,\"model\":\"V28\",\"threshold\":0.88,\"status\":\"ok\",\"message\":"
            <<jsonString(age>5000?"Historical result: source frame older than 5 seconds":"Shared-camera V28 result")
            <<",\"published_monotonic_ms\":"<<liveio::monotonicMs()<<",\"source_frame_age_ms\":"<<age
            <<",\"historical\":"<<(age>5000?"true":"false")<<",\"source_session\":"<<jsonString(frame.session)
            <<",\"frame_sequence\":"<<jsonString(std::to_string(frame.sequence))<<",\"frame_path\":"<<jsonString(path)
            <<",\"source\":"<<jsonString(frame.source)<<",\"captured_utc\":"<<jsonString(frame.capturedUtc)
            <<",\"width\":"<<frame.width<<",\"height\":"<<frame.height<<",\"objects\":"<<objects
            <<",\"processing_ms\":"<<processingMs<<",\"inference_fps\":"<<fps<<",\"timing_ms\":"<<timing<<"}";
        const auto text=s.str();
        try{atomicFile(directory_+"/latest.json",text.data(),text.size());}
        catch(...){unlink(path.c_str());throw;}
        owned_.push_back(path);
        while(owned_.size()>2){unlink(owned_.front().c_str());owned_.pop_front();}
    }
};
}
