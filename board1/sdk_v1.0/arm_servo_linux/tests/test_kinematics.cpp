#include "kinematics.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef ARM_TEST_CONTROLLER
#include "robot_arm.hpp"
#endif

namespace {
void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
void near(double a,double b,const char* message,double tolerance=1e-6) {
    check(std::fabs(a-b)<=tolerance,message);
}
void roundTrip(const arm::JointAngles& joints) {
    arm::CartesianPose target=arm::forwardKinematics(joints);
    arm::IKOptions options;options.useSeed=true;options.seed=joints;
    arm::IKResult result=arm::inverseKinematics(target,arm::Geometry(),options);
    check(result.ok,"reachable FK pose rejected by IK");
    near(result.achieved.xCm,target.xCm,"round trip x");
    near(result.achieved.yCm,target.yCm,"round trip y");
    near(result.achieved.zCm,target.zCm,"round trip z");
    near(result.achieved.pitchDeg,target.pitchDeg,"round trip pitch");
}
#ifdef ARM_TEST_CONTROLLER
class FakeHardware : public arm::MockBackend {
public: bool hardware() const { return true; }
};
void robotArmTests() {
    using namespace arm;
    ArmConfig config=ArmConfig::defaults();
    MockBackend backend; Controller controller(backend,config);
    check(controller.initialize(),"mock initialize");
    const std::size_t records=backend.records.size();
    RobotArm robot(controller,true);
    check(backend.records.size()==records&&!controller.moving(),"constructor moved robot");
    check(controller.acknowledgeInitialPulses(config.homePulses(),true),"mock baseline");
    check(controller.enable(),"mock enable");
    check(robot.moveClaw(60,100,0),"claw motion schedule");
    check(controller.tick(100),"claw motion tick");
    near(controller.currentAngles()[0],30,"claw angle mapping");
    check(robot.moveRoll(-30,100,100),"roll motion schedule");
    check(controller.tick(200),"roll motion tick");
    near(controller.currentAngles()[1],120,"roll angle mapping");
    check(!robot.moveClaw(91,100,200),"invalid claw accepted");
    check(!robot.moveRoll(-91,100,200),"invalid roll accepted");
    check(robot.moveCoordinate(CartesianPose(15,0,2,-45),1000,200),"mock coordinate motion");
    check(controller.tick(220),"coordinate motion tick");
    Pose stopped=controller.currentAngles();robot.stop();
    check(!controller.moving(),"stop failed to cancel interpolation");
    check(controller.tick(1500),"stopped tick");
    for(unsigned i=0;i<6;++i) near(controller.currentAngles()[i],stopped[i],"stopped pose changed");
    check(robot.home(100,1500)&&controller.tick(1600),"explicit home failed");
    for(unsigned i=0;i<6;++i) near(controller.currentAngles()[i],config.axes[i].homeAngleDeg,"home target");
    config.calibrationConfirmed=true;
    for(unsigned i=0;i<6;++i)config.axes[i].calibrated=true;
    FakeHardware hardware; Controller hardController(hardware,config);
    check(hardController.initialize(),"fake hardware initialize");
    check(hardController.acknowledgeInitialPulses(config.homePulses(),true)&&hardController.enable(),"fake hardware enable");
    RobotArm hardRobot(hardController,true);
    check(!hardRobot.moveCoordinate(CartesianPose(15,0,2,-45),100,0),"simulation flag bypassed geometry interlock");
    check(hardRobot.error().find("geometry")!=std::string::npos,"geometry failure unclear");
    config.geometryConfirmed=true;
    FakeHardware confirmed; Controller confirmedController(confirmed,config);
    check(confirmedController.initialize()&&confirmedController.acknowledgeInitialPulses(config.homePulses(),true)&&
          confirmedController.enable(),"confirmed fake hardware setup");
    RobotArm changedGeometry(confirmedController,Geometry(2.89,10.5,8.9,17.7),true);
    check(!changedGeometry.moveCoordinate(CartesianPose(15,0,2,-45),100,0),"custom geometry bypassed confirmed dimensions");
    ArmConfig uncalibrated=ArmConfig::defaults();FakeHardware debugHardware;
    Controller debugController(debugHardware,uncalibrated);RobotArm debugRobot(debugController,true);
    check(debugController.initialize(),"debug hardware initialize");
    Pulses baseline={{1000,1500,1500,1500,1500,1500}};
    check(debugController.enablePulseDebug(baseline,true),"explicit full-range debug enable");
    check(debugRobot.moveCoordinate(CartesianPose(15,0,2,-45),20,0)&&debugController.tick(20),"explicit debug coordinate permission");
    check(debugRobot.home(20,20)&&debugController.tick(40),"debug home permission");
    check(debugRobot.moveClaw(0,20,40)&&debugController.tick(60),"debug claw permission");
    check(!debugRobot.moveCoordinate(CartesianPose(100,0,0,0),20,60),"debug accepted unreachable coordinate");
    check(!debugController.config().calibrationConfirmed&&!debugController.config().geometryConfirmed,"debug fabricated confirmations");
    for(const AxisConfig &axis:debugController.config().axes)check(!axis.calibrated,"debug fabricated axis calibration");
    check(debugController.disable(),"debug disable");
    check(!debugRobot.moveCoordinate(CartesianPose(15,0,2,-45),20,60),"debug geometry permission survived disable");
    check(debugController.acknowledgeInitialPulses(baseline,true)&&!debugController.enable(),"debug calibration permission survived disable");
}
void coordinateOriginTests() {
    using namespace arm;
    ArmConfig config=ArmConfig::defaults();MockBackend backend;Controller controller(backend,config);
    RobotArm robot(controller,true);
    check(controller.initialize(),"origin controller initialize");
    check(!robot.zeroAtCurrentPose(),"origin accepted unknown disabled pose");
    const Pulses initial={{500,1495.7,539.2,970.4,1252.6,1652.5}};
    check(controller.enablePulseDebug(initial,true),"origin debug setup");
    const auto before=backend.records.size();
    const auto base=robot.currentBaseCartesian();
    check(robot.zeroAtCurrentPose(),"capture Cartesian origin");
    check(backend.records.size()==before&&!controller.moving(),"zeroing issued motor commands");
    const auto origin=robot.cartesianFrame();
    check(origin.enabled&&origin.debugMapping,"origin mode not recorded");
    near(robot.currentCartesian().xCm,0,"zero x");near(robot.currentCartesian().yCm,0,"zero y");
    near(robot.currentCartesian().zCm,0,"zero z");near(robot.currentCartesian().pitchDeg,base.pitchDeg,"zero changed pitch");
    check(robot.zeroAtCurrentPose(),"re-zero failed");
    near(robot.cartesianFrame().originCm[0],base.xCm,"re-zero accumulated offset");
    const auto preview=robot.previewCoordinate(CartesianPose(0,0,0,base.pitchDeg));
    check(preview.ok,"zero point rejected");near(preview.achieved.xCm,0,"preview relative x");
    near(preview.achieved.yCm,0,"preview relative y");near(preview.achieved.zCm,0,"preview relative z");
    const auto initialJoints=servoAnglesToJoints(controller.currentAngles());
    near(preview.joints.yawDeg,initialJoints.yawDeg,"zero yaw changed");
    near(preview.joints.shoulderDeg,initialJoints.shoulderDeg,"zero shoulder changed");
    near(preview.joints.elbowDeg,initialJoints.elbowDeg,"zero elbow changed");
    near(preview.joints.wristPitchDeg,initialJoints.wristPitchDeg,"zero wrist changed");
    JointAngles changed=initialJoints;changed.shoulderDeg-=2;changed.wristPitchDeg+=2;
    const auto absolute=forwardKinematics(changed);
    const CartesianPose relative(absolute.xCm-base.xCm,absolute.yCm-base.yCm,absolute.zCm-base.zCm,absolute.pitchDeg);
    check(robot.moveCoordinateWithTools(relative,20,110,20,0)&&controller.tick(20),"relative coordinate motion with tools");
    near(controller.currentAngles()[0],20,"coordinate motion claw target ignored");
    near(controller.currentAngles()[1],110,"coordinate motion roll target ignored");
    near(robot.currentBaseCartesian().xCm,absolute.xCm,"relative target converted to wrong base x");
    near(robot.currentCartesian().zCm,relative.zCm,"relative current z wrong");
    check(robot.moveCoordinate(CartesianPose(0,0,0,base.pitchDeg),20,20)&&controller.tick(40),"return to origin");
    near(robot.currentCartesian().xCm,0,"return origin x");near(robot.currentCartesian().zCm,0,"return origin z");
    check(controller.moveDebugPulses(initial,100,40),"origin moving setup");
    check(!robot.zeroAtCurrentPose(),"origin captured while moving");controller.stop();
    CartesianFrame invalid=origin;invalid.originCm[1]=std::numeric_limits<double>::quiet_NaN();
    check(!robot.setCartesianFrame(invalid),"nonfinite origin accepted");
    near(robot.cartesianFrame().originCm[1],origin.originCm[1],"failed origin update changed frame");
    check(controller.disable(),"origin disable");
    check(!robot.cartesianFrameCompatible(),"origin mapping mismatch ignored");
    check(robot.previewCoordinate(CartesianPose(0,0,0,base.pitchDeg)).ok,"read-only preview requires hardware enabling");
    const auto disabledRecords=backend.records.size();
    check(!robot.moveCoordinate(CartesianPose(0,0,0,base.pitchDeg),20,40),"incompatible mapping used for motion");
    check(backend.records.size()==disabledRecords,"mapping rejection issued motor commands");
    check(!std::isfinite(robot.currentCartesian().xCm),"incompatible frame returned a misleading location");
    check(controller.enablePulseDebug(initial,true),"origin debug restore");
    check(robot.cartesianFrameCompatible(),"recorded mode not restored");
    check(robot.setCartesianFrame(CartesianFrame()),"clear origin failed");
    near(robot.currentCartesian().xCm,base.xCm,"clear origin did not restore base frame");
    // An upright reference must keep its pitch despite the default search cap.
    const Pose upright=jointsToServoAngles(JointAngles(0,90,0,0),controller.currentAngles());
    check(controller.moveAngles(upright,20,40)&&controller.tick(60),"upright origin setup");
    check(robot.zeroAtCurrentPose(),"upright origin capture");
    const auto uprightPreview=robot.previewCoordinate(CartesianPose(0,0,0,90));
    check(uprightPreview.ok,"upright origin rejected");near(uprightPreview.achieved.pitchDeg,90,"upright origin pitch relaxed");
}
void xyTests() {
    using namespace arm;
    ArmConfig config=ArmConfig::defaults();MockBackend backend;Controller controller(backend,config);
    RobotArm robot(controller);check(controller.initialize(),"XY initialize");
    check(controller.acknowledgeInitialPulses(config.homePulses(),true)&&controller.enable(),"XY enable");
    const CartesianPose target=forwardKinematics(JointAngles(20,60,-45,-30));
    XYPlane plane(target.zCm,target.pitchDeg);
    const auto records=backend.records.size();
    auto solved=robot.previewXY(target.xCm,target.yCm,plane);
    check(solved.ok&&backend.records.size()==records,"XY preview must not move");
    near(solved.achieved.zCm,plane.heightCm,"XY fixed height");
    near(solved.achieved.pitchDeg,plane.pitchDeg,"XY fixed pitch");
    const Pose tools=controller.currentAngles();
    check(robot.moveXY(target.xCm,target.yCm,plane,100,0)&&controller.tick(100),"XY motion");
    near(robot.currentCartesian().xCm,target.xCm,"XY end x");near(robot.currentCartesian().yCm,target.yCm,"XY end y");
    near(controller.currentAngles()[0],tools[0],"XY claw changed");near(controller.currentAngles()[1],tools[1],"XY roll changed");
    check(!robot.previewXY(37.03,0,XYPlane(2.89,-90)).ok,"XY silently relaxed strict pitch");
    check(robot.previewXY(37.03,0,XYPlane(2.89,-90,true)).ok,"XY explicit pitch search failed");
    check(!robot.previewXY(1000,0,plane).ok,"XY unreachable accepted");
    check(!robot.moveXY(target.xCm,target.yCm,plane,3600001,100),"XY invalid duration accepted");
    check(!controller.moving(),"XY rejected target started motion");
    const double nan=std::numeric_limits<double>::quiet_NaN();
    check(!robot.previewXY(nan,0,plane).ok&&!robot.previewXY(0,0,XYPlane(nan,0)).ok&&
          !robot.previewXY(0,0,XYPlane(0,nan)).ok,"XY nonfinite accepted");
    check(controller.acknowledgeInitialPulses(config.homePulses(),true)&&controller.enable(),"XY restore after controller error");
    check(robot.zeroAtCurrentPose(),"XY relative zero");
    const CartesianPose relative=robot.currentCartesian();
    solved=robot.previewXY(0,0,XYPlane(0,relative.pitchDeg));
    check(solved.ok,"XY relative origin rejected");near(solved.achieved.zCm,0,"XY relative height");
    check(controller.disable()&&!robot.moveXY(0,0,XYPlane(0,relative.pitchDeg),100,100),"XY disabled motion accepted");
}
#endif
}
int main() {
    try {
        using namespace arm;
        CartesianPose horizontal=forwardKinematics(JointAngles(0,0,0,0));
        near(horizontal.xCm,37.03,"known horizontal reach");
        near(horizontal.yCm,0,"known horizontal y");
        near(horizontal.zCm,2.89,"known horizontal height");
        CartesianPose upright=forwardKinematics(JointAngles(0,90,0,0));
        near(upright.xCm,0,"known vertical x");
        near(upright.zCm,39.92,"known vertical height");
        CartesianPose elbow=forwardKinematics(JointAngles(90,90,-90,0));
        near(elbow.xCm,0,"known yaw90 x");
        near(elbow.yCm,26.6,"known yaw90 y");
        near(elbow.zCm,13.32,"known bent height");
        roundTrip(JointAngles(0,0,0,0));
        roundTrip(JointAngles(0,90,0,0));
        roundTrip(JointAngles(45,60,-45,-30));
        roundTrip(JointAngles(-60,30,45,-60));
        roundTrip(JointAngles(90,90,-90,0));
        roundTrip(JointAngles(-90,0,90,-90));
        roundTrip(JointAngles(0,180,0,0)); // Signed radius / folded behind base.
        check(!inverseKinematics(CartesianPose(100,0,0,0)).ok,"unreachable accepted");
        check(inverseKinematics(CartesianPose(0,0,39.92,90)).status==IKStatus::Singular,
              "undefined yaw without seed not reported");
        check(inverseKinematics(CartesianPose(std::numeric_limits<double>::infinity(),0,0,0)).status==IKStatus::NonFinite,
              "infinity accepted");
        check(inverseKinematics(CartesianPose(0,0,std::numeric_limits<double>::quiet_NaN(),0)).status==IKStatus::NonFinite,
              "NaN accepted");
        check(inverseKinematics(CartesianPose(10,0,10,0),Geometry(0,-1,8.9,17.7)).status==IKStatus::InvalidGeometry,
              "negative link accepted");
        IKOptions options;options.minPitchDeg=20;options.maxPitchDeg=10;
        check(inverseKinematicsPitchRange(CartesianPose(15,0,2,-45),Geometry(),options).status==IKStatus::InvalidOptions,
              "reversed pitch range accepted");
        IKResult ranged=inverseKinematicsPitchRange(CartesianPose(15,0,2,-45));
        check(ranged.ok,"reference default coordinate not reachable in pitch range");
        near(ranged.achieved.xCm,15,"pitch search x");
        near(ranged.achieved.zCm,2,"pitch search z");
        check(ranged.achieved.pitchDeg>=-90-1e-6&&ranged.achieved.pitchDeg<=10+1e-6,
              "pitch search left range");
        options=IKOptions();options.minPitchDeg=-20;options.maxPitchDeg=100;
        CartesianPose preferred=forwardKinematics(JointAngles(0,60,-45,-30));
        preferred.pitchDeg=100;
        ranged=inverseKinematicsPitchRange(preferred,Geometry(),options);
        check(ranged.ok,"pitch relaxation rejected reachable coordinate");
        near(ranged.achieved.xCm,preferred.xCm,"relaxed pitch x");
        near(ranged.achieved.zCm,preferred.zCm,"relaxed pitch z");
        const JointAngles branches[2]={JointAngles(0,30,45,-60),JointAngles(0,30+41.263, -45,26.737)};
        CartesianPose branchTarget=forwardKinematics(branches[0]);
        options=IKOptions();options.useSeed=true;options.seed=branches[0];
        IKResult positive=inverseKinematics(branchTarget,Geometry(),options);
        check(positive.ok&&positive.joints.elbowDeg>0,"seed failed to select positive elbow");
        options.seed=branches[1];
        IKResult negative=inverseKinematics(branchTarget,Geometry(),options);
        check(negative.ok&&negative.joints.elbowDeg<0,"seed failed to select negative elbow");
        std::array<double,6> a={{12,34,0,0,0,0}};
        a=jointsToServoAngles(JointAngles(10,60,-30,-20),a);
        near(a[0],12,"claw not preserved");near(a[1],34,"roll not preserved");
        near(a[2],70,"wrist mapping");near(a[3],60,"elbow mapping");
        near(a[4],120,"shoulder mapping");near(a[5],100,"base mapping");
        JointAngles restored=servoAnglesToJoints(a);
        near(restored.shoulderDeg,60,"reverse shoulder mapping");
#ifdef ARM_TEST_CONTROLLER
        robotArmTests();
        coordinateOriginTests();
        xyTests();
#endif
        std::cout<<"kinematics tests passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"kinematics test failed: "<<e.what()<<"\n";return 1;
    }
}
