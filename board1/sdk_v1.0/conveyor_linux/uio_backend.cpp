#include "conveyor.hpp"
#include <stdexcept>
#ifdef __linux__
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace conveyor {
#ifdef __linux__
namespace {
class UioRegisters : public Registers {
public:
    explicit UioRegisters(const std::string &path) {
        const std::string prefix="/dev/uio";
        if(path.compare(0,prefix.size(),prefix)!=0 || path.size()==prefix.size() ||
           path.find_first_not_of("0123456789",prefix.size())!=std::string::npos)
            throw std::runtime_error("expected /dev/uioN");
        const auto sys="/sys/class/uio/"+path.substr(5);
        std::string name,size;
        std::ifstream(sys+"/name")>>name;
        std::ifstream(sys+"/maps/map0/size")>>size;
        if(name!="conveyor") throw std::runtime_error("UIO device name must be conveyor");
        size_t used=0;
        const auto bytes=std::stoull(size,&used,0);
        if(used!=size.size() || bytes<4096) throw std::runtime_error("UIO map0 must cover 4 KiB");
        std::string offset;
        std::ifstream(sys+"/maps/map0/offset")>>offset;
        if(std::stoull(offset,nullptr,0)!=0) throw std::runtime_error("UIO map0 must be page aligned");
        fd_=open(path.c_str(),O_RDWR|O_CLOEXEC|O_SYNC);
        if(fd_<0) throw std::runtime_error("open UIO: "+std::string(std::strerror(errno)));
        struct stat st;
        if(fstat(fd_,&st)<0 || !S_ISCHR(st.st_mode) || flock(fd_,LOCK_EX|LOCK_NB)<0) {
            close(fd_);fd_=-1;throw std::runtime_error("UIO must be an exclusive character device");
        }
        void *p=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd_,0);
        if(p==MAP_FAILED) {close(fd_);fd_=-1;throw std::runtime_error("UIO mmap failed");}
        base_=static_cast<volatile uint32_t*>(p);
    }
    ~UioRegisters() {if(base_) munmap(const_cast<uint32_t*>(base_),4096);if(fd_>=0) close(fd_);}
    uint32_t read(uint32_t offset) override {
        check(offset); barrier();const auto v=base_[offset/4];barrier();return v;
    }
    void write(uint32_t offset,uint32_t value) override {
        check(offset);barrier();base_[offset/4]=value;barrier();
    }
private:
    static void check(uint32_t offset) {
        if(offset>BenchAB || offset%4) throw std::runtime_error("MMIO offset out of range");
    }
    static void barrier() {
#if defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("dmb sy" ::: "memory");
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
    }
    int fd_=-1;
    volatile uint32_t *base_=nullptr;
};
}
std::unique_ptr<Registers> openUio(const std::string &path) {
    return std::unique_ptr<Registers>(new UioRegisters(path));
}
#else
std::unique_ptr<Registers> openUio(const std::string &) {
    throw std::runtime_error("UIO backend requires Linux; use --simulate");
}
#endif
}
