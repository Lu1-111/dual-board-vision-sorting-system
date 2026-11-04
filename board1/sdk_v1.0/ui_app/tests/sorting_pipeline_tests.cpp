// End-to-end application simulation. No camera/NPU inference or hardware I/O.
// Exercise production controller, IK and action player with simulated detections.
#include "../armpage.hpp"
#include "../conveyorpage.hpp"
#include "../sort_recipe.hpp"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool ok,const QString &why) {if(!ok)throw std::runtime_error(why.toStdString());}
void writeJson(const QString &path,const QJsonObject &value) {
    QSaveFile file(path);require(file.open(QIODevice::WriteOnly),"Cannot open fixture/result");
    const auto bytes=QJsonDocument(value).toJson();
    require(file.write(bytes)==bytes.size()&&file.commit(),"Cannot atomically save fixture/result");
}
struct Simulation {
    QTemporaryDir fixtures;
    ArmPage arm;
    ConveyorPage belt;
    QFile &trace;
    QString name,vision,config,label="red",lastState,lastArmState;
    QJsonObject reference,recipe;
    QElapsedTimer time;
    qint64 previous=0,lastFrame=-1000;
    int sequence=0,object=1,armStarts=0;
    double target=0,y=0,position=0,velocity=0;
    bool publish=true,freezePosition=false,distract=false;
    QSet<QString> states;
    QSet<int> actionStages;
    QJsonObject lastBelt,lastArm;

