#ifndef CONVEYOR_HPP
#define CONVEYOR_HPP
#include <cstdint>
#include <memory>
#include <string>

namespace conveyor {
constexpr uint32_t RegisterId = 0x434e5601u;
enum Register : uint32_t { Id=0, Period=4, Duty=8, Control=12, Apply=16,
    Status=20, Watchdog=24, ActiveDuty=28, ClockHz=32, EncoderCount=36, EncoderErrors=40,
    Features=44, BenchAB=48, RunLimitMs=52 };
constexpr uint32_t FaultMask = 0x700u;
class Registers {
public:
    virtual ~Registers() {}
    virtual void setNow(uint64_t) {}
    virtual uint32_t read(uint32_t offset)=0;
    virtual void write(uint32_t offset,uint32_t value)=0;
};
// Offline register model; use RTL simulation to verify AXI timing and PWM edges.
class MockRegisters : public Registers {
public:
    MockRegisters();
    void setNow(uint64_t now) override;
    uint32_t read(uint32_t offset) override;
    void write(uint32_t offset,uint32_t value) override;
    void injectFault(bool active);
    void advanceEncoder(int32_t delta,bool invalidTransition=false);
private:
    uint64_t now_=0, last_=0;
    uint32_t duty_=0, control_=0, active_=0, faults_=0;
    uint32_t encoder_=0, encoderErrors_=0;
    bool enabled_=false, external_=false;
};
// Linux UIO map0, expected UIO name "conveyor". No /dev/mem or guessed address.
std::unique_ptr<Registers> openUio(const std::string &device);

class MotorDriver {
public:
    explicit MotorDriver(Registers &registers);
    ~MotorDriver();
    void initialize(uint64_t now);
    void clearFault(uint64_t now);
    void command(double dutyFraction,bool reverse,uint64_t now);
    uint32_t status(uint64_t now);
    uint32_t encoderCount();
    uint32_t encoderErrors();
    uint32_t features();
    void benchAB(unsigned ab);
    void stop(uint64_t now);
    MotorDriver(const MotorDriver &)=delete;
    MotorDriver &operator=(const MotorDriver &)=delete;
private:
    void commit(uint32_t duty,uint32_t control);
    Registers &registers_;
    uint32_t period_=0;
    uint64_t lastNow_=0;
};

struct Config {
    // Unit is selected by the input producer: all spatial values must agree.
    // Defaults and the supplied .ini are synthetic pixel examples, not calibration.
    double originX=0, originY=0, axisX=1, axisY=0;
    double target=500, lateralMin=-100, lateralMax=100;
    double tolerance=3, slowDistance=120, maxDuty=0.45, minDuty=0.12;
    double leadMs=80, stableSpeed=4;
    uint64_t visionTimeoutMs=400, stableMs=400, maxRunMs=15000;
    unsigned stableFrames=3;
    // Set cmPerCount from measured belt travel / PL x4 counts, or from gearing.
    // encoderEnabled=0 is duty-only bench simulation; hardware CLI requires 1.
    uint64_t encoderEnabled=0, motorReverse=0, stallMs=1000;
    double cmPerCount=0, encoderSign=1;
    double maxSpeed=3, minSpeed=0.3, speedKp=0.08, speedKi=0.12;
    double speedFilterMs=80, stallMinSpeed=0.05, maxEncoderSpeed=30;
    void validate() const;
};
Config loadConfig(const std::string &path);
struct Observation {
    uint64_t objectId, sequence, captureMs;
    double x,y;
    bool valid;
    Observation(uint64_t id,uint64_t seq,uint64_t ms,double px,double py,bool ok=true)
        :objectId(id),sequence(seq),captureMs(ms),x(px),y(py),valid(ok) {}
};
enum class State { Idle, Waiting, Feeding, Tracking, Settling, Holding, Picking, Fault };
const char *stateName(State state);
struct Snapshot {
    State state=State::Idle;
    double along=0, lateral=0, velocity=0, duty=0;
    double targetSpeed=0, measuredSpeed=0;
    uint64_t objectId=0;
    std::string reason;
    // Holding means optically stable at the stop line, not a mechanical brake.
    bool ready=false;
};
class Controller {
public:
    Controller(MotorDriver &driver,const Config &config);
    void arm(uint64_t now);
    void feed(uint64_t now); // explicit initial feeding before an object is visible
    void observe(const Observation &observation,uint64_t now);
    void tick(uint64_t now); // call every ~20 ms even without new camera data
    void stop(uint64_t now);
    void release(uint64_t now); // arm/robot explicitly releases a held object
    void beginPick(uint64_t now); // belt disabled; camera may move with arm
    void resetFault(uint64_t now); // leaves Idle; requires a separate arm()
    const Snapshot &snapshot() const { return snapshot_; }
private:
    void fault(const std::string &reason,uint64_t now);
    void send(double duty,uint64_t now);
    void requestMotion(double ratio,uint64_t now);
    void speedTick(uint64_t now);
    bool checkTime(uint64_t now);
    MotorDriver &driver_;
    Config config_;
    Snapshot snapshot_;
    bool clockSeen_=false, haveFrame_=false, blocked_=false, haveObject_=false;
    uint64_t clock_=0, lastSequence_=0, lastCapture_=0, started_=0;
    uint64_t stableStart_=0, blockedId_=0;
    unsigned stableCount_=0;
    bool haveEncoder_=false, stallPending_=false;
    uint32_t encoderCount_=0, encoderErrors_=0;
    uint32_t heldEncoderCount_=0;
    uint64_t encoderMs_=0, stallStart_=0;
    double integral_=0;
};
}
#endif
