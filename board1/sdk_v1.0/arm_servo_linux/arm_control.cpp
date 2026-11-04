#include "arm_control.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <limits>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace arm {
namespace {
std::string trim(const std::string &s) {
    const std::size_t b=s.find_first_not_of(" \t\r\n"), e=s.find_last_not_of(" \t\r\n");
    return b==std::string::npos ? "" : s.substr(b,e-b+1);
}
bool number(const std::string &s, double &v) {
    std::istringstream in(s); in.imbue(std::locale::classic());
    in >> v; return in && std::isfinite(v) && (in >> std::ws).eof();
}
bool boolean(const std::string &s, bool &v) {
    if(s=="true" || s=="1") { v=true; return true; }
    if(s=="false" || s=="0") { v=false; return true; }
    return false;
}
std::string axisError(std::size_t i,const std::string &s) {
    std::ostringstream o; o << "axis " << i << ": " << s; return o.str();
}
double toUs(const AxisConfig &a,double degree) {
    double ratio=(degree-a.minAngleDeg)/(a.maxAngleDeg-a.minAngleDeg);
    if(a.direction<0) ratio=1-ratio;
    return a.minPulseUs+ratio*(a.maxPulseUs-a.minPulseUs)+a.offsetUs;
}
}
bool CartesianFrame::valid() const {
    for(double value:originCm)if(!std::isfinite(value))return false;
    return std::isfinite(pitchDeg)&&pitchDeg>=-180&&pitchDeg<=360;
}
ArmConfig ArmConfig::defaults() {
    ArmConfig c; c.calibrationConfirmed=false; c.geometryConfirmed=false;
    c.linkLengthsCm={{2.89,10.43,8.9,17.7}};
    const char *names[]={"claw","wrist_roll","joint3","joint4","joint5","base"};
    // Reference commands are templates, not measured mechanical positions.
    const double homesUs[]={770,1500,640,511,1255,1500};
    for(std::size_t i=0;i<AxisCount;++i) {
        AxisConfig a; a.name=names[i]; a.channel=static_cast<unsigned>(i); a.direction=1;
        a.minAngleDeg=0; a.maxAngleDeg=180; a.minPulseUs=500; a.maxPulseUs=PlMaxUs;
        if(i==0) {a.maxAngleDeg=90;a.maxPulseUs=1500;}
        a.offsetUs=0;
        a.homeAngleDeg=(homesUs[i]-a.minPulseUs)/(a.maxPulseUs-a.minPulseUs)*(a.maxAngleDeg-a.minAngleDeg)+a.minAngleDeg;
        a.calibrated=false; c.axes[i]=a;
    }
    return c;
}
bool ArmConfig::validate(std::string &error) const {
    if(!cartesianFrame.valid()) {error="invalid Cartesian origin or reference pitch";return false;}
    std::set<unsigned> channels;
    for(std::size_t i=0;i<AxisCount;++i) {
        const AxisConfig &a=axes[i];
        if(a.name.empty() || a.name.find_first_of("=\r\n#;")!=std::string::npos) {
            error=axisError(i,"invalid name"); return false;
        }
        if(a.channel>=AxisCount || !channels.insert(a.channel).second || (a.direction!=1 && a.direction!=-1)) {
            error=axisError(i,"channel must be a permutation of 0..5 and direction +/-1"); return false;
        }
        const double vals[]={a.minAngleDeg,a.maxAngleDeg,a.minPulseUs,a.maxPulseUs,a.offsetUs,a.homeAngleDeg};
        for(double v:vals) if(!std::isfinite(v)) {error=axisError(i,"nonfinite calibration");return false;}
        if(a.minAngleDeg>=a.maxAngleDeg || a.minPulseUs>=a.maxPulseUs ||
           a.minPulseUs<PlMinUs || a.maxPulseUs>PlMaxUs ||
           std::ceil((a.minPulseUs-PlMinUs)/PlStepUs-1e-9)>std::floor((a.maxPulseUs-PlMinUs)/PlStepUs+1e-9) ||
           a.homeAngleDeg<a.minAngleDeg || a.homeAngleDeg>a.maxAngleDeg ||
           toUs(a,a.homeAngleDeg)<a.minPulseUs || toUs(a,a.homeAngleDeg)>a.maxPulseUs) {
            error=axisError(i,"invalid ranges, pulse offset or home"); return false;
        }
    }
    for(double length:linkLengthsCm) if(!std::isfinite(length)||length<=0||length>1000) {
        error="link lengths must be finite positive cm values <=1000"; return false;
    }
    error.clear(); return true;
}
bool ArmConfig::load(const std::string &path,ArmConfig &out,std::string &error) {
    std::ifstream in(path.c_str()); if(!in) {error="cannot open config: "+path; return false;}
    ArmConfig c=defaults(); std::map<std::string,std::string> values; std::string line;
    unsigned lineNo=0;
    while(std::getline(in,line)) {
        ++lineNo; line=trim(line); if(line.empty()||line[0]=='#'||line[0]==';')continue;
        const std::size_t eq=line.find('=');
        if(eq==std::string::npos) {error="config line "+std::to_string(lineNo)+": expected key=value";return false;}
        const std::string key=trim(line.substr(0,eq)), value=trim(line.substr(eq+1));
        if(key.empty()||value.empty()||!values.insert(std::make_pair(key,value)).second) {
            error="config line "+std::to_string(lineNo)+": empty or duplicate key"; return false;
        }
    }
    if(!in.eof()) {error="config read error";return false;}
    std::set<std::string> allowed={"format_version","calibration_confirmed","geometry_confirmed"};
    for(unsigned j=0;j<4;++j) allowed.insert("link"+std::to_string(j)+"_cm");
    const char *fields[]={"name","channel","direction","min_angle_deg","max_angle_deg","min_pulse_us","max_pulse_us","offset_us","home_angle_deg","calibrated"};
    for(unsigned i=0;i<AxisCount;++i)for(const char *f:fields)allowed.insert("axis"+std::to_string(i)+"."+f);
    for(const auto &key:allowed)if(!values.count(key)) {error="missing config key: "+key;return false;}
    // Optional as a complete block, so existing version-1 files remain valid.
    const char *frameKeys[]={"origin_enabled","origin_debug_mapping","origin_x_cm",
                            "origin_y_cm","origin_z_cm","origin_pitch_deg"};
    unsigned frameFields=0;
    for(const char *key:frameKeys){allowed.insert(key);if(values.count(key))++frameFields;}
    for(const auto &kv:values)if(!allowed.count(kv.first)) {error="unknown config key: "+kv.first;return false;}
    if(frameFields&&frameFields!=6){error="incomplete Cartesian origin block";return false;}
    if(frameFields) {
        CartesianFrame &frame=c.cartesianFrame;
        if(!boolean(values["origin_enabled"],frame.enabled)||
           !boolean(values["origin_debug_mapping"],frame.debugMapping)||
           !number(values["origin_x_cm"],frame.originCm[0])||
           !number(values["origin_y_cm"],frame.originCm[1])||
           !number(values["origin_z_cm"],frame.originCm[2])||
           !number(values["origin_pitch_deg"],frame.pitchDeg)) {
            error="invalid Cartesian origin block";return false;
        }
    }
    if(values["format_version"]!="1") {error="unsupported config version";return false;}
    if(!boolean(values["calibration_confirmed"],c.calibrationConfirmed)||!boolean(values["geometry_confirmed"],c.geometryConfirmed)) {error="invalid confirmation flag";return false;}
    for(unsigned j=0;j<4;++j)if(!number(values["link"+std::to_string(j)+"_cm"],c.linkLengthsCm[j])) {error="invalid geometry length";return false;}
    for(unsigned i=0;i<AxisCount;++i) {
        AxisConfig &a=c.axes[i]; const std::string p="axis"+std::to_string(i)+".";
        a.name=values[p+"name"]; double ch=0,dir=0;
        if(!number(values[p+"channel"],ch)||ch<0||ch>=AxisCount||std::floor(ch)!=ch ||
           !number(values[p+"direction"],dir)||(dir!=1 && dir!=-1)||
           !number(values[p+"min_angle_deg"],a.minAngleDeg)||!number(values[p+"max_angle_deg"],a.maxAngleDeg)||
           !number(values[p+"min_pulse_us"],a.minPulseUs)||!number(values[p+"max_pulse_us"],a.maxPulseUs)||
           !number(values[p+"offset_us"],a.offsetUs)||!number(values[p+"home_angle_deg"],a.homeAngleDeg)||
           !boolean(values[p+"calibrated"],a.calibrated)) {error=axisError(i,"invalid config value");return false;}
        a.channel=static_cast<unsigned>(ch); a.direction=static_cast<int>(dir);
    }
    if(!c.validate(error))return false;
    out=c; error.clear();return true;
}
bool ArmConfig::save(const std::string &path,std::string &error) const {
    if(!validate(error))return false;
    // Render before touching disk. A unique same-directory file ensures failed
    // writes and failed rename preserve the previous valid configuration.
    std::ostringstream o;
    o.imbue(std::locale::classic()); o << std::setprecision(17);
    o << "# Logical axes: claw, wrist_roll, joint3, joint4, joint5, base.\nformat_version=1\ncalibration_confirmed=" << calibrationConfirmed << "\ngeometry_confirmed=" << geometryConfirmed << '\n';
    for(unsigned j=0;j<4;++j)o << "link" << j << "_cm=" << linkLengthsCm[j] << '\n';
    o << "origin_enabled=" << cartesianFrame.enabled << '\n'
      << "origin_debug_mapping=" << cartesianFrame.debugMapping << '\n'
      << "origin_x_cm=" << cartesianFrame.originCm[0] << '\n'
      << "origin_y_cm=" << cartesianFrame.originCm[1] << '\n'
      << "origin_z_cm=" << cartesianFrame.originCm[2] << '\n'
      << "origin_pitch_deg=" << cartesianFrame.pitchDeg << '\n';
    for(unsigned i=0;i<AxisCount;++i) {
        const AxisConfig &a=axes[i]; const std::string p="axis"+std::to_string(i)+".";
        o << p << "name=" << a.name << '\n' << p << "channel=" << a.channel << '\n' << p << "direction=" << a.direction << '\n'
          << p << "min_angle_deg=" << a.minAngleDeg << '\n' << p << "max_angle_deg=" << a.maxAngleDeg << '\n'
          << p << "min_pulse_us=" << a.minPulseUs << '\n' << p << "max_pulse_us=" << a.maxPulseUs << '\n'
          << p << "offset_us=" << a.offsetUs << '\n' << p << "home_angle_deg=" << a.homeAngleDeg << '\n' << p << "calibrated=" << a.calibrated << '\n';
    }
    static std::atomic<unsigned> counter(0);
    std::string temporary;int fd=-1;
    for(unsigned attempt=0;attempt<100;++attempt) {
#ifdef _WIN32
        temporary=path+".tmp."+std::to_string(_getpid())+"."+std::to_string(counter.fetch_add(1));
        fd=_open(temporary.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
        temporary=path+".tmp."+std::to_string(getpid())+"."+std::to_string(counter.fetch_add(1));
        fd=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
#endif
        if(fd>=0 || errno!=EEXIST)break;
    }
    if(fd<0) {error="cannot create temporary config: "+std::string(std::strerror(errno));return false;}
    const std::string data=o.str();std::size_t offset=0;bool good=true;
    while(offset<data.size()) {
#ifdef _WIN32
        const int count=_write(fd,data.data()+offset,static_cast<unsigned>(data.size()-offset));
#else
        const ssize_t count=write(fd,data.data()+offset,data.size()-offset);
#endif
        if(count<0 && errno==EINTR)continue;
        if(count<=0) {good=false;error="config write failed: "+std::string(std::strerror(errno));break;}
        offset+=static_cast<std::size_t>(count);
    }
#ifdef _WIN32
    if(good && _commit(fd)<0) {good=false;error="config flush failed";}
    if(_close(fd)<0) {good=false;error="config close failed";}
#else
    if(good && fsync(fd)<0) {good=false;error="config flush failed";}
    if(close(fd)<0) {good=false;error="config close failed";}
#endif
    if(good) {
#ifdef _WIN32
        if(!MoveFileExA(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            good=false;error="config replace failed, Windows error "+std::to_string(GetLastError());
        }
#else
        if(std::rename(temporary.c_str(),path.c_str())<0) {good=false;error="config replace failed: "+std::string(std::strerror(errno));}
#endif
    }
    if(!good) {std::remove(temporary.c_str());return false;}
    error.clear();return true;
}
bool ArmConfig::validatePulses(const Pulses &p,std::string &error) const {
    for(unsigned i=0;i<AxisCount;++i)if(!std::isfinite(p[i])||p[i]<axes[i].minPulseUs||p[i]>axes[i].maxPulseUs) {
        error=axisError(i,"pulse outside calibrated limits");return false;
    }
    error.clear();return true;
}
bool ArmConfig::anglesToPulses(const Pose &pose,Pulses &p,std::string &error) const {
    Pulses result;
    for(unsigned i=0;i<AxisCount;++i) {
        const AxisConfig &a=axes[i]; if(!std::isfinite(pose[i])||pose[i]<a.minAngleDeg||pose[i]>a.maxAngleDeg) {
            error=axisError(i,"angle outside calibrated limits");return false;
        }
        result[i]=toUs(a,pose[i]);
    }
    if(!validatePulses(result,error))return false;
    p=result;return true;
}
Pose ArmConfig::pulsesToAngles(const Pulses &p) const {
    Pose pose;
    for(unsigned i=0;i<AxisCount;++i) {
        const AxisConfig &a=axes[i]; double ratio=(p[i]-a.offsetUs-a.minPulseUs)/(a.maxPulseUs-a.minPulseUs);
        if(a.direction<0)ratio=1-ratio;
        pose[i]=a.minAngleDeg+ratio*(a.maxAngleDeg-a.minAngleDeg);
    }
    return pose;
}
Pulses ArmConfig::homePulses() const { Pose angles;for(unsigned i=0;i<AxisCount;++i)angles[i]=axes[i].homeAngleDeg;Pulses p={{0,0,0,0,0,0}};std::string e;anglesToPulses(angles,p,e);return p; }
bool quantizePulse(double us,uint8_t &code,std::string &error) {
    if(!std::isfinite(us)||us<PlMinUs||us>PlMaxUs) {error="pulse outside PL range 500..2499.2 us";return false;}
    code=static_cast<uint8_t>(std::lround((us-PlMinUs)/PlStepUs));error.clear();return true;
}
double decodePulse(uint8_t code) {return PlMinUs+PlStepUs*code;}
CommandWords encodeCommand(const ChannelBytes &c,bool enabled,bool commit) {
    CommandWords w;
    w.low=static_cast<uint32_t>(c[0])|(static_cast<uint32_t>(c[1])<<8)|(static_cast<uint32_t>(c[2])<<16)|(static_cast<uint32_t>(c[3])<<24);
    w.high=0xA6000000u|static_cast<uint32_t>(c[4])|(static_cast<uint32_t>(c[5])<<8)|(enabled ? 0x10000u:0)|(commit ? 0x20000u:0);
    return w;
}
MockBackend::MockBackend():failNext(false),commit_(false) {}
bool MockBackend::initialize(std::string &error) {commit_=false; error.clear();return true;}
bool MockBackend::send(const ChannelBytes &c,bool enabled,std::string &error) {
    if(failNext) {failNext=false;error="injected backend failure";return false;}
    commit_=!commit_;Record r;r.values=c;r.enabled=enabled;r.words=encodeCommand(c,enabled,commit_);records.push_back(r);error.clear();return true;
}
Controller::Controller(Backend &backend,const ArmConfig &config):backend_(backend),config_(config),initialized_(false),baseline_(false),enabled_(false),moving_(false),hasTick_(false),calibrationOnly_(false),pulseDebug_(false),holdingAfterTimeout_(false),startMs_(0),durationMs_(0),lastTickMs_(0) {
    current_.fill(1500);start_=target_=current_;
    debugMin_.fill(0);debugMax_.fill(0);
}
bool Controller::initialize() {
    if(initialized_)return fail("already initialized");
    if(!config_.validate(error_))return false;
    if(!backend_.initialize(error_))return false;
    initialized_=true; baseline_=false;enabled_=false;moving_=false;hasTick_=false;
    if(backend_.requiresRearm()) {
        holdAfterTimeout("connection locked; existing PWM preserved; explicitly disable before re-enabling");
        return true;
    }
    // Disabled bytes are placeholders, not a declared physical pose.
    return transmit(current_,false);
}
bool Controller::acknowledgeInitialPulses(const Pulses &p,bool explicitConfirmed) {
    if(!initialized_)return fail("initialize first");
    if(enabled_)return fail("disable before acknowledging an initial pose");
    if(!explicitConfirmed)return fail("all six initial pulses require explicit confirmation; PL enables all axes together");
    if(!config_.validatePulses(p,error_))return fail(error_);
    current_=start_=target_=p;baseline_=true;moving_=false;hasTick_=false;calibrationOnly_=false;pulseDebug_=false;error_.clear();return true;
}
bool Controller::enable() {
    if(holdingAfterTimeout_)return holdAfterTimeout("link timed out; holding last pose; explicitly disable before re-enabling");
    if(pulseDebug_)return fail("disable pulse debugging before normal enabling");
    if(!initialized_||!baseline_)return fail("initial six-axis pose must be explicitly acknowledged before enabling");
    if(backend_.hardware()) {
        if(!config_.calibrationConfirmed)return fail("hardware calibration is not confirmed");
        for(const AxisConfig &a:config_.axes)if(!a.calibrated)return fail("all six axes must be calibrated before normal hardware motion");
    }
    if(!transmit(current_,true))return false;
    enabled_=true;calibrationOnly_=false;error_.clear();return true;
}
bool Controller::enablePulseDebug(bool explicitConfirmed) {
    if(holdingAfterTimeout_)return holdAfterTimeout("link timed out; holding last pose; explicitly disable before re-enabling");
    if(!initialized_||!baseline_)return fail("acknowledge all six initial pulses before pulse debugging");
    if(enabled_)return fail("disable before starting a new pulse debug session");
    if(!explicitConfirmed)return fail("pulse debugging requires explicit confirmation that all six axes will be enabled");
    for(unsigned i=0;i<AxisCount;++i) {
        debugMin_[i]=PlMinUs;
        debugMax_[i]=PlMaxUs;
    }
    pulseDebug_=true;calibrationOnly_=false;
    if(!transmit(current_,true))return false;
    enabled_=true;error_.clear();return true;
}
bool Controller::enablePulseDebug(const Pulses &p,bool explicitConfirmed) {
    if(holdingAfterTimeout_)return holdAfterTimeout("link timed out; holding last pose; explicitly disable before re-enabling");
    if(!initialized_)return fail("initialize before pulse debugging");
    if(enabled_)return fail("disable before starting a new pulse debug session");
    if(!explicitConfirmed)return fail("pulse debugging requires explicit confirmation that all six axes will be enabled");
    for(unsigned i=0;i<AxisCount;++i)if(!std::isfinite(p[i])||p[i]<PlMinUs||p[i]>PlMaxUs)
        return fail(axisError(i,"pulse outside PL range 500..2499.2 us"));
    const Pulses previous=current_,previousStart=start_,previousTarget=target_;
    current_=start_=target_=p;baseline_=true;moving_=false;hasTick_=false;
    if(enablePulseDebug(true))return true;
    // A rejected/failed enable did not apply this baseline to the hardware.
    current_=previous;start_=previousStart;target_=previousTarget;
    return false;
}
ArmConfig Controller::effectiveMotionConfig() const {
    ArmConfig result=config_;
    if(pulseDebug_)for(AxisConfig &axis:result.axes) {
        axis.minPulseUs=PlMinUs;axis.maxPulseUs=PlMaxUs;
        axis.minAngleDeg=0;axis.maxAngleDeg=180;axis.offsetUs=0;
        axis.homeAngleDeg=std::max(0.0,std::min(180.0,axis.homeAngleDeg));
    }
    return result;
}
bool Controller::moveDebugPulses(const Pulses &p,uint64_t durationMs,uint64_t nowMs) {
    if(!enabled_||!pulseDebug_)return fail("enable an explicitly confirmed pulse debug session before moving");
    return movePulses(p,durationMs,nowMs);
}
bool Controller::transmit(const Pulses &p,bool enabled) {
    ChannelBytes bytes; bytes.fill(128);
    if(enabled)for(unsigned i=0;i<AxisCount;++i) {
        uint8_t code;std::string e;if(!quantizePulse(p[i],code,e))return fail(e);
        // Choose the nearest representable code inside calibrated physical limits.
        const double minUs=pulseDebug_?debugMin_[i]:config_.axes[i].minPulseUs;
        const double maxUs=pulseDebug_?debugMax_[i]:config_.axes[i].maxPulseUs;
        const int lo=static_cast<int>(std::ceil((minUs-PlMinUs)/PlStepUs-1e-9));
        const int hi=static_cast<int>(std::floor((maxUs-PlMinUs)/PlStepUs+1e-9));
        bytes[config_.axes[i].channel]=static_cast<uint8_t>(std::max(lo,std::min(hi,static_cast<int>(code))));
    }
    std::string e;if(!backend_.send(bytes,enabled,e)) {
        if(backend_.requiresRearm())return holdAfterTimeout(e);
        return fail(e);
    }
    return true;
}
bool Controller::holdAfterTimeout(const std::string &message) {
    moving_=false;enabled_=false;baseline_=false;hasTick_=false;
    calibrationOnly_=false;pulseDebug_=false;holdingAfterTimeout_=true;
    error_=message;return false;
}
bool Controller::fail(const std::string &message) {
    if(holdingAfterTimeout_)return holdAfterTimeout(message+"; PWM held, disable/re-enable required");
    // A validation error can arrive before the next timer tick after a stall.
    // Check transport health before an old cleanup path can disable held PWM.
    if(initialized_&&backend_.hardware()) {
        std::string health;
        if(!backend_.keepAlive(health)&&backend_.requiresRearm())
            return holdAfterTimeout(message+"; "+health);
    }
    const std::string reason=message;moving_=false;enabled_=false;baseline_=false;hasTick_=false;calibrationOnly_=false;pulseDebug_=false;
    if(initialized_) { ChannelBytes c;c.fill(128);std::string disableError;if(!backend_.send(c,false,disableError))error_=reason+"; disable failed: "+disableError;else error_=reason; }
    else error_=reason;
    return false;
}
bool Controller::schedule(const Pulses &p,uint64_t durationMs,uint64_t nowMs) {
    if(!enabled_)return fail("enable before moving");
    if(hasTick_ && nowMs<lastTickMs_)return fail("nonmonotonic movement timestamp");
    if(durationMs>3600000)return fail("motion duration exceeds 1 hour");
    start_=current_;target_=p;startMs_=nowMs;durationMs_=std::max<uint64_t>(20,durationMs);
    lastTickMs_=nowMs;hasTick_=true;moving_=true;error_.clear();return true;
}
bool Controller::moveAngles(const Pose &pose,uint64_t durationMs,uint64_t nowMs) {
    Pulses p;if(!effectiveMotionConfig().anglesToPulses(pose,p,error_))return fail(error_);return movePulses(p,durationMs,nowMs);
}
bool Controller::movePulses(const Pulses &p,uint64_t durationMs,uint64_t nowMs) {
    if(backend_.hardware() && calibrationOnly_ && !pulseDebug_)return fail("normal movement requires confirmed calibration; calibration session permits calibrateAxis only");
    if(!effectiveMotionConfig().validatePulses(p,error_))return fail(error_);
    return schedule(p,durationMs,nowMs);
}
bool Controller::calibrateAxis(unsigned axis,double us,uint64_t durationMs,uint64_t nowMs,bool explicitConfirmed) {
    if(!explicitConfirmed)return fail("calibration test needs explicit confirmation that all six axes will be enabled");
    if(axis>=AxisCount)return fail("axis outside 0..5");
    if(!initialized_||!baseline_)return fail("confirm the initial six-axis pulses before calibration");
    Pulses p=current_;p[axis]=us;if(!effectiveMotionConfig().validatePulses(p,error_))return fail(error_);
    if(!enabled_) {if(!transmit(current_,true))return false;enabled_=true;}
    calibrationOnly_=!pulseDebug_;
    return schedule(p,durationMs,nowMs);
}
bool Controller::tick(uint64_t nowMs) {
    if(holdingAfterTimeout_) {
        std::string ignored;backend_.keepAlive(ignored);
        return true; // Keep status fresh without clearing the latched fault.
    }
    {
        std::string error;
        if(!backend_.keepAlive(error)) {
            if(backend_.requiresRearm())return holdAfterTimeout(error);
            return fail(error);
        }
        if(backend_.requiresRearm())return holdAfterTimeout("FPGA requires explicit rearm");
    }
    if(!moving_)return true;
    if(nowMs<lastTickMs_||nowMs<startMs_)return fail("nonmonotonic tick timestamp");
    if(nowMs-lastTickMs_<20)return true;
    const uint64_t elapsed=nowMs-startMs_;const double f=std::min(1.0,static_cast<double>(elapsed)/durationMs_);
    Pulses p;for(unsigned i=0;i<AxisCount;++i)p[i]=start_[i]+(target_[i]-start_[i])*f;
    if(!transmit(p,true))return false;
    current_=p;lastTickMs_=nowMs;if(elapsed>=durationMs_)moving_=false;return true;
}
bool Controller::stop() {moving_=false;target_=current_;if(holdingAfterTimeout_)return false;error_.clear();return true;}
bool Controller::disable() {
    moving_=false;enabled_=false;baseline_=false;hasTick_=false;calibrationOnly_=false;pulseDebug_=false;
    if(!initialized_) {error_="initialize first";return false;}
    if(!transmit(current_,false))return false;
    holdingAfterTimeout_=false;error_.clear();return true;
}
}
