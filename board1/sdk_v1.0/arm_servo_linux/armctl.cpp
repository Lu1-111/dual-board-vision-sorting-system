#include "arm_control.hpp"
#include "gpio_backend.hpp"
#include "robot_arm.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t interrupted = 0;
void onSignal(int) { interrupted = 1; }
uint64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool number(const std::string &text, double &value) {
    char *end = nullptr; errno = 0;
    value = std::strtod(text.c_str(), &end);
    return !text.empty() && end == text.c_str()+text.size() && errno != ERANGE && std::isfinite(value);
}
bool duration(const std::vector<std::string> &t, std::size_t index, unsigned &ms) {
    ms = 1000;
    if (t.size() == index) return true;
    double value = 0;
    if (t.size() != index+1 || !number(t[index], value) || value < 20 || value > 3600000 || std::floor(value)!=value) return false;
    ms = static_cast<unsigned>(value); return true;
}
void help() {
    std::cout << "Commands (logical axes 0..5 = claw, roll, joint3, joint4, joint5, base):\n"
      " status | fk | ack p0 p1 p2 p3 p4 p5 confirm | enable\n"
      " angles a0 a1 a2 a3 a4 a5 [ms] | pulses p0 p1 p2 p3 p4 p5 [ms]\n"
      " claw open_deg [ms] | roll rotation_deg [ms] | home [ms]\n"
      " preview x_cm y_cm z_cm pitch_deg | coord x_cm y_cm z_cm pitch_deg [ms]\n"
      " xy-plane z_cm pitch_deg [search] | preview-xy x_cm y_cm | xy x_cm y_cm [ms]\n"
      " calibrate axis pulse_us ms confirm | stop (hold) | off (disable) | quit\n"
      "No startup pose is applied. ack declares a caller-verified starting pose; it is not feedback.\n"
      "PL enables all six axes together, including during a single-axis calibration test.\n" << std::flush;
}
void usage() {
    std::cerr << "armctl [--simulate] [--config file]\n"
              << "armctl --hardware --bank2 /dev/gpiochipN --bank3 /dev/gpiochipM --config file\n"
              << "armctl --list | --write-default-config file\n";
}
int listChips() {
    DIR *dir=opendir("/dev"); if (!dir) { std::cerr << "Cannot list /dev: " << std::strerror(errno) << '\n'; return 1; }
    while (dirent *entry=readdir(dir)) {
        if (std::strncmp(entry->d_name,"gpiochip",8)!=0) continue;
        const std::string path=std::string("/dev/")+entry->d_name;
        int fd=open(path.c_str(), O_RDONLY|O_CLOEXEC); if(fd<0) continue;
        gpiochip_info info={};
        if(ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&info)==0)
            std::cout << path << " " << info.name << " " << info.label << " lines=" << info.lines << '\n';
        close(fd);
    }
    closedir(dir); return 0;
}
class Session {
public:
    Session(arm::Controller &controller, arm::RobotArm &robot):c(controller),r(robot) {}
    bool command(const std::string &line) {
        std::istringstream input(line); std::vector<std::string> t; std::string item;
        while(input>>item) t.push_back(item);
        if(t.empty() || t[0][0]=='#') return true;
        const std::string &name=t[0]; unsigned ms=1000; bool ok=false, valid=true, robotError=false;
        if(name=="quit" && t.size()==1) return false;
        if(name=="help" && t.size()==1) {help();return true;}
        if((name=="status" || name=="fk") && t.size()==1) {status();return true;}
        if(name=="stop" && t.size()==1) ok=c.stop();
        else if((name=="off"||name=="disable") && t.size()==1) ok=c.disable();
        else if(name=="enable" && t.size()==1) ok=c.enable();
        else if(name=="ack") {
            arm::Pulses p;valid=t.size()==8 && t.back()=="confirm";
            for(std::size_t i=0; valid && i<6; ++i) valid=number(t[i+1],p[i]);
            if(valid) ok=c.acknowledgeInitialPulses(p,true);
        } else if(name=="angles" || name=="pose" || name=="pulses") {
            arm::Pose p;valid=t.size()>=7 && duration(t,7,ms);
            for(std::size_t i=0; valid && i<6; ++i) valid=number(t[i+1],p[i]);
            if(valid) ok=name=="pulses"?c.movePulses(p,ms,nowMs()):c.moveAngles(p,ms,nowMs());
        } else if(name=="claw" || name=="roll") {
            double value=0;valid=t.size()>=2 && number(t[1],value) && duration(t,2,ms);robotError=true;
            if(valid) ok=name=="claw"?r.moveClaw(value,ms,nowMs()):r.moveRoll(value,ms,nowMs());
        } else if(name=="home") {
            valid=duration(t,1,ms);robotError=true;if(valid)ok=r.home(ms,nowMs());
        } else if(name=="xy-plane") {
            double z=0,pitch=0;
            valid=(t.size()==3||(t.size()==4&&t[3]=="search"))&&number(t[1],z)&&number(t[2],pitch)
                &&pitch>=-180&&pitch<=360;
            if(valid){plane.reset(new arm::XYPlane(z,pitch,t.size()==4));ok=true;}
        } else if(name=="xy" || name=="preview-xy") {
            double x=0,y=0;
            valid=t.size()>=3&&number(t[1],x)&&number(t[2],y)&&
                (name=="preview-xy"?t.size()==3:duration(t,3,ms));robotError=true;
            if(valid&&!plane){c.stop();std::cerr<<"Rejected: configure xy-plane z_cm pitch_deg first\n";hadError=true;return true;}
            if(valid){
                const auto solved=r.previewXY(x,y,*plane);
                if(name=="preview-xy"){
                    if(solved.ok)std::cout<<"XY IK verified: x="<<solved.achieved.xCm<<" y="<<solved.achieved.yCm
                        <<" z="<<solved.achieved.zCm<<" pitch="<<solved.achieved.pitchDeg<<'\n';
                    else {std::cerr<<"XY IK rejected: "<<solved.error<<'\n';hadError=true;}
                    std::cout.flush();return true;
                }
                ok=r.moveXY(x,y,*plane,ms,nowMs());
            }
        } else if(name=="coord" || name=="preview") {
            double p[4]={};valid=t.size()>=5;
            for(std::size_t i=0;valid && i<4;++i)valid=number(t[i+1],p[i]);
            valid=valid && (name=="preview"?t.size()==5:duration(t,5,ms));robotError=true;
            if(valid) {
                const arm::CartesianPose target(p[0],p[1],p[2],p[3]);
                const arm::IKResult solved=r.previewCoordinate(target);
                if(name=="preview") {
                    if(solved.ok) std::cout << "IK verified: x=" << solved.achieved.xCm << " y=" << solved.achieved.yCm
                        << " z=" << solved.achieved.zCm << " pitch=" << solved.achieved.pitchDeg
                        << " position_error_cm=" << solved.positionErrorCm << " pitch_error_deg=" << solved.pitchErrorDeg << '\n';
                    else std::cerr << "IK rejected: " << solved.error << '\n';
                    return true;
                }
                ok=r.moveCoordinate(target,ms,nowMs());
            }
        } else if(name=="calibrate") {
            double axis=0,pulse=0,time=0;
            valid=t.size()==5 && t[4]=="confirm" && number(t[1],axis) && number(t[2],pulse) && number(t[3],time)
                && axis>=0 && axis<=5 && std::floor(axis)==axis && time>=20 && time<=3600000 && std::floor(time)==time;
            if(valid)ok=c.calibrateAxis(static_cast<unsigned>(axis),pulse,static_cast<unsigned>(time),nowMs(),true);
        } else valid=false;
        if(!valid) {c.stop();std::cerr << "Invalid command; motion held. Type help.\n";hadError=true;}
        else if(!ok) {const std::string error=robotError?r.error():c.error();c.stop();std::cerr << "Rejected: " << error << '\n';hadError=true;}
        else std::cout << "OK " << name << '\n';
        std::cout.flush();return true;
    }
    void status() {
        std::cout << "mode=" << (c.hardware()?"hardware":"simulation") << " enabled=" << c.enabled()
            << " moving=" << c.moving() << " initial_ack=" << c.initialPoseAcknowledged()
            << " calibration_confirmed=" << c.config().calibrationConfirmed << " geometry_confirmed=" << c.config().geometryConfirmed << '\n';
        std::cout << "connection_locked=" << c.holdingAfterTimeout()
            << " pl_status_available=" << c.hardwareStatusAvailable()
            << " pl_pwm_active=" << c.hardwareOutputActive() << '\n';
        std::cout << "commanded_us:";for(double p:c.currentPulses())std::cout << ' ' << p;
        std::cout << "\ncommanded_deg:";for(double a:c.currentAngles())std::cout << ' ' << a;
        const arm::CartesianPose p=r.currentCartesian();
        std::cout << "\nFK_cm=" << p.xCm << ',' << p.yCm << ',' << p.zCm << " pitch_deg=" << p.pitchDeg
            << " (command estimate, no position feedback)\n" << std::flush;
    }
    bool hadError=false;
private:arm::Controller &c;arm::RobotArm &r;std::unique_ptr<arm::XYPlane> plane;
};
}
int main(int argc,char **argv) {
    bool hardware=false,simulate=false;std::string path,low,high;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--help") {usage();help();return 0;}
        if(arg=="--list" && argc==2)return listChips();
        if(arg=="--write-default-config" && argc==3) {
            std::string error;if(!arm::ArmConfig::defaults().save(argv[2],error)){std::cerr<<error<<'\n';return 1;}return 0;
        }
        if(arg=="--hardware")hardware=true;
        else if(arg=="--simulate")simulate=true;
        else if((arg=="--config" || arg=="--bank2" || arg=="--bank3") && i+1<argc) {
            std::string &out=arg=="--config"?path:arg=="--bank2"?low:high;out=argv[++i];
        } else {usage();return 2;}
    }
    if((hardware&&simulate) || (hardware&&(path.empty()||low.empty()||high.empty())) || (!hardware&&(!low.empty()||!high.empty()))) {usage();return 2;}
    arm::ArmConfig config=arm::ArmConfig::defaults();std::string error;
    if(!path.empty()&&!arm::ArmConfig::load(path,config,error)){std::cerr<<error<<'\n';return 1;}
    std::unique_ptr<arm::Backend> backend;
    if(hardware)backend.reset(new arm::GPIOBackend(low,high));else backend.reset(new arm::MockBackend);
    arm::Controller controller(*backend,config);if(!controller.initialize()){std::cerr<<controller.error()<<'\n';return 1;}
    arm::RobotArm robot(controller);Session session(controller,robot);
    std::signal(SIGINT,onSignal);std::signal(SIGTERM,onSignal);
    const int oldFlags=fcntl(STDIN_FILENO,F_GETFL);
    if(oldFlags<0 || fcntl(STDIN_FILENO,F_SETFL,oldFlags|O_NONBLOCK)<0) {std::cerr<<"Cannot use nonblocking stdin\n";return 1;}
    std::cout<<std::setprecision(8)<<"armctl "<<(hardware?"hardware; connection locked, explicitly disable before enabling":"simulation; outputs disabled")<<'\n';help();
    std::string buffered;bool active=true;
    while(active&&!interrupted) {
        if(!controller.tick(nowMs())) {std::cerr<<"Motion failed: "<<controller.error()<<'\n';session.hadError=true;}
        pollfd fd={STDIN_FILENO,POLLIN,0};const int ready=poll(&fd,1,10);
        if(ready<0&&errno!=EINTR){session.hadError=true;break;}
        if(ready>0&&(fd.revents&(POLLIN|POLLHUP))) {
            char block[512];const ssize_t count=read(STDIN_FILENO,block,sizeof(block));
            if(count>0) {
                buffered.append(block,static_cast<std::size_t>(count));
                std::size_t nl;
                while(active&&(nl=buffered.find('\n'))!=std::string::npos){active=session.command(buffered.substr(0,nl));buffered.erase(0,nl+1);}
                if(buffered.size()>4096){std::cerr<<"Input line too long\n";session.hadError=true;active=false;}
            } else if(count==0){if(!buffered.empty())session.command(buffered);active=false;}
            else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR){session.hadError=true;active=false;}
        }
        if(ready>0&&(fd.revents&(POLLERR|POLLNVAL))){session.hadError=true;active=false;}
    }
    fcntl(STDIN_FILENO,F_SETFL,oldFlags);
    // EOF, signal or an input error disconnects control without removing
    // holding torque. The 'disable' command is the explicit PWM-off action.
    std::cout<<"Control disconnected; use disable explicitly to turn PWM off.\n";
    return session.hadError?1:0;
}
