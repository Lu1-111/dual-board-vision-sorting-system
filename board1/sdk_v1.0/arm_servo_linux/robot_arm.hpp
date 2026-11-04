#ifndef ARM_ROBOT_ARM_HPP
#define ARM_ROBOT_ARM_HPP
#include "arm_control.hpp"
#include "kinematics.hpp"
#include <cstdint>
#include <string>

namespace arm {
// XY is in the same translated frame as moveCoordinate(); height is fixed.
// A strict pitch is the default. Search must be requested explicitly.
struct XYPlane {
    double heightCm, pitchDeg;
    bool allowPitchSearch;
    XYPlane(double z, double pitch, bool search=false)
        : heightCm(z), pitchDeg(pitch), allowPitchSearch(search) {}
};
class RobotArm {
public:
    // Simulation hint is kept for callers; actual Backend::hardware() controls
    // safety interlocks, so passing true cannot bypass hardware confirmation.
    explicit RobotArm(Controller& controller, bool simulation=false);
    RobotArm(Controller& controller, const Geometry& geometry, bool simulation=false);
    IKResult previewCoordinate(const CartesianPose& target,
                               const IKOptions& options=IKOptions()) const;
    bool moveCoordinate(const CartesianPose& target, unsigned durationMs,
                        std::uint64_t nowMs, const IKOptions& options=IKOptions());
    IKResult previewXY(double xCm, double yCm, const XYPlane& plane,
                       const IKOptions& options=IKOptions()) const;
    bool moveXY(double xCm, double yCm, const XYPlane& plane, unsigned durationMs,
                std::uint64_t nowMs, const IKOptions& options=IKOptions());
    bool moveCoordinateWithTools(const CartesianPose& target, double clawServoDeg,
                        double rollServoDeg, unsigned durationMs, std::uint64_t nowMs,
                        const IKOptions& options=IKOptions());
    bool moveClaw(double openDeg, unsigned durationMs, std::uint64_t nowMs);
    bool moveRoll(double rollDeg, unsigned durationMs, std::uint64_t nowMs);
    bool home(unsigned durationMs, std::uint64_t nowMs);
    void stop();
    // Capturing or changing the frame never issues a motor command.
    bool zeroAtCurrentPose();
    bool setCartesianFrame(const CartesianFrame& frame);
    const CartesianFrame& cartesianFrame() const { return frame_; }
    bool cartesianFrameCompatible() const;
    CartesianPose currentBaseCartesian() const;
    CartesianPose currentCartesian() const;
    const std::string& error() const { return error_; }
    const Geometry& geometry() const { return geometry_; }
private:
    bool move(const Pose& pose, unsigned durationMs, std::uint64_t nowMs);
    bool coordinateMotion(const CartesianPose& target,unsigned durationMs,std::uint64_t nowMs,
                          const IKOptions& options,const std::array<double,2>* tools);
    Controller& controller_;
    Geometry geometry_;
    CartesianFrame frame_;
    std::string error_;
};
} // namespace arm
#endif
