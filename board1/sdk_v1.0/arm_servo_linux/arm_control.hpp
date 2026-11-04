#ifndef ARM_CONTROL_HPP
#define ARM_CONTROL_HPP
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace arm {
const std::size_t AxisCount = 6;
typedef std::array<double, AxisCount> Pose;
typedef std::array<double, AxisCount> Pulses;
typedef std::array<uint8_t, AxisCount> ChannelBytes;
const double PlMinUs = 500.0;
const double PlStepUs = 7.84;
const double PlMaxUs = 2499.2;
struct AxisConfig {
    std::string name;
    unsigned channel;
    int direction;
    double minAngleDeg, maxAngleDeg, minPulseUs, maxPulseUs, offsetUs, homeAngleDeg;
    bool calibrated;
};
// A translated Cartesian frame: axes retain the base-frame directions.
// The reference is command-derived, not measured position feedback.
struct CartesianFrame {
    bool enabled, debugMapping;
    std::array<double,3> originCm;
    double pitchDeg;
    CartesianFrame():enabled(false),debugMapping(false),originCm{{0,0,0}},pitchDeg(0) {}
    bool valid() const;
};
struct ArmConfig {
    std::array<AxisConfig, AxisCount> axes;
    bool calibrationConfirmed;
    bool geometryConfirmed;
    std::array<double,4> linkLengthsCm;
    CartesianFrame cartesianFrame;
    static ArmConfig defaults();
    bool validate(std::string &error) const;
    static bool load(const std::string &path, ArmConfig &out, std::string &error);
    bool save(const std::string &path, std::string &error) const;
    bool anglesToPulses(const Pose &, Pulses &, std::string &error) const;
    Pose pulsesToAngles(const Pulses &) const;
    bool validatePulses(const Pulses &, std::string &error) const;
    Pulses homePulses() const;
};
bool quantizePulse(double us, uint8_t &code, std::string &error);
double decodePulse(uint8_t code);
struct CommandWords { uint32_t low, high; };
CommandWords encodeCommand(const ChannelBytes &, bool enabled, bool commit);
class Backend {
public:
    virtual ~Backend() {}
    virtual bool initialize(std::string &error) = 0;
    virtual bool send(const ChannelBytes &, bool enabled, std::string &error) = 0;
    virtual bool hardware() const = 0;
    virtual bool keepAlive(std::string &error) { error.clear(); return true; }
    virtual bool requiresRearm() const { return false; }
    virtual bool statusAvailable() const { return false; }
    virtual bool outputActive() const { return false; }
};
class MockBackend : public Backend {
public:
    struct Record { ChannelBytes values; bool enabled; CommandWords words; };
    MockBackend();
    bool initialize(std::string &error);
    bool send(const ChannelBytes &, bool enabled, std::string &error);
    bool hardware() const { return false; }
    std::vector<Record> records;
    // Fail the next send; allows proving that the controller tries to disable.
    bool failNext;
private: bool commit_;
};
// Single-threaded; dispatch tick() from a monotonic 20 ms timer.
// current* reports the last commanded interpolation, never measured feedback.
class Controller {
public:
    Controller(Backend &, const ArmConfig &);
    bool initialize();
    bool acknowledgeInitialPulses(const Pulses &, bool explicitConfirmed);
    bool enable();
    // Explicit, nonpersistent pulse-only session. All six channels are enabled.
    // Full PL pulse range on all axes; configuration confirmations remain unchanged.
    bool enablePulseDebug(bool explicitConfirmed);
    bool enablePulseDebug(const Pulses &baseline, bool explicitConfirmed);
    bool moveDebugPulses(const Pulses &, uint64_t durationMs, uint64_t nowMs);
    bool moveAngles(const Pose &, uint64_t durationMs, uint64_t nowMs);
    bool movePulses(const Pulses &, uint64_t durationMs, uint64_t nowMs);
    bool calibrateAxis(unsigned axis, double pulseUs, uint64_t durationMs,
                       uint64_t nowMs, bool explicitConfirmed);
    bool tick(uint64_t nowMs);
    bool stop(); // Hold last transmitted command, cancel interpolation.
    bool disable(); // PL applies disable at its next frame boundary (up to 20 ms).
    bool enabled() const { return enabled_; }
    bool hardware() const { return backend_.hardware(); }
    bool moving() const { return moving_; }
    bool initialPoseAcknowledged() const { return baseline_; }
    bool pulseDebugEnabled() const { return pulseDebug_; }
    bool holdingAfterTimeout() const { return holdingAfterTimeout_; }
    bool hardwareStatusAvailable() const { return backend_.statusAvailable(); }
    bool hardwareOutputActive() const { return backend_.outputActive(); }
    const Pulses &debugMinPulses() const { return debugMin_; }
    const Pulses &debugMaxPulses() const { return debugMax_; }
    const Pulses &currentPulses() const { return current_; }
    const Pulses &targetPulses() const { return target_; }
    ArmConfig motionConfig() const { return effectiveMotionConfig(); }
    Pose currentAngles() const { return effectiveMotionConfig().pulsesToAngles(current_); }
    Pulses homePulses() const { return effectiveMotionConfig().homePulses(); }
    const ArmConfig &config() const { return config_; }
    const std::string &error() const { return error_; }
private:
    bool transmit(const Pulses &, bool enable);
    bool fail(const std::string &);
    bool holdAfterTimeout(const std::string &);
    bool schedule(const Pulses &, uint64_t durationMs, uint64_t nowMs);
    ArmConfig effectiveMotionConfig() const;
    Backend &backend_;
    ArmConfig config_;
    Pulses current_, start_, target_, debugMin_, debugMax_;
    bool initialized_, baseline_, enabled_, moving_, hasTick_, calibrationOnly_, pulseDebug_;
    bool holdingAfterTimeout_;
    uint64_t startMs_, durationMs_, lastTickMs_;
    std::string error_;
};
}
#endif