    Simulation(const QString &caseName,QFile &log):
        belt([this](const QJsonObject &command){
            if(command.value("action").toString()=="sort-run") {
                const auto state=belt.handleConsoleCommand({{"action","conveyor-status"}});
                require(state.value("state").toString()=="picking"&&state.value("duty").toDouble()==0,
                        "Belt must be locked before any arm job");
                ++armStarts;
            }
            return arm.handleConsoleCommand(command);
        }),trace(log),name(caseName) {
        require(fixtures.isValid(),"Temporary fixture directory unavailable");
        auto enabled=arm.handleConsoleCommand({{"action","debug-enable"},{"confirm",true},
            {"baseline",QJsonArray{1000,1500,833.2,999.8,1832.8,1499.6}}});
        require(enabled.value("ok").toBool()&&enabled.value("mode").toString()=="simulation","Simulation arm required");
        const auto base=arm.handleConsoleCommand({{"action","coordinate-base"}});
        require(base.value("ok").toBool(),"Cannot establish simulated observation reference");
        const auto xyz=base.value("cartesianCm").toArray();require(xyz.size()==3,"Missing observation XYZ");
        target=xyz[0].toDouble();y=xyz[1].toDouble();const double z=xyz[2].toDouble();
        reference=base.value("visionReference").toObject();
        QJsonObject zones;const QStringList classes{"red","yellow","blue","green_good","green_defect"};
        for(int i=0;i<classes.size();++i)zones[classes[i]]=QJsonObject{
            {"zone",QString(QChar('A'+i))},{"place_xyz_cm",QJsonArray{target,y+i*.1,z-.3}}};
        recipe={{"version",1},{"confirmed",true},{"coordinate_frame","arm_current_cm"},
            {"unknown_class_policy","hold"},{"vision_reference",reference},
            {"observation_pulses_us",reference.value("pulses")},{"pick_z_cm",z-.3},{"safe_lift_z_cm",z+.7},
            {"pitch_deg",base.value("cartesianPitchDeg")},{"gripper_open_deg",30},{"gripper_closed_deg",60},
            {"wrist_roll_deg",90},{"move_ms",100},{"grip_ms",100},{"settle_ms",0},{"zones",zones}};
        config=fixtures.path()+"/belt.ini";vision=fixtures.path()+"/detections.json";
        QFile f(config);require(f.open(QIODevice::WriteOnly),"Cannot write conveyor configuration");
        f.write("origin_x=0\norigin_y=0\naxis_x=1\naxis_y=0\ntarget=20\nlateral_min=-5\nlateral_max=5\n"
            "tolerance=0.2\nslow_distance=4\nmax_duty=0.45\nmin_duty=0.12\nlead_ms=100\nstable_speed=0.08\n"
            "vision_timeout_ms=1000\nstable_ms=250\nmax_run_ms=20000\nstable_frames=3\nencoder_enabled=1\n"
            "motor_reverse=0\ncm_per_count=0.001\nencoder_sign=1\nmax_speed_cm_s=1.5\nmin_speed_cm_s=0.2\n"
            "speed_kp=0.08\nspeed_ki=0.12\nspeed_filter_ms=60\nstall_ms=1000\nstall_min_speed_cm_s=0.03\nmax_encoder_speed_cm_s=30\n");f.close();
        const auto configured=belt.handleConsoleCommand({{"action","conveyor-configure"},{"targetCm",target},
            {"configPath",config},{"visionPath",vision},{"watchVision",true},{"sortConfig",recipe}});
        require(configured.value("ok").toBool(),"Cannot configure simulated conveyor: "+configured.value("error").toString());
        position=target-2;writeFrame();
        const auto started=belt.handleConsoleCommand({{"action","conveyor-auto-start"},{"confirm",true}});
        require(started.value("ok").toBool(),"Cannot start automatic simulation: "+started.value("error").toString());
        time.start();event("start");
    }
    ~Simulation() {
        belt.handleConsoleCommand({{"action","conveyor-stop"}});
        arm.handleConsoleCommand({{"action","off"}});
        arm.handleConsoleCommand({{"action","off"}});
    }
    void event(const QString &kind) {
        const QJsonObject row{{"case",name},{"elapsed_ms",double(time.isValid()?time.elapsed():0)},
            {"event",kind},{"object_id",object},{"class_name",label},{"position_cm",position},
            {"belt",lastBelt},{"arm",lastArm}};
        trace.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+"\n");trace.flush();
    }
    void writeFrame() {
        const bool wrong=distract&&lastBelt.value("state").toString()=="picking";
        writeJson(vision,{{"frame",++sequence},{"capture_timestamp_ms",double(QDateTime::currentMSecsSinceEpoch())},
            {"coordinate_frame","belt_cm"},{"object_id",object},{"class_name",wrong?"distractor":label},
            {"xy_cm",QJsonArray{wrong?target+1000:position,y}}});
    }
    void pump() {
        const auto now=time.elapsed();const double dt=std::min(.05,(now-previous)/1000.0);previous=now;
        // Same first-order motor model/gain as the production mock backend;
        // detections follow the moving object rather than teleporting to target.
        const double goal=lastBelt.value("duty").toDouble()*4.054;
        velocity+=(goal-velocity)*std::min(1.0,dt/.08);
        if(!freezePosition)position+=velocity*dt;
        if(publish&&now-lastFrame>=100){writeFrame();lastFrame=now;}
        QApplication::processEvents();
        lastBelt=belt.handleConsoleCommand({{"action","conveyor-status"}});
        lastArm=arm.handleConsoleCommand({{"action","status"}});
        require(lastBelt.value("mode").toString()=="simulation"&&lastArm.value("mode").toString()=="simulation",
                "Simulation must never fall back to hardware");
        const auto state=lastBelt.value("state").toString();states.insert(state);
        const auto job=lastArm.value("sortJobState").toString();
        const int stage=lastArm.value("actionFrameIndex").toInt();if(state=="picking"&&stage>0)actionStages.insert(stage);
        if(state=="picking")require(lastBelt.value("duty").toDouble()==0,"Motor command during pick");
        const auto armState=job+":"+QString::number(stage);
        if(state!=lastState||armState!=lastArmState){lastState=state;lastArmState=armState;event("transition");}
        QThread::msleep(5);
    }
    template<typename Predicate> void until(Predicate done,int timeout,const QString &reason) {
        QElapsedTimer wait;wait.start();
        do {pump();if(done())return;}while(wait.elapsed()<timeout);
        throw std::runtime_error((name+": "+reason+" "+QString::fromUtf8(QJsonDocument(lastBelt).toJson(QJsonDocument::Compact))).toStdString());
    }
    void waitMs(int ms) {QElapsedTimer wait;wait.start();while(wait.elapsed()<ms)pump();}
    bool stopped() const {const auto state=lastBelt.value("state").toString();return (state=="idle"||state=="fault")&&lastBelt.value("duty").toDouble()==0;}
    void checkNoPick() {
        require(armStarts==0&&lastBelt.value("sortedCount").toInt()==0&&!lastArm.value("moving").toBool(),"Unexpected pick after rejected input");
    }
};
}

