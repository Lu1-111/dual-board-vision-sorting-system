#ifndef LINE_CAN_PROTOCOL_H
#define LINE_CAN_PROTOCOL_H
#include <cstdint>
#include <cstring>

// Classic CAN, standard 11-bit identifiers, eight data bytes, little-endian integers.
namespace LineCan {
constexpr uint8_t Version = 1;
constexpr uint32_t Hello = 0x100, Heartbeat = 0x101, Status = 0x102;
constexpr uint32_t CountBase = 0x110, Alarm = 0x120, Ack = 0x220;
struct Frame { uint32_t id = 0; uint8_t len = 8; uint8_t data[8] = {}; };
inline uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) | uint32_t(u16(p+2)) << 16; }
inline void put16(uint8_t *p, uint16_t n) { p[0]=uint8_t(n); p[1]=uint8_t(n>>8); }
inline void put32(uint8_t *p, uint32_t n) { put16(p,uint16_t(n)); put16(p+2,uint16_t(n>>16)); }
inline Frame hello(uint32_t session, bool simulated) {
    Frame f; f.id=Hello; f.data[0]=Version; f.data[1]=simulated; put32(f.data+4,session); return f;
}
inline Frame heartbeat(uint8_t seq, uint8_t state, uint8_t alarms, uint32_t uptime) {
    Frame f; f.id=Heartbeat; f.data[0]=Version; f.data[1]=seq; f.data[2]=state;
    f.data[3]=alarms; put32(f.data+4,uptime); return f;
}
inline Frame status(uint8_t seq,uint8_t state,uint8_t arm,uint8_t vision,uint8_t color,uint8_t confidence) {
    Frame f; f.id=Status; f.data[0]=Version; f.data[1]=seq; f.data[2]=state;
    f.data[3]=arm; f.data[4]=vision; f.data[5]=color; f.data[6]=confidence; return f;
}
inline Frame counter(uint8_t seq,unsigned index,uint32_t value) {
    Frame f; f.id=CountBase+index; f.data[0]=Version; f.data[1]=seq; put32(f.data+4,value); return f;
}
inline Frame alarm(uint32_t event,uint8_t type,uint8_t color,bool active,uint16_t duration) {
    Frame f; f.id=Alarm; put32(f.data,event); f.data[4]=uint8_t((type<<4)|color);
    f.data[5]=active; put16(f.data+6,duration); return f;
}
inline bool valid(const Frame &f) {
    if (f.len!=8 || f.id>0x7ff) return false;
    const auto *p=f.data;
    if (f.id==Hello) return p[0]==Version && p[1]<=1 && !p[2] && !p[3] && u32(p+4)!=0;
    if (f.id==Heartbeat) return p[0]==Version && p[2]<=3 && p[3]<=3;
    if (f.id==Status) return p[0]==Version && p[2]<=3 && p[3]<=2 && p[4]<=3 && p[5]<=5 && p[6]<=100 && !p[7] && (p[5] || !p[6]);
    if (f.id>=CountBase && f.id<CountBase+5) return p[0]==Version && !p[2] && !p[3];
    if (f.id==Alarm || f.id==Ack) return u32(p)!=0 && (p[4]>>4)>=1 && (p[4]>>4)<=3 && (p[4]&15)<=5 && p[5]<=1;
    return false;
}
// UDP test envelope is explicit, not a native struct dump. It never leaves loopback.
inline void pack(const Frame &f,uint8_t out[13]) { put32(out,f.id); out[4]=f.len; std::memcpy(out+5,f.data,8); }
inline Frame unpack(const uint8_t in[13]) { Frame f; f.id=u32(in); f.len=in[4]; std::memcpy(f.data,in+5,8); return f; }
}
#endif
