#include "../armpage.hpp"
#include "../conveyorpage.hpp"
#include "../sort_recipe.hpp"
#include <QApplication>
#include <QPushButton>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
int sortSmoke(QApplication &app){
    QTemporaryDir dir;ArmPage arm;
    auto a=arm.handleConsoleCommand({{"action","coordinate-base"}});
    a=arm.handleConsoleCommand({{"action","debug-enable"},{"confirm",true},{"baseline",QJsonArray{1000,1500,833.2,999.8,1832.8,1499.6}}});
    if(!a.value("ok").toBool())return 80;
    a=arm.handleConsoleCommand({{"action","coordinate-base"}});
    const auto xyz=a.value("cartesianCm").toArray();if(xyz.size()!=3)return 81;
    const double x=xyz[0].toDouble(),y=xyz[1].toDouble(),z=xyz[2].toDouble(),pitch=a.value("cartesianPitchDeg").toDouble();
    const auto reference=a.value("visionReference").toObject();
    QJsonObject zones;QStringList names{"red","yellow","blue","green_good","green_defect"};
    for(int i=0;i<5;++i)zones[names[i]]=QJsonObject{{"zone",QString(QChar('A'+i))},{"place_xyz_cm",QJsonArray{x,y+i*.1,z-.3}}};
    QJsonObject recipe{{"version",1},{"confirmed",true},{"coordinate_frame","arm_current_cm"},{"unknown_class_policy","hold"},
        {"vision_reference",reference},{"observation_pulses_us",reference.value("pulses")},{"pick_z_cm",z-.3},{"safe_lift_z_cm",z+.7},
        {"pitch_deg",pitch},{"gripper_open_deg",30},{"gripper_closed_deg",60},{"wrist_roll_deg",90},{"move_ms",100},{"grip_ms",100},{"settle_ms",0},{"zones",zones}};
    auto request=[&](const QString &action,const QString &name,const QJsonObject &cfg){return arm.handleConsoleCommand({{"action",action},{"className",name},{"config",cfg},{"xy",QJsonArray{x,y}},{"confirm",true}});};
    auto bad=recipe;bad["confirmed"]=false;if(request("sort-run","red",bad).value("ok").toBool())return 82;
    if(request("sort-run","unknown",recipe).value("ok").toBool())return 83;
    bad=recipe;bad["safe_lift_z_cm"]=z-.4;if(request("sort-run","red",bad).value("ok").toBool())return 84;
    bad=recipe;bad["safe_lift_z_cm"]=1000;if(request("sort-run","red",bad).value("ok").toBool())return 85;
    for(int i=0;i<5;++i){a=request("fixed-sort-preview",names[i],recipe);
        if(!a.value("ok").toBool()||a.value("zone").toString()!=QString(QChar('A'+i))||a.value("steps").toArray().size()!=9){std::fprintf(stderr,"Sort preview: %s\n",QJsonDocument(a).toJson().constData());return 86;}}
    // Cancellation is distinct from a completed queue, even while not moving.
    a=request("fixed-sort-run","red",recipe);if(!a.value("ok").toBool())return 87;
    a=arm.handleConsoleCommand({{"action","stop"}});if(a.value("sortJobState").toString()!="interrupted")return 88;
    const auto config=dir.path()+"/belt.ini",vision=dir.path()+"/frame.json";
    QFile f(config);if(!f.open(QIODevice::WriteOnly))return 89;
    f.write("origin_x=0\norigin_y=0\naxis_x=1\naxis_y=0\ntarget=20\nlateral_min=-5\nlateral_max=5\n"
        "tolerance=0.2\nslow_distance=4\nmax_duty=0.45\nmin_duty=0.12\nlead_ms=0\nstable_speed=0.08\n"
        "vision_timeout_ms=1000\nstable_ms=250\nmax_run_ms=60000\nstable_frames=3\nencoder_enabled=0\n"
        "motor_reverse=0\ncm_per_count=0\nencoder_sign=1\nmax_speed_cm_s=3\nmin_speed_cm_s=0.2\n"
        "speed_kp=0.08\nspeed_ki=0.12\nspeed_filter_ms=60\nstall_ms=1000\nstall_min_speed_cm_s=0.03\nmax_encoder_speed_cm_s=30\n");f.close();
    ConveyorPage belt([&](const QJsonObject &c){return arm.handleConsoleCommand(c);});
    a=belt.handleConsoleCommand({{"action","conveyor-configure"},{"targetCm",x},{"sortConfig",recipe},{"visionPath",vision},{"configPath",config},{"watchVision",true}});
    if(!a.value("ok").toBool())return 90;
    QFile initial(vision);if(!initial.open(QIODevice::WriteOnly))return 92;
    initial.write(QJsonDocument(QJsonObject{{"frame",0},{"capture_timestamp_ms",double(QDateTime::currentMSecsSinceEpoch())},
        {"coordinate_frame","belt_cm"},{"object_id",1},{"class_name","red"},{"xy_cm",QJsonArray{x,y}}}).toJson());initial.close();
    a=belt.handleConsoleCommand({{"action","conveyor-auto-start"},{"confirm",true}});if(!a.value("ok").toBool()){std::fprintf(stderr,"Auto start: %s\n",QJsonDocument(a).toJson().constData());return 91;}
    int sequence=0,index=0;QElapsedTimer elapsed,frameTime;elapsed.start();frameTime.start();
    while(elapsed.elapsed()<20000&&index<5){
        if(frameTime.elapsed()>=140){frameTime.restart();QFile frame(vision);if(!frame.open(QIODevice::WriteOnly|QIODevice::Truncate))return 92;
            frame.write(QJsonDocument(QJsonObject{{"frame",++sequence},{"capture_timestamp_ms",double(QDateTime::currentMSecsSinceEpoch())},
                {"coordinate_frame","belt_cm"},{"object_id",index+1},{"class_name",names[index]},{"xy_cm",QJsonArray{x,y}}}).toJson());}
        app.processEvents();QThread::msleep(5);a=belt.handleConsoleCommand({{"action","conveyor-status"}});
        if(a.value("state").toString()=="idle"||a.value("state").toString()=="fault"){
            std::fprintf(stderr,"Automatic sort stopped: %s\n",QJsonDocument(a).toJson().constData());return 93;}
        if(a.value("sortedCount").toInt()>index){
            if(a.value("lastSortedClass").toString()!=names[index]||a.value("lastSortedZone").toString()!=QString(QChar('A'+index)))return 94;
            const auto state=arm.handleConsoleCommand({{"action","status"}});
            if(!SortRecipe::referenceMatches(reference,state.value("visionReference").toObject()))return 95;
            ++index;
        }
    }
    belt.handleConsoleCommand({{"action","conveyor-stop"}});arm.handleConsoleCommand({{"action","off"}});
    if(index!=5)return 96;
    std::puts("Qt automatic sorting: PASS (5 classes, real IK/player, mock GPIO/belt, cancellation and invalid plans)");return 0;
}
