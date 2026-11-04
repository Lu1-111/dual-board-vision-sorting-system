#include "kinematics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace arm {
namespace {
const double pi=3.14159265358979323846;
const double eps=1e-9;
double rad(double d) { return d*pi/180.0; }
double deg(double r) { return r*180.0/pi; }
bool finite(const JointAngles& j) {
    return std::isfinite(j.yawDeg)&&std::isfinite(j.shoulderDeg)&&
           std::isfinite(j.elbowDeg)&&std::isfinite(j.wristPitchDeg);
}
bool finite(const CartesianPose& p) {
    return std::isfinite(p.xCm)&&std::isfinite(p.yCm)&&
           std::isfinite(p.zCm)&&std::isfinite(p.pitchDeg);
}
IKResult failure(IKStatus status, const char* error) {
    IKResult r; r.status=status; r.error=error; return r;
}
bool validOptions(const IKOptions& o) {
    return std::isfinite(o.minPitchDeg)&&std::isfinite(o.maxPitchDeg)&&
        o.minPitchDeg<=o.maxPitchDeg&&o.minPitchDeg>=-180&&o.maxPitchDeg<=360&&
        std::isfinite(o.pitchStepDeg)&&o.pitchStepDeg>=0.01&&o.pitchStepDeg<=10&&
        std::isfinite(o.positionToleranceCm)&&o.positionToleranceCm>0&&
        o.positionToleranceCm<=0.1&&std::isfinite(o.pitchToleranceDeg)&&
        o.pitchToleranceDeg>0&&o.pitchToleranceDeg<=0.1&&
        (!o.useSeed||jointsInLimits(o.seed));
}
double distance(const CartesianPose& a,const CartesianPose& b) {
    return std::hypot(std::hypot(a.xCm-b.xCm,a.yCm-b.yCm),a.zCm-b.zCm);
}
double seedDistance(const JointAngles& a,const JointAngles& b) {
    const double dy=a.yawDeg-b.yawDeg,ds=a.shoulderDeg-b.shoulderDeg;
    const double de=a.elbowDeg-b.elbowDeg,dw=a.wristPitchDeg-b.wristPitchDeg;
    return dy*dy+ds*ds+de*de+dw*dw;
}
}

IKOptions::IKOptions(): minPitchDeg(-90),maxPitchDeg(10),pitchStepDeg(0.25),
    positionToleranceCm(1e-5),pitchToleranceDeg(1e-5),useSeed(false) {}
IKResult::IKResult():ok(false),singular(false),status(IKStatus::Unreachable),
    positionErrorCm(0),pitchErrorDeg(0) {}
bool Geometry::valid() const {
    return std::isfinite(baseHeightCm)&&baseHeightCm>=0&&
        std::isfinite(upperArmCm)&&upperArmCm>0&&
        std::isfinite(forearmCm)&&forearmCm>0&&
        std::isfinite(toolCm)&&toolCm>0;
}
bool jointsInLimits(const JointAngles& j) {
    return finite(j)&&j.yawDeg>=-90-eps&&j.yawDeg<=90+eps&&
        j.shoulderDeg>=-eps&&j.shoulderDeg<=180+eps&&
        j.elbowDeg>=-90-eps&&j.elbowDeg<=90+eps&&
        j.wristPitchDeg>=-90-eps&&j.wristPitchDeg<=90+eps;
}
CartesianPose forwardKinematics(const JointAngles& j,const Geometry& g) {
    if(!finite(j)||!g.valid()) {
        const double n=std::numeric_limits<double>::quiet_NaN();
        return CartesianPose(n,n,n,n);
    }
    const double s=rad(j.shoulderDeg),e=s+rad(j.elbowDeg);
    const double p=e+rad(j.wristPitchDeg);
    const double r=g.upperArmCm*std::cos(s)+g.forearmCm*std::cos(e)+g.toolCm*std::cos(p);
    const double z=g.baseHeightCm+g.upperArmCm*std::sin(s)+g.forearmCm*std::sin(e)+g.toolCm*std::sin(p);
    return CartesianPose(r*std::cos(rad(j.yawDeg)),r*std::sin(rad(j.yawDeg)),z,
                         j.shoulderDeg+j.elbowDeg+j.wristPitchDeg);
}

