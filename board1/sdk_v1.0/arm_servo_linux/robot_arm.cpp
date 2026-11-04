#include "robot_arm.hpp"
#include <cmath>
#include <limits>

namespace arm {
RobotArm::RobotArm(Controller& c,bool /*simulation*/):controller_(c),
    geometry_(c.config().linkLengthsCm[0],c.config().linkLengthsCm[1],
              c.config().linkLengthsCm[2],c.config().linkLengthsCm[3]),
    frame_(c.config().cartesianFrame) {}
RobotArm::RobotArm(Controller& c,const Geometry& g,bool /*simulation*/):
    controller_(c),geometry_(g),frame_(c.config().cartesianFrame) {}
IKResult RobotArm::previewCoordinate(const CartesianPose& target,const IKOptions& options) const {
    IKOptions seeded=options;
    if(!seeded.useSeed) {
        Pose angles=controller_.currentAngles();
        if(frame_.enabled&&frame_.debugMapping&&!controller_.pulseDebugEnabled()) {
            // Read-only previews can use the recorded debug model before PWM
            // is enabled. Actual motion still requires a matching live mode.
            ArmConfig mapping=controller_.config();
            for(auto &axis:mapping.axes){axis.minAngleDeg=0;axis.maxAngleDeg=180;
                axis.minPulseUs=PlMinUs;axis.maxPulseUs=PlMaxUs;axis.offsetUs=0;}
            angles=mapping.pulsesToAngles(controller_.currentPulses());
        } else if(frame_.enabled&&!frame_.debugMapping&&controller_.pulseDebugEnabled()) {
            angles=controller_.config().pulsesToAngles(controller_.currentPulses());
        }
        seeded.seed=servoAnglesToJoints(angles);
        seeded.useSeed=jointsInLimits(seeded.seed);
    }
    CartesianPose base=target;
    if(frame_.enabled) {
        base.xCm+=frame_.originCm[0];base.yCm+=frame_.originCm[1];base.zCm+=frame_.originCm[2];
    }
    // Preserve the recorded pitch even when it lies outside the default pitch
    // search interval. Relaxation remains available if the exact pose fails.
    IKResult result;
    if(frame_.enabled)result=inverseKinematics(base,geometry_,seeded);
    if(!result.ok)result=inverseKinematicsPitchRange(base,geometry_,seeded);
    if(result.ok&&frame_.enabled) {
        result.achieved.xCm-=frame_.originCm[0];result.achieved.yCm-=frame_.originCm[1];
        result.achieved.zCm-=frame_.originCm[2];
    }
    return result;
}
bool RobotArm::move(const Pose& pose,unsigned durationMs,std::uint64_t nowMs) {
    if(!controller_.moveAngles(pose,durationMs,nowMs)) {
        error_=controller_.error(); return false;
    }
    error_.clear(); return true;
}
bool RobotArm::moveCoordinate(const CartesianPose& target,unsigned durationMs,
                             std::uint64_t nowMs,const IKOptions& options) {
    return coordinateMotion(target,durationMs,nowMs,options,nullptr);
}
namespace {
IKOptions xyOptions(const XYPlane& plane,const IKOptions& options) {
    IKOptions result=options;
    if(!plane.allowPitchSearch)result.minPitchDeg=result.maxPitchDeg=plane.pitchDeg;
    return result;
}
}
IKResult RobotArm::previewXY(double xCm,double yCm,const XYPlane& plane,const IKOptions& options) const {
    return previewCoordinate(CartesianPose(xCm,yCm,plane.heightCm,plane.pitchDeg),xyOptions(plane,options));
}
bool RobotArm::moveXY(double xCm,double yCm,const XYPlane& plane,unsigned durationMs,
                      std::uint64_t nowMs,const IKOptions& options) {
    return moveCoordinate(CartesianPose(xCm,yCm,plane.heightCm,plane.pitchDeg),durationMs,nowMs,
                          xyOptions(plane,options));
}
bool RobotArm::moveCoordinateWithTools(const CartesianPose& target,double clawServoDeg,
                         double rollServoDeg,unsigned durationMs,std::uint64_t nowMs,const IKOptions& options) {
    const std::array<double,2> tools={{clawServoDeg,rollServoDeg}};
    return coordinateMotion(target,durationMs,nowMs,options,&tools);
}
bool RobotArm::coordinateMotion(const CartesianPose& target,unsigned durationMs,
                         std::uint64_t nowMs,const IKOptions& options,const std::array<double,2>* tools) {
    if(!cartesianFrameCompatible()) {
        error_="Cartesian origin was recorded with a different angle mapping; use its mode or record a new origin";
        return false;
    }
    if(controller_.hardware()&&!controller_.pulseDebugEnabled()&&!controller_.config().geometryConfirmed) {
        error_="measure link geometry and confirm geometryConfirmed before hardware coordinate motion";
        return false;
    }
    // A caller-supplied Geometry must match the geometry actually confirmed in
    // the hardware config; simulation may freely experiment with dimensions.
    if(controller_.hardware()) {
        const std::array<double,4>& l=controller_.config().linkLengthsCm;
        if(std::fabs(geometry_.baseHeightCm-l[0])>1e-9||
           std::fabs(geometry_.upperArmCm-l[1])>1e-9||
           std::fabs(geometry_.forearmCm-l[2])>1e-9||std::fabs(geometry_.toolCm-l[3])>1e-9) {
            error_="requested geometry differs from confirmed hardware geometry"; return false;
        }
    }
    IKResult r=previewCoordinate(target,options);
    if(!r.ok) { error_=r.error; return false; }
    Pose pose=jointsToServoAngles(r.joints,controller_.currentAngles());
    if(tools){pose[0]=(*tools)[0];pose[1]=(*tools)[1];}
    return move(pose,durationMs,nowMs);
}
bool RobotArm::moveClaw(double open,unsigned durationMs,std::uint64_t nowMs) {
    if(!std::isfinite(open)||open<0||open>90) {
        error_="claw opening must be finite and between 0 and 90 degrees"; return false;
    }
    Pose pose=controller_.currentAngles(); pose[0]=90-open;
    return move(pose,durationMs,nowMs);
}
bool RobotArm::moveRoll(double roll,unsigned durationMs,std::uint64_t nowMs) {
    if(!std::isfinite(roll)||roll< -90||roll>90) {
        error_="wrist roll must be finite and between -90 and 90 degrees"; return false;
    }
    Pose pose=controller_.currentAngles(); pose[1]=90-roll;
    return move(pose,durationMs,nowMs);
}
bool RobotArm::home(unsigned durationMs,std::uint64_t nowMs) {
    Pose pose;
    for(std::size_t i=0;i<AxisCount;++i) pose[i]=controller_.config().axes[i].homeAngleDeg;
    return move(pose,durationMs,nowMs);
}
void RobotArm::stop() {
    if(!controller_.stop()) error_=controller_.error(); else error_.clear();
}
bool RobotArm::cartesianFrameCompatible() const {
    return !frame_.enabled||frame_.debugMapping==controller_.pulseDebugEnabled();
}
bool RobotArm::setCartesianFrame(const CartesianFrame& frame) {
    if(controller_.moving()){error_="stop movement before changing the Cartesian origin";return false;}
    if(!frame.valid()){error_="invalid Cartesian origin";return false;}
    frame_=frame;error_.clear();return true;
}
bool RobotArm::zeroAtCurrentPose() {
    if(!controller_.enabled()||controller_.moving()) {
        error_="enable and hold a stationary command before recording the Cartesian origin";return false;
    }
    CartesianPose base=currentBaseCartesian();
    CartesianFrame frame;frame.enabled=true;frame.debugMapping=controller_.pulseDebugEnabled();
    frame.originCm={{base.xCm,base.yCm,base.zCm}};frame.pitchDeg=base.pitchDeg;
    return setCartesianFrame(frame);
}
CartesianPose RobotArm::currentBaseCartesian() const {
    return forwardKinematics(servoAnglesToJoints(controller_.currentAngles()),geometry_);
}
CartesianPose RobotArm::currentCartesian() const {
    if(!cartesianFrameCompatible()) {
        const double n=std::numeric_limits<double>::quiet_NaN();return CartesianPose(n,n,n,n);
    }
    CartesianPose pose=currentBaseCartesian();
    if(frame_.enabled) {
        pose.xCm-=frame_.originCm[0];pose.yCm-=frame_.originCm[1];pose.zCm-=frame_.originCm[2];
    }
    return pose;
}
} // namespace arm
