#include "gpio_backend.hpp"
#include <cerrno>
#include <cstring>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#ifdef __linux__
#include <fcntl.h>
#include <linux/gpio.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#endif

namespace arm {
namespace {
uint64_t steadyMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}
#ifdef __linux__
namespace {
std::string systemError(const std::string &operation) { return operation+": "+std::strerror(errno); }
bool ancestorHasNode(std::string directory,const char *expected) {
    char resolved[PATH_MAX];
    for(;;) {
        const std::string candidate=directory+"/of_node";
        if(realpath(candidate.c_str(),resolved)) {
            const char *name=std::strrchr(resolved,'/');
            if(name && std::strcmp(name+1,expected)==0)return true;
        }
        const std::size_t slash=directory.find_last_of('/');
        if(slash==std::string::npos||slash==0)return false;
        directory.resize(slash);
    }
}
bool matchesNode(int fd,const char *expected) {
    struct stat st;if(fstat(fd,&st)<0)return false;
    std::ostringstream s;s << "/sys/dev/char/" << major(st.st_rdev) << ':' << minor(st.st_rdev);
    char resolved[PATH_MAX];
    return (realpath(s.str().c_str(),resolved)&&ancestorHasNode(resolved,expected))||
           (realpath((s.str()+"/device").c_str(),resolved)&&ancestorHasNode(resolved,expected));
}
int openVerified(const std::string &path,const char *node,bool unsafe,std::string &error) {
    int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);
    if(fd<0) {error=systemError(path);return -1;}
    struct stat st;struct gpiochip_info info;
    if(fstat(fd,&st)<0 || !S_ISCHR(st.st_mode)) {error="GPIO path is not a character device: "+path;close(fd);return -1;}
    std::memset(&info,0,sizeof(info));
    if(ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&info)<0) {error=systemError("GPIO_GET_CHIPINFO_IOCTL "+path);close(fd);return -1;}
    if(info.lines!=32) {error="GPIO bank must contain exactly 32 lines: "+path;close(fd);return -1;}
    if(!matchesNode(fd,node)&&!unsafe) {error=path+" is not verified as "+node+"; verify device-tree mapping";close(fd);return -1;}
    return fd;
}
int requestLines(int chip,bool status,bool high,std::string &error) {
    struct gpiohandle_request r;std::memset(&r,0,sizeof(r));
    r.flags=status?GPIOHANDLE_REQUEST_INPUT:GPIOHANDLE_REQUEST_OUTPUT;
    r.lines=status?6:(high?26:32);
    for(unsigned index=0;index<r.lines;++index)
        r.lineoffsets[index]=status?18+index:(high?gpioHighCommandLine(index):index);
    std::strncpy(r.consumer_label,status?"arm-status":"arm-joints",sizeof(r.consumer_label)-1);
    if(ioctl(chip,GPIO_GET_LINEHANDLE_IOCTL,&r)<0) {error=systemError("GPIO_GET_LINEHANDLE_IOCTL");return -1;}
    return r.fd;
}
}
#endif
namespace {
class NativeGPIOTransport : public GPIOTransport {
    int low_,high_,status_;
public:
    NativeGPIOTransport():low_(-1),high_(-1),status_(-1) {}
    ~NativeGPIOTransport() {close();}
    bool open(const std::string &lowPath,const std::string &highPath,bool unsafe,std::string &error) {
#ifdef __linux__
        int low=openVerified(lowPath,"gpio@f8411200",unsafe,error);if(low<0)return false;
        int high=openVerified(highPath,"gpio@f8411300",unsafe,error);if(high<0) {::close(low);return false;}
        struct stat ls,hs;
        if(fstat(low,&ls)<0||fstat(high,&hs)<0||ls.st_rdev==hs.st_rdev) {
            error="GPIO banks must be distinct character devices";::close(low);::close(high);return false;
        }
        // Default marker is zero (invalid). Taking ownership never commits a
        // disable or a new pose, even when a previous process held PWM active.
        high_=requestLines(high,false,true,error);
        if(high_>=0)status_=requestLines(high,true,true,error);
        if(status_>=0)low_=requestLines(low,false,false,error);
        ::close(low);::close(high);
        if(low_<0||high_<0||status_<0) {close();return false;}
        return true;
#else
        (void)lowPath;(void)highPath;(void)unsafe;
        error="GPIO backend requires Linux";return false;
#endif
    }
    bool writeBank(int fd,uint32_t word,bool high,std::string &error) {
#ifdef __linux__
        struct gpiohandle_data data;std::memset(&data,0,sizeof(data));
        for(unsigned index=0;index<(high?26u:32u);++index) {
            const unsigned bit=high?gpioHighCommandLine(index):index;
            data.values[index]=(word>>bit)&1u;
        }
        if(ioctl(fd,GPIOHANDLE_SET_LINE_VALUES_IOCTL,&data)<0) {
            error=systemError("GPIOHANDLE_SET_LINE_VALUES_IOCTL");return false;
        }
        return true;
#else
        (void)fd;(void)word;(void)high;error="GPIO backend requires Linux";return false;
#endif
    }
    bool writeLow(uint32_t word,std::string &error) {return writeBank(low_,word,false,error);}
    bool writeHigh(uint32_t word,std::string &error) {return writeBank(high_,word,true,error);}
    bool readStatus(uint8_t &word,std::string &error) {
#ifdef __linux__
        struct gpiohandle_data data;std::memset(&data,0,sizeof(data));
        if(ioctl(status_,GPIOHANDLE_GET_LINE_VALUES_IOCTL,&data)<0) {
            error=systemError("GPIOHANDLE_GET_LINE_VALUES_IOCTL status");return false;
        }
        word=0;for(unsigned bit=0;bit<6;++bit)word|=(data.values[bit]&1u)<<bit;
        return true;
#else
        (void)word;error="GPIO backend requires Linux";return false;
#endif
    }
    void close() {
#ifdef __linux__
        if(low_>=0)::close(low_);
        if(high_>=0)::close(high_);
        if(status_>=0)::close(status_);
#endif
        low_=high_=status_=-1;
    }
    uint64_t nowMs() const {return steadyMs();}
    void sleepMs(unsigned ms) {
#ifdef __linux__
        struct timespec delay;delay.tv_sec=ms/1000;delay.tv_nsec=(ms%1000)*1000000;
        while(nanosleep(&delay,&delay)<0&&errno==EINTR) {}
#else
        (void)ms;
#endif
    }
};
}
GPIOBackend::GPIOBackend(const std::string &low,const std::string &high,bool unsafe)
    :lowChip_(low),highChip_(high),allowUnverified_(unsafe),commit_(false),
     ownedTransport_(new NativeGPIOTransport),transport_(ownedTransport_.get()),
     opened_(false),rearm_(true),statusValid_(false),status_(0) {}