// The original kinematics.c is absent. FK and angle conventions are recovered
// from Listings/kinematics.lst lines 35..110 (the compiler saved source lines).
// This is a new, double precision analytic IK with both elbow branches,
// signed radius candidates, explicit validity checks and FK verification.
IKResult inverseKinematics(const CartesianPose& target,const Geometry& g,const IKOptions& o) {
    if(!g.valid()) return failure(IKStatus::InvalidGeometry,"invalid link geometry");
    if(!finite(target)) return failure(IKStatus::NonFinite,"coordinate or pitch is non-finite");
    if(!validOptions(o)) return failure(IKStatus::InvalidOptions,"invalid IK options or seed");
    if(target.pitchDeg < -180-eps || target.pitchDeg > 360+eps)
        return failure(IKStatus::JointLimits,"pitch exceeds the sum of joint limits");
    const double radius=std::hypot(target.xCm,target.yCm);
    const bool yawSingular=radius<eps;
    if(yawSingular&&!o.useSeed)
        return failure(IKStatus::Singular,"base yaw is underdetermined on the Z axis; provide a seed");
    const double primaryYaw=yawSingular?o.seed.yawDeg:deg(std::atan2(target.yCm,target.xCm));
    bool withinReach=false,withinLimits=false;
    double bestScore=std::numeric_limits<double>::infinity();
    IKResult best;
    for(int direction=0;direction<2;++direction) {
        double yaw=primaryYaw;
        if(direction) yaw += yaw>0?-180:180;
        if(yawSingular&&direction) continue;
        if(yaw < -90-eps||yaw>90+eps) continue;
        const double signedRadius=direction?-radius:radius;
        const double a=signedRadius-g.toolCm*std::cos(rad(target.pitchDeg));
        const double b=target.zCm-g.baseHeightCm-g.toolCm*std::sin(rad(target.pitchDeg));
        double c=(a*a+b*b-g.upperArmCm*g.upperArmCm-g.forearmCm*g.forearmCm)/
                  (2*g.upperArmCm*g.forearmCm);
        if(!std::isfinite(c)||c < -1-eps||c>1+eps) continue;
        withinReach=true;
        c=std::max(-1.0,std::min(1.0,c));
        const double elbowAbs=std::acos(c);
        for(int branch=0;branch<2;++branch) {
            const double elbow=branch?elbowAbs:-elbowAbs;
            double shoulder=std::atan2(b,a)-std::atan2(g.forearmCm*std::sin(elbow),
                                                       g.upperArmCm+g.forearmCm*std::cos(elbow));
            double shoulderDeg=deg(shoulder);
            if(shoulderDeg< -eps) shoulderDeg+=360;
            JointAngles j(yaw,shoulderDeg,deg(elbow),target.pitchDeg-shoulderDeg-deg(elbow));
            if(!jointsInLimits(j)) continue;
            // Prevent round-off at endpoints from becoming an out-of-limit servo command.
            j.yawDeg=std::max(-90.0,std::min(90.0,j.yawDeg));
            j.shoulderDeg=std::max(0.0,std::min(180.0,j.shoulderDeg));
            j.elbowDeg=std::max(-90.0,std::min(90.0,j.elbowDeg));
            j.wristPitchDeg=std::max(-90.0,std::min(90.0,j.wristPitchDeg));
            withinLimits=true;
            CartesianPose achieved=forwardKinematics(j,g);
            const double pe=distance(target,achieved),ae=std::fabs(target.pitchDeg-achieved.pitchDeg);
            if(!finite(achieved)||pe>o.positionToleranceCm||ae>o.pitchToleranceDeg) continue;
            const double score=o.useSeed?seedDistance(j,o.seed):(branch?1.0:0.0);
            if(score>=bestScore) continue;
            bestScore=score; best.ok=true; best.status=IKStatus::Ok; best.joints=j;
            best.achieved=achieved; best.positionErrorCm=pe; best.pitchErrorDeg=ae;
            best.singular=yawSingular||std::fabs(std::sin(elbow))<eps; best.error.clear();
        }
    }
    if(best.ok) return best;
    if(withinLimits) return failure(IKStatus::VerificationFailed,"IK failed forward-kinematics verification");
    if(withinReach) return failure(IKStatus::JointLimits,"target requires a joint angle outside limits");
    return failure(IKStatus::Unreachable,"target is outside link reach for this pitch and base range");
}