int main(int argc,char **argv) {
    QApplication app(argc,argv);const QString output=argc>1?QString::fromLocal8Bit(argv[1]):".";
    QDir().mkpath(output);QFile trace(output+"/sorting-pipeline-trace.jsonl");
    if(!trace.open(QIODevice::WriteOnly|QIODevice::Truncate))return 2;
    QJsonArray results;
    try {
        {
            Simulation s("five_classes_moving_belt",trace);s.distract=true;
            const QStringList classes{"red","yellow","blue","green_good","green_defect"};
            for(int i=0;i<classes.size();++i) {
                s.label=classes[i];s.object=i+1;s.position=s.target-2;s.freezePosition=false;s.actionStages.clear();
                s.event("new_object");
                s.until([&](){return s.lastBelt.value("sortedCount").toInt()==i+1||s.stopped();},15000,"Object did not complete");
                require(!s.stopped(),"Sorting unexpectedly stopped: "+s.lastBelt.value("error").toString()+s.lastBelt.value("message").toString());
                require(s.lastBelt.value("lastSortedClass").toString()==classes[i]&&
                    s.lastBelt.value("lastSortedZone").toString()==QString(QChar('A'+i)),"Wrong class/placement zone");
                require(s.actionStages.size()==9,"All nine real IK/player stages must execute");
                require(SortRecipe::referenceMatches(s.reference,s.lastArm.value("visionReference").toObject()),"Arm did not return to observation pose");
                require(s.armStarts==i+1,"Duplicate/omitted arm job");
                s.freezePosition=true;s.waitMs(500);
                require(s.armStarts==i+1&&s.lastBelt.value("sortedCount").toInt()==i+1,"Repeated old object was picked twice");
            }
            require(s.states.contains("tracking")&&s.states.contains("settling")&&s.states.contains("picking")&&s.states.contains("feeding"),"Missing required conveyor phases");
            s.belt.handleConsoleCommand({{"action","conveyor-stop"}});s.pump();require(s.stopped(),"Final stop must disable conveyor");
            s.event("pass");results.append(QJsonObject{{"case",s.name},{"passed",true},{"completed_objects",5},{"arm_jobs",s.armStarts},{"action_stages_per_object",9}});
        }
        {
            Simulation s("vision_timeout",trace);
            s.until([&](){return s.lastBelt.value("state").toString()=="tracking";},3000,"No tracked object");
            s.publish=false;s.until([&](){return s.stopped();},2500,"Lost vision did not stop belt");
            s.checkNoPick();s.waitMs(300);require(s.stopped(),"Vision timeout auto-resumed");s.event("pass");
            results.append(QJsonObject{{"case",s.name},{"passed",true}});
        }
        {
            Simulation s("class_changed_during_tracking",trace);
            s.until([&](){return s.lastBelt.value("state").toString()=="tracking";},3000,"No tracked object");
            s.label="blue";s.until([&](){return s.stopped();},1500,"Class change did not stop belt");
            s.checkNoPick();s.event("pass");results.append(QJsonObject{{"case",s.name},{"passed",true}});
        }
        {
            Simulation s("unknown_class_at_pick",trace);s.label="unknown";
            s.until([&](){return s.stopped();},15000,"Unknown class did not hold/stop");
            s.checkNoPick();s.event("pass");results.append(QJsonObject{{"case",s.name},{"passed",true}});
        }
        {
            Simulation s("stop_during_pick",trace);
            s.until([&](){return s.lastBelt.value("state").toString()=="picking";},15000,"Pick never began");
            s.belt.handleConsoleCommand({{"action","conveyor-stop"}});s.waitMs(600);
            require(s.stopped()&&s.lastBelt.value("sortedCount").toInt()==0&&
                s.lastArm.value("sortJobState").toString()=="interrupted"&&!s.lastArm.value("actionPlaying").toBool(),"Interrupted job was counted/released/resumed");
            require(s.lastArm.value("enabled").toBool(),"Stop must preserve simulated arm holding state");
            s.event("pass");results.append(QJsonObject{{"case",s.name},{"passed",true}});
        }
        writeJson(output+"/sorting-pipeline-result.json",{{"passed",true},{"hardware_access",false},
            {"recognition_input","synthetic class/coordinates at production JSON boundary; NPU inference not executed"},
            {"calibration","synthetic simulation parameters only"},{"cases",results}});
        std::puts("FULL_SORTING_SIMULATION_PASSED: five moving objects, five zones, real IK/player, encoder model, duplicate/distractor checks and four fault/interruption cases");
        return 0;
    } catch(const std::exception &error) {
        writeJson(output+"/sorting-pipeline-result.json",{{"passed",false},{"hardware_access",false},
            {"error",QString::fromUtf8(error.what())},{"completed_cases",results}});
        std::fprintf(stderr,"FULL_SORTING_SIMULATION_FAILED: %s\n",error.what());return 1;
    }
}
