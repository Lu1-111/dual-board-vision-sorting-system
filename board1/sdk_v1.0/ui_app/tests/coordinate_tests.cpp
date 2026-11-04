#include "../armpage.hpp"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <cstdio>

static int failures=0;
static void check(bool ok,const char *name){if(!ok){std::fprintf(stderr,"FAIL: %s\n",name);++failures;}}
int main(int argc,char **argv){
    QApplication app(argc,argv);QTemporaryDir temp;check(temp.isValid(),"temporary configuration folder");
    ArmPage page;
    auto *path=page.findChild<QLineEdit*>("armConfigPath");check(path,"configuration path control");
    if(!path)return 1;
    const QString configPath=temp.path()+"/arm.conf";path->setText(configPath);
    auto command=[&](const QJsonObject &request){return page.handleConsoleCommand(request);};
    auto result=command({{"action","coordinate-zero"}});
    check(!result["ok"].toBool(),"disabled zero rejected");
    const QJsonArray initial{500,1495.7,539.2,970.4,1252.6,1652.5};
    result=command({{"action","debug-enable"},{"confirm",true},{"baseline",initial}});
    check(result["ok"].toBool()&&result["mode"].toString()=="simulation","debug simulation setup");
    result=command({{"action","coordinate-zero"}});
    check(result["ok"].toBool()&&result["coordinateOriginEnabled"].toBool(),"capture zero");
    check(!result["moving"].toBool()&&result["commandedPulses"].toArray()==initial,"zero does not schedule a move");
    const QJsonArray relative=result["cartesianCm"].toArray();
    check(relative.size()==3,"relative pose reported");
    for(const auto &v:relative)check(std::fabs(v.toDouble())<1e-8,"current tip at relative zero");
    const double pitch=result["coordinateOriginPitchDeg"].toDouble();
    for(int i=0;i<3;++i){auto *field=page.findChild<QDoubleSpinBox*>(QString("armCoordinate%1").arg(i));check(field&&field->value()==0,"coordinate input reset to zero");}
    arm::ArmConfig saved;std::string error;
    check(arm::ArmConfig::load(configPath.toStdString(),saved,error)&&saved.cartesianFrame.enabled&&saved.cartesianFrame.debugMapping,"zero persists in configuration");
    const auto origin=saved.cartesianFrame.originCm;
    // Coordinate actions are resolved when played, with the original frame and
    // independent claw/roll targets. Exercise a reachable offset, then zero.
    ArmActions group;group.coordinateFrame=saved.cartesianFrame;
    ArmActionFrame point;point.kind=ArmActionFrame::Kind::Coordinate;point.durationMs=20;
    arm::Pose angles;
    for(int i=0;i<6;++i)angles[i]=(initial[i].toDouble()-500)/1999.2*180;
    auto joints=arm::servoAnglesToJoints(angles);joints.shoulderDeg-=1;joints.wristPitchDeg+=1;
    const arm::Geometry links(saved.linkLengthsCm[0],saved.linkLengthsCm[1],saved.linkLengthsCm[2],saved.linkLengthsCm[3]);
    const auto offset=arm::forwardKinematics(joints,links);
    point.coordinate={{offset.xCm-origin[0],offset.yCm-origin[1],offset.zCm-origin[2],pitch}};
    point.angles[0]=30;point.angles[1]=80;group.frames.append(point);
    point.coordinate={{0,0,0,pitch}};point.angles[0]=0;point.angles[1]=90;group.frames.append(point);
    const QString actionPath=temp.path()+"/coordinate.json";QString saveError;
    check(group.save(actionPath,saveError),"save coordinate group");
    result=command({{"action","load-actions"},{"path",actionPath}});
    check(result["ok"].toBool()&&result["actionUsesCoordinates"].toBool(),"load coordinate group");
    result=command({{"action","play-actions"},{"confirm",true}});check(result["ok"].toBool(),"start coordinate playback");
    QEventLoop loop;QTimer::singleShot(250,&loop,&QEventLoop::quit);loop.exec();
    result=command({{"action","status"}});
    check(!result["moving"].toBool()&&!result["actionPlaying"].toBool()&&result["uiError"].toString().isEmpty(),"coordinate playback completes");
    for(const auto &v:result["cartesianCm"].toArray())check(std::fabs(v.toDouble())<1e-6,"coordinate action returns to origin");
    check(std::fabs(result["commandedPulses"].toArray()[0].toDouble()-500)<1e-6,"coordinate action restores claw target");
    check(std::fabs(result["commandedPulses"].toArray()[1].toDouble()-1499.6)<1e-6,"coordinate action applies roll target");
    group.coordinateFrame.originCm[0]+=1;check(group.save(actionPath,saveError),"save mismatched coordinate group");
    result=command({{"action","load-actions"},{"path",actionPath}});check(result["ok"].toBool(),"load frame metadata before validation");
    result=command({{"action","play-actions"},{"confirm",true}});
    check(!result["ok"].toBool()&&!result["moving"].toBool()&&!result["actionPlaying"].toBool(),"playback rejects changed origin before motion");
    result=command({{"action","debug-pulses"},{"pulses",initial},{"durationMs",20}});
    QEventLoop restore;QTimer::singleShot(60,&restore,&QEventLoop::quit);restore.exec();
    check(result["ok"].toBool(),"restore simulation baseline");
    result=command({{"action","coordinate-preview"},{"coordinate",QJsonArray{0,0,0,pitch}}});
    check(result["ok"].toBool()&&!result["moving"].toBool(),"zero preview does not move");
    for(const auto &v:result["achievedCm"].toArray())check(std::fabs(v.toDouble())<1e-6,"preview uses relative frame");
    result=command({{"action","coordinate-move"},{"coordinate",QJsonArray{0,0,0,pitch}},{"durationMs",20}});
    check(!result["ok"].toBool()&&!result["moving"].toBool(),"coordinate move requires explicit command flag");
    result=command({{"action","coordinate-preview"},{"coordinate",QJsonArray{1000,0,0,pitch}}});
    check(!result["ok"].toBool()&&result["enabled"].toBool()&&!result["moving"].toBool(),"unreachable preview retains outputs");
    result=command({{"action","coordinate-preview"},{"coordinate",QJsonArray{0,0,QString("bad"),pitch}}});
    check(!result["ok"].toBool(),"nonnumeric coordinate rejected");
    path->setText(temp.path()); // A directory cannot atomically replace a config file.
    result=command({{"action","coordinate-base"}});
    check(!result["ok"].toBool()&&result["coordinateOriginEnabled"].toBool(),"failed save rolls back runtime origin");
    path->setText(configPath);
    result=command({{"action","off"}});
    check(!result["coordinateFrameCompatible"].toBool(),"mapping mode mismatch is explicit");
    result=command({{"action","coordinate-preview"},{"coordinate",QJsonArray{0,0,0,pitch}}});
    check(result["ok"].toBool()&&!result["enabled"].toBool()&&!result["moving"].toBool(),"recorded-frame preview works before output enabling");
    result=command({{"action","coordinate-move"},{"coordinate",QJsonArray{0,0,0,pitch}},{"durationMs",20},{"confirm",true}});
    check(!result["ok"].toBool()&&!result["enabled"].toBool(),"disabled coordinate motion remains blocked");
    result=command({{"action","debug-enable"},{"confirm",true},{"baseline",initial}});
    check(result["ok"].toBool()&&result["coordinateFrameCompatible"].toBool(),"restored mapping reuses saved origin");
    result=command({{"action","coordinate-zero"}});
    check(result["ok"].toBool(),"repeated zero succeeds");
    check(arm::ArmConfig::load(configPath.toStdString(),saved,error)&&saved.cartesianFrame.originCm==origin,"repeated zero has no drift");
    result=command({{"action","coordinate-base"}});
    check(result["ok"].toBool()&&!result["coordinateOriginEnabled"].toBool(),"base frame restored");
    result=command({{"action","coordinate-zero"}});
    check(result["ok"].toBool(),"zero before geometry change");
    auto *geometry=page.findChild<QDoubleSpinBox*>("armGeometry0");auto *save=page.findChild<QPushButton*>("armSaveConfig");
    check(geometry&&save,"geometry configuration controls");
    if(geometry&&save){geometry->setValue(geometry->value()+1);save->click();}
    result=command({{"action","status"}});
    check(!result["coordinateOriginEnabled"].toBool(),"geometry edit invalidates origin");
    command({{"action","off"}});
    // XY requests use a fixed plane and a strict pitch unless requested otherwise.
    result=command({{"action","debug-enable"},{"confirm",true},{"baseline",initial}});
    check(result["ok"].toBool(),"XY session setup");
    result=command({{"action","coordinate-base"}});check(result["ok"].toBool(),"XY base frame setup");
    const auto reference=result["visionReference"].toObject();
    check(QJsonDocument(result).toJson(QJsonDocument::Compact).size()<4096,"status fits transport packet");
    const auto xyTarget=arm::forwardKinematics(arm::JointAngles(20,60,-45,-30));
    QJsonObject xy{{"action","xy-preview"},{"xy",QJsonArray{xyTarget.xCm,xyTarget.yCm}},
        {"zCm",xyTarget.zCm},{"pitchDeg",xyTarget.pitchDeg},{"visionReference",reference}};
    result=command(xy);check(result["ok"].toBool()&&!result["moving"].toBool(),"XY preview without movement");
    check(std::fabs(result["achievedCm"].toArray()[2].toDouble()-xyTarget.zCm)<1e-6,"XY fixed z");
    check(std::fabs(result["achievedPitchDeg"].toDouble()-xyTarget.pitchDeg)<1e-6,"XY strict pitch");
    auto badXY=xy;badXY.remove("zCm");check(!command(badXY)["ok"].toBool(),"XY missing height rejected");
    badXY=xy;badXY["xy"]=QJsonArray{QString("pixel"),0};check(!command(badXY)["ok"].toBool(),"XY string rejected");
    badXY=xy;badXY["allowPitchSearch"]=1;check(!command(badXY)["ok"].toBool(),"XY search flag must be bool");
    badXY=xy;badXY["visionReference"]=QJsonObject();check(!command(badXY)["ok"].toBool(),"XY stale reference rejected");
    badXY=xy;badXY["action"]="xy-move";badXY["durationMs"]=20;
    check(!command(badXY)["ok"].toBool(),"XY move needs confirm flag");
    badXY["confirm"]=true;badXY["durationMs"]=19;check(!command(badXY)["ok"].toBool(),"XY short duration rejected");
    xy["action"]="xy-move";xy["durationMs"]=20;xy["confirm"]=true;
    result=command(xy);check(result["ok"].toBool(),"XY move accepted");
    QEventLoop xyWait;QTimer::singleShot(80,&xyWait,&QEventLoop::quit);xyWait.exec();
    result=command({{"action","status"}});
    check(!result["moving"].toBool(),"XY move completes");
    check(std::fabs(result["cartesianCm"].toArray()[0].toDouble()-xyTarget.xCm)<1e-6,"XY endpoint x");
    check(std::fabs(result["cartesianCm"].toArray()[1].toDouble()-xyTarget.yCm)<1e-6,"XY endpoint y");
    check(!command(xy)["ok"].toBool(),"moving camera reference must be restored before reuse");
    command({{"action","off"}});
    std::printf("coordinate UI tests: %s (%d failures)\n",failures?"FAIL":"PASS",failures);return failures?1:0;
}