IKResult inverseKinematicsPitchRange(const CartesianPose& target,const Geometry& g,const IKOptions& o) {
    if(!g.valid()) return failure(IKStatus::InvalidGeometry,"invalid link geometry");
    if(!finite(target)) return failure(IKStatus::NonFinite,"coordinate or pitch is non-finite");
    if(!validOptions(o)) return failure(IKStatus::InvalidOptions,"invalid pitch search range/options");
    CartesianPose candidate=target;
    const double preferred=std::max(o.minPitchDeg,std::min(o.maxPitchDeg,target.pitchDeg));
    candidate.pitchDeg=preferred;
    IKResult best=inverseKinematics(candidate,g,o);
    if(best.status==IKStatus::Singular) return best;
    if(best.ok) return best;
    double bestDelta=std::numeric_limits<double>::infinity();
    std::vector<double> pitches;
    const unsigned steps=static_cast<unsigned>(std::ceil((o.maxPitchDeg-o.minPitchDeg)/o.pitchStepDeg));
    for(unsigned i=0;i<=steps;++i)
        pitches.push_back(std::min(o.maxPitchDeg,o.minPitchDeg+i*o.pitchStepDeg));
    // Visit closest sampled pitches first, then refine each feasible boundary.
    std::sort(pitches.begin(),pitches.end(),[preferred](double a,double b){
        return std::fabs(a-preferred)<std::fabs(b-preferred);
    });
    for(std::size_t i=0;i<pitches.size();++i) {
        const double pitch=pitches[i];
        if(std::fabs(pitch-preferred)>bestDelta+o.pitchStepDeg) continue;
        candidate.pitchDeg=pitch;
        IKResult found=inverseKinematics(candidate,g,o);
        if(!found.ok) continue;
        double feasible=pitch,infeasible=preferred;
        // Preferred failed; approach it from this verified feasible sample.
        for(unsigned n=0;n<40;++n) {
            const double mid=(feasible+infeasible)*0.5;
            candidate.pitchDeg=mid;
            IKResult trial=inverseKinematics(candidate,g,o);
            if(trial.ok) { feasible=mid; found=trial; } else infeasible=mid;
        }
        const double delta=std::fabs(found.achieved.pitchDeg-target.pitchDeg);
        if(delta<bestDelta) { bestDelta=delta; best=found; }
    }
    if(!best.ok) {
        best.error="no verified solution in pitch range at configured search resolution";
    }
    // pitchErrorDeg reports solver residual at selected pitch. The difference
    // from the requested pitch is available as achieved.pitchDeg-target.pitchDeg.
    return best;
}

std::array<double,6> jointsToServoAngles(const JointAngles& j,const std::array<double,6>& existing) {
    std::array<double,6> a=existing;
    a[5]=90+j.yawDeg; a[4]=180-j.shoulderDeg;
    a[3]=90+j.elbowDeg; a[2]=90+j.wristPitchDeg;
    return a;
}
JointAngles servoAnglesToJoints(const std::array<double,6>& a) {
    return JointAngles(a[5]-90,180-a[4],a[3]-90,a[2]-90);
}
const char* ikStatusName(IKStatus s) {
    switch(s) {
    case IKStatus::Ok:return "ok";
    case IKStatus::InvalidGeometry:return "invalid_geometry";
    case IKStatus::NonFinite:return "non_finite";
    case IKStatus::InvalidOptions:return "invalid_options";
    case IKStatus::Unreachable:return "unreachable";
    case IKStatus::JointLimits:return "joint_limits";
    case IKStatus::Singular:return "singular";
    case IKStatus::VerificationFailed:return "verification_failed";
    }
    return "unknown";
}
} // namespace arm
