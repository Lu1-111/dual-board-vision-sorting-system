#ifndef ARM_COMMAND_LEASE_HPP
#define ARM_COMMAND_LEASE_HPP
#include <cstdint>

namespace arm {
// Leave 250 ms margin before the PL's 1000 ms watchdog. Expiry is latched:
// neither a late heartbeat nor a new move renews an expired enabled session.
class CommandLease {
public:
    CommandLease():enabled_(false),expired_(false),lastMs_(0) {}
    bool check(uint64_t nowMs) {
        if(enabled_ && (nowMs<lastMs_ || nowMs-lastMs_>=750))expired_=true;
        return !expired_;
    }
    bool heartbeatDue(uint64_t nowMs) {
        return enabled_ && check(nowMs) && nowMs-lastMs_>=200;
    }
    void committed(uint64_t nowMs,bool enabled) {
        lastMs_=nowMs;enabled_=enabled;
        if(!enabled)expired_=false;
    }
    bool enabled() const { return enabled_; }
    void invalidate() { expired_=true; }
    bool requiresRearm() const { return expired_; }
private:
    bool enabled_,expired_;
    uint64_t lastMs_;
};
}
#endif
