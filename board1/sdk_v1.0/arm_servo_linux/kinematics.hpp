#ifndef ARM_KINEMATICS_HPP
#define ARM_KINEMATICS_HPP

#include <array>
#include <string>

namespace arm {

// cm, from bottom to tip. Reference: Hiwonder/Arm/lnc/kinematics.h.
struct Geometry {
    double baseHeightCm, upperArmCm, forearmCm, toolCm;
    Geometry(double h=2.89, double u=10.43, double f=8.9, double t=17.7)
        : baseHeightCm(h), upperArmCm(u), forearmCm(f), toolCm(t) {}
    bool valid() const;
};

// Same convention as the original compiler listing: yaw is about +Z;
// shoulder is above the horizontal; elbow/wrist are relative link angles.
// +X is forward, +Y is left. pitch = shoulder + elbow + wristPitch.
struct JointAngles {
    double yawDeg, shoulderDeg, elbowDeg, wristPitchDeg;
    JointAngles(double y=0, double s=90, double e=0, double w=0)
        : yawDeg(y), shoulderDeg(s), elbowDeg(e), wristPitchDeg(w) {}
};
struct CartesianPose {
    double xCm, yCm, zCm, pitchDeg;
    CartesianPose(double x=0,double y=0,double z=0,double p=0)
        : xCm(x), yCm(y), zCm(z), pitchDeg(p) {}
};
enum class IKStatus { Ok, InvalidGeometry, NonFinite, InvalidOptions,
                      Unreachable, JointLimits, Singular, VerificationFailed };
struct IKOptions {
    double minPitchDeg, maxPitchDeg, pitchStepDeg;
    double positionToleranceCm, pitchToleranceDeg;
    bool useSeed;
    JointAngles seed;
    IKOptions();
};
struct IKResult {
    bool ok, singular;
    IKStatus status;
    JointAngles joints;
    CartesianPose achieved;
    double positionErrorCm, pitchErrorDeg;
    std::string error;
    IKResult();
};

bool jointsInLimits(const JointAngles& joints);
CartesianPose forwardKinematics(const JointAngles& joints,
                                const Geometry& geometry=Geometry());
// Exact pitch, both elbow branches. Yaw on the Z axis is underdetermined:
// provide useSeed=true to keep a chosen base orientation there.
IKResult inverseKinematics(const CartesianPose& target,
                           const Geometry& geometry=Geometry(),
                           const IKOptions& options=IKOptions());
// Preferred pitch is attempted first, then range searched at pitchStepDeg;
// nearest sampled feasible boundary is refined by bisection. A narrower
// feasible interval than pitchStepDeg may be missed. Every candidate is
// checked by forward kinematics. Not a collision or trajectory planner.
IKResult inverseKinematicsPitchRange(const CartesianPose& target,
                                     const Geometry& geometry=Geometry(),
                                     const IKOptions& options=IKOptions());
std::array<double,6> jointsToServoAngles(const JointAngles& joints,
                                       const std::array<double,6>& existing);
JointAngles servoAnglesToJoints(const std::array<double,6>& servoAngles);
const char* ikStatusName(IKStatus status);

} // namespace arm
#endif
