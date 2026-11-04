// Offline only: uses MockBackend; never opens GPIO, a serial port, or a socket.
#include "robot_arm.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
template<class T> void array(const T& values) {
    std::cout << '[';
    for (std::size_t i=0; i<values.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << values[i];
    }
    std::cout << ']';
}
struct Group {
    arm::MockBackend backend;
    arm::Controller controller;
    arm::RobotArm robot;
    std::uint64_t now=0;
    bool first=true;
    explicit Group(const arm::ArmConfig& config):controller(backend,config),robot(controller) {
        require(controller.initialize(), controller.error());
        require(controller.acknowledgeInitialPulses(config.homePulses(),true),controller.error());
        require(controller.enable(),controller.error());
    }
    void frame(const char* label, unsigned duration, unsigned hold,
               const arm::CartesianPose* target=nullptr, const arm::IKResult* solved=nullptr) {
        const arm::Pose angles=controller.currentAngles();
        // Read the target only after the mock interpolation has completed.
        require(!controller.moving(),"frame captured before completion");
        require(duration>=20 && duration<=60000 && hold<=60000,"invalid frame time");
        for (double a:angles) require(std::isfinite(a),"nonfinite angle");
        arm::Pulses pulses;
        std::string error;
        require(controller.config().anglesToPulses(angles,pulses,error),error);
        arm::Pulses quantized;
        for (unsigned i=0;i<6;++i) {
            // Use the actual mock output: Controller also constrains quantized
            // endpoints to the configured range (e.g. claw 1500 -> 1495.68).
            require(!backend.records.empty(),"mock emitted no command");
            quantized[i]=arm::decodePulse(backend.records.back().values[controller.config().axes[i].channel]);
            require(quantized[i]>=controller.config().axes[i].minPulseUs-1e-9 &&
                    quantized[i]<=controller.config().axes[i].maxPulseUs+1e-9,
                    "quantized pulse exceeds axis limits");
        }
        if (!first) std::cout << ',';
        first=false;
        std::cout << "{\"label\":\"" << label << "\",\"anglesDeg\":";
        array(angles);
        std::cout << ",\"durationMs\":" << duration << ",\"holdMs\":" << hold;
        std::cout << ",\"requestedPulsesUs\":"; array(pulses);
        std::cout << ",\"quantizedPulsesUs\":"; array(quantized);
        if (target) {
            require(solved && solved->ok,"unverified coordinate");
            const arm::CartesianPose p=robot.currentCartesian();
            const double residual=std::hypot(std::hypot(p.xCm-target->xCm,p.yCm-target->yCm),p.zCm-target->zCm);
            require(residual<1e-5,"coordinate frame FK mismatch");
            const arm::CartesianPose q=arm::forwardKinematics(
                arm::servoAnglesToJoints(controller.config().pulsesToAngles(quantized)),robot.geometry());
            const double quantizedError=std::hypot(std::hypot(q.xCm-target->xCm,q.yCm-target->yCm),q.zCm-target->zCm);
            std::cout << ",\"targetCm\":[" << target->xCm << ',' << target->yCm << ',' << target->zCm << ']'
                      << ",\"preferredPitchDeg\":" << target->pitchDeg
                      << ",\"selectedPitchDeg\":" << solved->achieved.pitchDeg
                      << ",\"positionResidualCm\":" << residual
                      << ",\"quantizedPositionErrorCm\":" << quantizedError;
        }
        std::cout << '}';
        now+=hold;
    }
    void finish(unsigned duration) {
        // Exercise every nominal 20 ms interpolation step, including its final endpoint.
        const std::uint64_t start=now;
        for (unsigned dt=20;dt<duration;dt+=20)
            require(controller.tick(start+dt),controller.error());
        now=start+duration;
        require(controller.tick(now),controller.error());
    }
    void coordinate(const char* name,double x,double y,double z,unsigned duration) {
        const arm::CartesianPose target(x,y,z,0);
        const arm::IKResult solved=robot.previewCoordinate(target);
        require(solved.ok,std::string(name)+": "+solved.error);
        require(robot.moveCoordinate(target,duration,now),robot.error());
        finish(duration);
        frame(name,duration,0,&target,&solved);
    }
    void claw(const char* name,double opening,unsigned duration,unsigned hold=0) {
        require(robot.moveClaw(opening,duration,now),robot.error());
        finish(duration);
        frame(name,duration,hold);
    }
    void ready() {
        // Serialize the original overlapping startup commands to make each move explicit.
        coordinate("ready",12,0,0,500);
        claw("open",90,20);
    }
};
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: export_learm_actions arm_config.ini");
        arm::ArmConfig config;
        std::string error;
        require(arm::ArmConfig::load(argv[1],config,error),error);
        std::cout << std::setprecision(17) << "{\"groups\":[";
        {
            Group g(config);
            std::cout << "{\"file\":\"00_reference_home.json\",\"frames\":[";
            require(g.robot.home(1000,g.now),g.robot.error()); g.finish(1000);
            g.frame("home",1000,0); std::cout << "]}";
        }
        {
            Group g(config);
            std::cout << ",{\"file\":\"01_reference_ready.json\",\"frames\":[";
            g.ready(); std::cout << "]}";
        }
        const char* colors[]={"red","green","blue"};
        for (int distance=50;distance<=100;distance+=10) for (int color=0;color<3;++color) {
            Group g(config);
            std::cout << ",{\"file\":\"sort_" << colors[color] << "_" << distance << "mm.json\","
                      << "\"distanceMmMin\":" << distance << ",\"distanceMmMax\":" << (distance==100?100:distance+9)
                      << ",\"frames\":[";
            g.ready();
            // Original source uses integer division dis/10, then adds 7.6 cm.
            g.coordinate("approach",double(distance/10)+7.6,0,-7,1000);
            g.claw("close",0,200);
            g.coordinate("lift",15,0,15,1000);
            if (color==0) g.coordinate("place_red",5,12,-5,1000);
            if (color==1) g.coordinate("place_green",5,-12,-5,1000);
            if (color==2) g.coordinate("place_blue",27.6,0,-5,1000);
            // Original time=0 becomes 20 ms in pwm_servo_angle_set; its 500 ms wait
            // includes this move, so use a 480 ms hold to preserve the stage budget.
            g.claw("release",90,20,480);
            g.coordinate("return",12,0,0,500);
            std::cout << "]}";
        }
        std::cout << "]}\n";
    } catch(const std::exception& e) {
        std::cerr << "Export rejected: " << e.what() << '\n';
        return 1;
    }
}
