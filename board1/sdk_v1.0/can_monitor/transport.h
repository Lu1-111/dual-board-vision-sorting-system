#ifndef LINE_CAN_TRANSPORT_H
#define LINE_CAN_TRANSPORT_H
#include "protocol.h"
#include <string>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

namespace LineCan {
class Transport {
public:
    ~Transport() { close(); }
    Transport() = default;
    Transport(const Transport&) = delete;
    Transport &operator=(const Transport&) = delete;
    int fd() const { return fd_; }
    void close() { if(fd_>=0) ::close(fd_); fd_=-1; peerKnown_=false; }
    const std::string &error() const { return error_; }
    bool openCan(const std::string &name) {
        close(); udp_=false;
        if(name.empty() || name.size()>=IFNAMSIZ) { error_="invalid interface name"; return false; }
        fd_=::socket(PF_CAN,SOCK_RAW|SOCK_NONBLOCK|SOCK_CLOEXEC,CAN_RAW);
        if(fd_<0) return fail();
        ifreq ifr={}; std::strncpy(ifr.ifr_name,name.c_str(),IFNAMSIZ-1);
        if(ioctl(fd_,SIOCGIFINDEX,&ifr)<0) return fail();
        sockaddr_can addr={}; addr.can_family=AF_CAN; addr.can_ifindex=ifr.ifr_ifindex;
        if(ioctl(fd_,SIOCGIFFLAGS,&ifr)<0) return fail();
        if(!(ifr.ifr_flags&IFF_UP)) { errno=ENETDOWN; return fail(); }
        can_err_mask_t errors=CAN_ERR_BUSOFF|CAN_ERR_RESTARTED|CAN_ERR_CRTL;
        if(setsockopt(fd_,SOL_CAN_RAW,CAN_RAW_ERR_FILTER,&errors,sizeof(errors))<0) return fail();
        if(::bind(fd_,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0) return fail();
        error_.clear(); return true;
    }
    bool openUdp(uint16_t port,bool receiver) {
        close(); udp_=true;
        fd_=::socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
        if(fd_<0) return fail();
        sockaddr_in local={}; local.sin_family=AF_INET; local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        local.sin_port=receiver?htons(port):0;
        if(::bind(fd_,reinterpret_cast<sockaddr*>(&local),sizeof(local))<0) return fail();
        if(!receiver) { peer_=local; peer_.sin_port=htons(port); peerKnown_=true; }
        error_.clear(); return true;
    }
    // 1=frame, 0=no data, -1=transport error, -2=malformed/error frame (discard).
    int receive(Frame &f) {
        if(udp_) {
            uint8_t data[14]; sockaddr_in sender={}; socklen_t length=sizeof(sender);
            ssize_t n=recvfrom(fd_,data,sizeof(data),0,reinterpret_cast<sockaddr*>(&sender),&length);
            if(n<0) return readError();
            if(n!=13 || sender.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)) return -2;
            f=unpack(data);
            if(valid(f) && f.id==Hello) { peer_=sender; peerKnown_=true; }
            return 1;
        }
        can_frame raw={}; ssize_t n=::read(fd_,&raw,sizeof(raw));
        if(n<0) return readError();
        if(n!=sizeof(raw)) return -2;
        if(raw.can_id & CAN_ERR_FLAG) { error_="CAN controller error / bus-off"; return -1; }
        if(raw.can_id & (CAN_EFF_FLAG|CAN_RTR_FLAG)) return -2;
        f.id=raw.can_id; f.len=raw.can_dlc; std::memcpy(f.data,raw.data,8); return 1;
    }
    bool send(const Frame &f) {
        if(fd_<0) { error_="transport closed"; return false; }
        ssize_t n;
        if(udp_) {
            if(!peerKnown_) { error_="no peer"; return false; }
            uint8_t data[13]; pack(f,data);
            n=sendto(fd_,data,13,0,reinterpret_cast<sockaddr*>(&peer_),sizeof(peer_));
            if(n==13) return true;
        } else {
            can_frame raw={}; raw.can_id=f.id; raw.can_dlc=f.len; std::memcpy(raw.data,f.data,8);
            n=::write(fd_,&raw,sizeof(raw)); if(n==sizeof(raw)) return true;
        }
        error_=std::strerror(errno); return false;
    }
private:
    bool fail() { error_=std::strerror(errno); close(); return false; }
    int readError() { if(errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR) return 0; error_=std::strerror(errno); return -1; }
    int fd_=-1; bool udp_=false,peerKnown_=false; sockaddr_in peer_={}; std::string error_;
};
}
#endif
