#pragma once
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace liveio {
using Steady=std::chrono::steady_clock;
inline int64_t monotonicMs(){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)throw std::runtime_error("clock_gettime failed");return int64_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
struct Stale:std::runtime_error{using std::runtime_error::runtime_error;};
struct Frame {
    std::vector<uint8_t> jpeg;
    std::string session,capturedUtc,source;
    uint64_t sequence=0,headerAgeMs=0;
    int64_t requestStartedMs=0;
    unsigned width=0,height=0;
    uint64_t ageMs()const { return headerAgeMs+static_cast<uint64_t>(std::max<int64_t>(0,monotonicMs()-requestStartedMs)); }
};
inline uint64_t decimal(const std::string& s) {
    if(s.empty()||s.size()>20||!std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isdigit(c);}))throw std::runtime_error("Invalid numeric HTTP header");
    size_t end=0;auto v=std::stoull(s,&end);if(end!=s.size())throw std::runtime_error("Invalid numeric HTTP header");return v;
}
inline std::string trim(const std::string& s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};auto b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
inline std::string lower(std::string s){for(char& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
struct Url { std::string host,path;unsigned port; };
inline Url parseUrl(const std::string& url) {
    if(url.compare(0,7,"http://")!=0)throw std::runtime_error("URL must use http:// with an IPv4 literal");
    auto slash=url.find('/',7);std::string authority=url.substr(7,slash==std::string::npos?std::string::npos:slash-7);
    auto colon=authority.find(':');Url u{authority.substr(0,colon),slash==std::string::npos?"/":url.substr(slash),80};
    if(colon!=std::string::npos){auto v=decimal(authority.substr(colon+1));if(v==0||v>65535)throw std::runtime_error("Invalid port");u.port=static_cast<unsigned>(v);}
    struct in_addr addr;if(inet_pton(AF_INET,u.host.c_str(),&addr)!=1)throw std::runtime_error("URL requires an IPv4 literal; DNS is not used");
    if(u.path.size()>2048||u.path.find_first_of("\r\n \t")!=std::string::npos||u.path.find('#')!=std::string::npos)throw std::runtime_error("Invalid request path");return u;
}
struct Socket {int fd=-1;~Socket(){if(fd>=0)close(fd);} };
inline void waitReady(int fd,short events,Steady::time_point until,const volatile sig_atomic_t& stop) {
    while(!stop) {
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(until-Steady::now()).count();
        if(remaining<=0)throw std::runtime_error("HTTP deadline exceeded");
        struct pollfd p{fd,events,0};int rc=poll(&p,1,static_cast<int>(std::min<int64_t>(remaining,100)));
        if(rc<0&&errno!=EINTR)throw std::runtime_error("HTTP poll failed");
        if(rc>0){if(p.revents&(POLLNVAL|POLLERR))throw std::runtime_error("HTTP socket error");if(p.revents&(events|POLLHUP))return;}
    }
    throw std::runtime_error("Stopping");
}
inline void jpegSize(Frame& f) {
    const auto& b=f.jpeg;if(b.size()<4||b[0]!=0xff||b[1]!=0xd8)throw std::runtime_error("Not a JPEG");
    if(b[b.size()-2]!=0xff||b.back()!=0xd9)throw std::runtime_error("JPEG has no complete EOI terminator");
    size_t pos=2;
    while(pos+1<b.size()) {
        if(b[pos++]!=0xff)throw std::runtime_error("Invalid JPEG marker");
        while(pos<b.size()&&b[pos]==0xff)++pos;if(pos>=b.size())break;unsigned marker=b[pos++];
        if(marker==0xd9||marker==0xda)break;
        if(marker==0x01||(marker>=0xd0&&marker<=0xd7))continue;
        if(pos+2>b.size())break;size_t len=(size_t(b[pos])<<8)|b[pos+1];if(len<2||len>b.size()-pos)throw std::runtime_error("Truncated JPEG segment");
        const bool sof=marker>=0xc0&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8&&marker!=0xcc;
        if(sof) {
            if(len<8||b[pos+2]!=8)throw std::runtime_error("Unsupported JPEG precision");
            f.height=(unsigned(b[pos+3])<<8)|b[pos+4];f.width=(unsigned(b[pos+5])<<8)|b[pos+6];
            if(!f.width||!f.height||f.width>4096||f.height>4096||uint64_t(f.width)*f.height>8847360)throw std::runtime_error("JPEG dimensions exceed limits");
            return;
        }
        pos+=len;
    }
    throw std::runtime_error("JPEG missing bounded SOF dimensions");
}
inline Frame fetch(const Url& u,const volatile sig_atomic_t& stop) {
    constexpr size_t maxHeader=65536,maxJpeg=8*1024*1024;
    Frame f;f.requestStartedMs=monotonicMs();Socket s;s.fd=socket(AF_INET,SOCK_STREAM,0);if(s.fd<0)throw std::runtime_error("socket failed");
    if(fcntl(s.fd,F_SETFL,O_NONBLOCK)<0)throw std::runtime_error("fcntl failed");
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(static_cast<uint16_t>(u.port));inet_pton(AF_INET,u.host.c_str(),&addr.sin_addr);
    auto deadline=Steady::now()+std::chrono::seconds(2);int rc=connect(s.fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
    if(rc<0&&errno!=EINPROGRESS)throw std::runtime_error("Camera connection failed");
    if(rc<0){waitReady(s.fd,POLLOUT,deadline,stop);int error=0;socklen_t n=sizeof(error);if(getsockopt(s.fd,SOL_SOCKET,SO_ERROR,&error,&n)!=0||error)throw std::runtime_error("Camera connection failed");}
    // The same absolute deadline covers connect, send, headers and body.
    const std::string request="GET "+u.path+" HTTP/1.1\r\nHost: "+u.host+":"+std::to_string(u.port)+"\r\nConnection: close\r\nAccept: image/jpeg\r\n\r\n";
    size_t sent=0;while(sent<request.size()){waitReady(s.fd,POLLOUT,deadline,stop);auto n=send(s.fd,request.data()+sent,request.size()-sent,MSG_NOSIGNAL);if(n<0&&(errno==EAGAIN||errno==EINTR))continue;if(n<=0)throw std::runtime_error("HTTP send failed");sent+=static_cast<size_t>(n);}
    std::vector<uint8_t> bytes;bytes.reserve(262144);size_t headerEnd=std::string::npos,bodyLength=0;
    while(true) {
        if(headerEnd!=std::string::npos&&bytes.size()>=headerEnd+bodyLength)break;
        waitReady(s.fd,POLLIN,deadline,stop);uint8_t chunk[16384];auto n=recv(s.fd,chunk,sizeof(chunk),0);
        if(n<0&&(errno==EAGAIN||errno==EINTR))continue;if(n<=0)throw std::runtime_error("Truncated HTTP response");
        bytes.insert(bytes.end(),chunk,chunk+n);if(bytes.size()>maxHeader+maxJpeg)throw std::runtime_error("HTTP response exceeds limits");
        if(headerEnd==std::string::npos) {
            const std::string text(bytes.begin(),bytes.end());auto split=text.find("\r\n\r\n");
            if(split==std::string::npos){if(bytes.size()>maxHeader)throw std::runtime_error("HTTP headers too large");continue;}
            headerEnd=split+4;if(headerEnd>maxHeader)throw std::runtime_error("HTTP headers too large");
            auto lineEnd=text.find("\r\n");const std::string status=text.substr(0,lineEnd);
            if(status.compare(0,13,"HTTP/1.1 200 ")!=0&&status.compare(0,13,"HTTP/1.0 200 ")!=0)throw std::runtime_error("Camera HTTP status is not 200");
            std::map<std::string,std::string> headers;size_t offset=lineEnd+2;
            while(offset<split){auto end=text.find("\r\n",offset);auto colon=text.find(':',offset);if(end==std::string::npos||colon==std::string::npos||colon>=end)throw std::runtime_error("Invalid HTTP header");auto key=lower(trim(text.substr(offset,colon-offset)));if(headers.count(key))throw std::runtime_error("Duplicate HTTP header");headers[key]=trim(text.substr(colon+1,end-colon-1));offset=end+2;}
            auto get=[&](const std::string& key)->std::string {auto i=headers.find(key);if(i==headers.end()||i->second.empty())throw std::runtime_error("Missing HTTP header: "+key);return i->second;};
            if(headers.count("transfer-encoding"))throw std::runtime_error("Chunked HTTP is not accepted");
            auto length=decimal(get("content-length"));if(length<4||length>maxJpeg)throw std::runtime_error("JPEG Content-Length out of bounds");bodyLength=static_cast<size_t>(length);
            const auto mediaType=lower(get("content-type"));
            if(trim(mediaType.substr(0,mediaType.find(';')))!="image/jpeg")throw std::runtime_error("Expected image/jpeg");
            f.session=get("x-camera-session");f.sequence=decimal(get("x-frame-sequence"));f.headerAgeMs=decimal(get("x-frame-age-ms"));f.capturedUtc=get("x-captured-utc");f.source=get("x-camera-source");
            if(f.session.size()>128||f.capturedUtc.size()>128||f.source!="imx415")throw std::runtime_error("Invalid camera identity headers");
            if(!std::all_of(f.session.begin(),f.session.end(),[](unsigned char c){return std::isalnum(c)||c=='-';}))throw std::runtime_error("Invalid camera session");
            if(f.headerAgeMs>5000)throw Stale("Camera frame is already stale");
        }
    }
    if(bytes.size()!=headerEnd+bodyLength)throw std::runtime_error("HTTP body length mismatch");
    f.jpeg.assign(bytes.begin()+headerEnd,bytes.end());jpegSize(f);return f;
}
struct SequenceGate {
    std::string session;uint64_t sequence=0;std::set<std::string> retired;
    void accept(const Frame& f) {
        if(f.ageMs()>5000)throw Stale("Camera frame exceeded 5000 ms freshness limit");
        if(session==f.session){if(f.sequence<=sequence)throw Stale("Camera frame sequence did not advance");}
        else {if(retired.count(f.session))throw Stale("Camera returned a retired session");if(!session.empty()){if(retired.size()>=64)throw Stale("Too many camera sessions; restart worker");retired.insert(session);}session=f.session;}
        sequence=f.sequence;
    }
};
} // namespace liveio