GPIOBackend::GPIOBackend(GPIOTransport &transport)
    :allowUnverified_(false),commit_(false),transport_(&transport),opened_(false),
     rearm_(true),statusValid_(false),status_(0) {}
GPIOBackend::~GPIOBackend() {
    // Releasing a process/session must not turn off holding torque. Only an
    // explicit Controller::disable() sends a disable command.
    if(opened_)transport_->close();
}
bool GPIOBackend::fault(const std::string &message,std::string &error) {
    if(std::getenv("HDMI_UI_MOTION_TRACE"))std::fprintf(stderr,"ARM_GPIO_FAULT ms=%llu status=0x%02x reason=%s\n",static_cast<unsigned long long>(transport_->nowMs()),unsigned(status_),message.c_str());
    rearm_=true;lease_.invalidate();error=message;return false;
}
bool GPIOBackend::refreshStatus(std::string &error) {
    uint8_t first=0,second=0;
    // Read twice to avoid treating a changing multi-bit status as a coherent
    // acknowledgement. Status remains stable once a command is accepted.
    for(unsigned attempt=0;attempt<4;++attempt) {
        if(!transport_->readStatus(first,error)||!transport_->readStatus(second,error)) {
            statusValid_=false;return false;
        }
        if(first==second&&(first&0x30)==0x20) {
            status_=first;statusValid_=true;return true;
        }
        transport_->sleepMs(1);
    }
    statusValid_=false;error="FPGA servo status unavailable or protocol mismatch; matching PL required";return false;
}
bool GPIOBackend::initialize(std::string &error) {
    if(opened_) {error="GPIO backend already initialized";return false;}
    if(!transport_->open(lowChip_,highChip_,allowUnverified_,error))return false;
    opened_=true;commit_=false;transport_->sleepMs(1);
    if(!refreshStatus(error)) {transport_->close();opened_=false;return false;}
    // Every connection requires a human disable/re-enable; initialization must
    // not clear the PL watchdog or drop a pose held by the previous process.
    rearm_=true;error.clear();return true;
}
bool GPIOBackend::send(const ChannelBytes &c,bool enabled,std::string &error) {
    if(!opened_) {error="GPIO backend is not initialized";return false;}
    if(enabled&&(!lease_.check(transport_->nowMs())||requiresRearm()))
        return fault("servo connection locked; explicitly disable before re-enabling",error);
    if(!refreshStatus(error)) {const std::string reason=error;return fault(reason,error);}
    if(enabled&&((status_&1)||((status_&4)==0&&lease_.enabled())))
        return fault("FPGA reset or watchdog latched; last command cannot resume automatically",error);
    const bool next=!commit_;const CommandWords words=encodeCommand(c,enabled,next);
    // GPIO set-multiple may also fall back to per-line writes. Stage all high
    // data with the existing commit, then change only commit in a second call.
    const CommandWords staged=encodeCommand(c,enabled,commit_);
    if(!transport_->writeLow(words.low,error)||!transport_->writeHigh(staged.high,error)||
       !transport_->writeHigh(words.high,error)) {
        const std::string reason=error;return fault(reason+"; PWM preserved, manual rearm required",error);
    }
    commit_=next;
    const uint64_t start=transport_->nowMs();
    // One millisecond also guarantees the commit level is visible before a
    // following call can toggle it back, including an already-disabled pose.
    transport_->sleepMs(1);
    for(unsigned attempt=0;attempt<35;++attempt) {
        if(enabled&&!lease_.check(transport_->nowMs()))return fault("servo command exceeded its lease",error);
        if(!refreshStatus(error)) {const std::string reason=error;return fault(reason,error);}
        if(!(status_&1)&&bool(status_&8)==next&&bool(status_&2)==enabled&&bool(status_&4)==enabled) {
            if(std::getenv("HDMI_UI_MOTION_TRACE")&&(!lease_.enabled()||lastChannels_!=c))
                std::fprintf(stderr,"ARM_GPIO_APPLIED ms=%llu enabled=%d low=0x%08x high=0x%08x axis5Code=%u status=0x%02x\n",static_cast<unsigned long long>(transport_->nowMs()),int(enabled),unsigned(words.low),unsigned(words.high),unsigned(c[4]),unsigned(status_));
            rearm_=false;lastChannels_=c;lease_.committed(transport_->nowMs(),enabled);
            error.clear();return true;
        }
        const uint64_t now=transport_->nowMs();
        if(now<start||now-start>=30)break;
        transport_->sleepMs(1);
    }
    return fault("FPGA did not acknowledge/apply servo command within 30 ms; manual rearm required",error);
}
bool GPIOBackend::keepAlive(std::string &error) {
    if(!opened_) {error="GPIO backend is not initialized";return false;}
    if(!refreshStatus(error)) {const std::string reason=error;return fault(reason,error);}
    if(!rearm_&&((status_&1)||(lease_.enabled()&&!(status_&4)))) {
        if(std::getenv("HDMI_UI_MOTION_TRACE"))std::fprintf(stderr,"ARM_GPIO_STATUS_FAULT ms=%llu status=0x%02x linkLost=%d active=%d\n",static_cast<unsigned long long>(transport_->nowMs()),unsigned(status_),int(bool(status_&1)),int(bool(status_&4)));
        rearm_=true;
    }
    if(requiresRearm()) {error="servo connection locked; PWM state read back; explicit disable/re-enable required";return false;}
    const uint64_t now=transport_->nowMs();
    if(!lease_.check(now))return fault("servo heartbeat expired; last pose held; explicit rearm required",error);
    if(lease_.heartbeatDue(now))return send(lastChannels_,true,error);
    error.clear();return true;
}
}
