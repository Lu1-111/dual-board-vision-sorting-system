#ifndef ARM_GPIO_BACKEND_HPP
#define ARM_GPIO_BACKEND_HPP
#include "arm_control.hpp"
#include "command_lease.hpp"
#include <memory>
namespace arm {
// Claim marker lines first: gpiochip may initialize requested outputs one
// line at a time. Invalidate the old marker before touching enable/commit.
inline unsigned gpioHighCommandLine(unsigned index) { return index<8?24+index:index-8; }
// Injectable GPIO transport: production uses verified Linux gpiochip handles;
// tests exercise the real protocol, restart and I/O failure paths without GPIO.
class GPIOTransport {
public:
    virtual ~GPIOTransport() {}
    virtual bool open(const std::string &,const std::string &,bool,std::string &)=0;
    virtual bool writeLow(uint32_t,std::string &)=0;
    virtual bool writeHigh(uint32_t,std::string &)=0;
    virtual bool readStatus(uint8_t &,std::string &)=0;
    virtual void close()=0;
    virtual uint64_t nowMs() const=0;
    virtual void sleepMs(unsigned)=0;
};
class GPIOBackend : public Backend {
public:
    GPIOBackend(const std::string &lowChip, const std::string &highChip,
                bool allowUnverified = false);
    ~GPIOBackend();
    explicit GPIOBackend(GPIOTransport &transport);
    bool initialize(std::string &error);
    bool send(const ChannelBytes &, bool enabled, std::string &error);
    bool hardware() const { return true; }
    bool keepAlive(std::string &error);
    bool requiresRearm() const { return rearm_ || lease_.requiresRearm(); }
    bool statusAvailable() const { return statusValid_; }
    bool outputActive() const { return statusValid_ && (status_ & 4); }
    GPIOBackend(const GPIOBackend &) = delete;
    GPIOBackend &operator=(const GPIOBackend &) = delete;
private:
    bool refreshStatus(std::string &);
    bool fault(const std::string &,std::string &);
    std::string lowChip_, highChip_;
    bool allowUnverified_, commit_;
    ChannelBytes lastChannels_;
    CommandLease lease_;
    std::unique_ptr<GPIOTransport> ownedTransport_;
    GPIOTransport *transport_;
    bool opened_,rearm_,statusValid_;
    uint8_t status_;
};
}
#endif
