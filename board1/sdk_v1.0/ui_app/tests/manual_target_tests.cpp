#include "../armpage.hpp"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QKeyEvent>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
#include <cstdio>
static int failures=0;
static void check(bool value,const char *name){if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",name);}}
static void pump(QApplication &app,int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){app.processEvents();QThread::msleep(2);}}
static double commanded(const QJsonObject &s,int axis){return s["commandedPulses"].toArray()[axis].toDouble();}
class LockedBackend:public arm::MockBackend {
public:bool locked=false;
 bool keepAlive(std::string &e)override{e=locked?"injected timeout":"";return !locked;}
 bool requiresRearm()const override{return locked;}
};
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setApplicationName("arm_manual_target_tests");
 ArmPage page;page.show();pump(app,30);
 auto command=[&](const QJsonObject &c){return page.handleConsoleCommand(c);};
 auto status=[&](){return command({{"action","status"}});};
 auto *angle=page.findChild<QDoubleSpinBox*>("armAngle4");auto *pulse=page.findChild<QDoubleSpinBox*>("armPulse4");
 auto *wrist=page.findChild<QDoubleSpinBox*>("armPulse1");auto *duration=page.findChild<QSpinBox*>("armDuration");
 auto *moveA=page.findChild<QPushButton*>("armMoveAngles");auto *moveP=page.findChild<QPushButton*>("armMovePulses");
 if(!angle||!pulse||!wrist||!duration||!moveA||!moveP)return 99;
 check(command({{"action","debug-enable"},{"confirm",true},{"baseline",QJsonArray{770,1500,640,511,1255,1500}}})["ok"].toBool(),"debug fixture");
 duration->setValue(20);pulse->setValue(1221.9);wrist->setValue(1600);
 check(std::fabs(angle->value()-65)<0.01,"pulse edit updates matching angle");moveP->click();pump(app,90);
 angle->setValue(60);pump(app,70);
 check(angle->value()==60&&std::fabs(pulse->value()-1166.4)<0.1,"pending target survives status timer");
 moveA->click();pump(app,90);auto s=status();
 check(std::fabs(commanded(s,1)-1600)<0.1,"angle button preserves other axis pulse target");
 check(std::fabs(commanded(s,4)-1166.4)<0.1,"angle button sends requested shoulder target");
 moveP->click();pump(app,90);s=status();
 check(std::fabs(commanded(s,4)-1166.4)<0.1&&angle->value()==60,"switching input buttons preserves the same target");
 auto *edit=angle->findChild<QLineEdit*>();angle->setFocus();edit->selectAll();
 for(QChar ch:QString("61.25")){QKeyEvent key(QEvent::KeyPress,0,Qt::NoModifier,QString(ch));QApplication::sendEvent(edit,&key);pump(app,45);}
 check(edit->text()=="61.25","timer and linked pulse field preserve intermediate keyboard edits");
 check(command({{"action","debug-pulses"},{"pulses",QJsonArray{770,1700,640,511,1277.3,1500}},{"durationMs",20}})["ok"].toBool(),"external target command");pump(app,90);
 check(std::fabs(pulse->value()-1277.3)<0.1&&std::fabs(angle->value()-(1277.3-500)/1999.2*180)<0.011,"external commands refresh both editors");
 angle->setValue(60);moveA->click();pump(app,90);
 check(std::fabs(commanded(status(),1)-1700)<0.1,"manual edit after external command preserves other joints");
 command({{"action","debug-pulses"},{"pulses",QJsonArray{770,1700,640,511,1400,1500}},{"durationMs",2000}});pump(app,150);
 s=command({{"action","stop"}});
 check(!s["moving"].toBool()&&std::fabs(pulse->value()-commanded(s,4))<0.1,"stop adopts the held command rather than the old endpoint");
 command({{"action","off"}});
 // Recorded and legacy action paths: fixtures live only in a temporary folder.
 QTemporaryDir temp;auto config=arm::ArmConfig::defaults();config.calibrationConfirmed=true;
 for(auto &axis:config.axes)axis.calibrated=true;
 config.axes[4].minPulseUs=1000;config.axes[4].maxPulseUs=2000;config.axes[4].homeAngleDeg=60;
 std::string error;const QString configPath=temp.path()+"/arm.conf";check(config.save(configPath.toStdString(),error),"temporary config save");
 ArmPage playback;playback.findChild<QLineEdit*>("armConfigPath")->setText(configPath);
 for(auto *b:playback.findChildren<QPushButton*>())if(b->text()==QString::fromUtf8("载入配置")){b->click();break;}
 playback.findChild<QPushButton*>("armEnable")->click();playback.findChild<QSpinBox*>("armDuration")->setValue(20);
 playback.findChild<QPushButton*>("armActionRecord")->click();
 playback.handleConsoleCommand({{"action","off"}});
 playback.handleConsoleCommand({{"action","debug-enable"},{"confirm",true},{"baseline",QJsonArray{770,1500,640,511,1333.3,1500}}});
 auto result=playback.handleConsoleCommand({{"action","play-actions"},{"confirm",true}});
 check(!result["ok"].toBool()&&!result["moving"].toBool()&&std::fabs(commanded(result,4)-1333.3)<0.1,"recorded action rejects mode reinterpretation without moving");
 ArmActions legacy;ArmActionFrame frame;frame.angles=config.pulsesToAngles(config.homePulses());frame.durationMs=20;legacy.frames.append(frame);
 const QByteArray original=legacy.json();const QString actionPath=temp.path()+"/legacy.json";QFile file(actionPath);check(file.open(QIODevice::WriteOnly),"legacy fixture open");file.write(original);file.close();
 result=playback.handleConsoleCommand({{"action","load-actions"},{"path",actionPath}});check(result["ok"].toBool(),"legacy load preserved");
 result=playback.handleConsoleCommand({{"action","play-actions"},{"confirm",true}});check(result["ok"].toBool(),"legacy first play binds current mapping");pump(app,100);
 playback.handleConsoleCommand({{"action","off"}});playback.findChild<QPushButton*>("armEnable")->click();
 result=playback.handleConsoleCommand({{"action","play-actions"},{"confirm",true}});
 check(!result["ok"].toBool()&&!result["moving"].toBool(),"legacy second mode rejected");
 check(file.open(QIODevice::ReadOnly)&&file.readAll()==original,"legacy action file remains byte-for-byte unchanged");file.close();
 legacy.bindAngleMapping(config);ArmActions reloaded;QString failure;std::array<double,6> lo,hi;lo.fill(0);hi.fill(180);
 check(reloaded.load(legacy.json(),lo,hi,failure)&&reloaded.matchesAngleMapping(config),"new mapping metadata survives serialization");
 config.axes[4].direction=-1;check(!reloaded.matchesAngleMapping(config),"direction changes invalidate mapping");
 LockedBackend backend;arm::Controller faulted(backend,arm::ArmConfig::defaults());faulted.initialize();faulted.enablePulseDebug(arm::ArmConfig::defaults().homePulses(),true);
 const auto held=faulted.currentPulses();backend.locked=true;faulted.tick(0);auto other=held;other[4]=1500;const auto sends=backend.records.size();
 check(!faulted.enablePulseDebug(other,true)&&faulted.currentPulses()==held&&backend.records.size()==sends,"latched timeout rejects baseline without altering last command");
 arm::MockBackend failing;arm::Controller c(failing,arm::ArmConfig::defaults());c.initialize();const auto prior=c.currentPulses();failing.failNext=true;
 check(!c.enablePulseDebug(other,true)&&c.currentPulses()==prior&&!c.enabled(),"transport failure rolls back unsent baseline");
 if(!failures)std::puts("Manual target/mapping/fault regressions: PASS (mock backend, no hardware motion)");
 return failures?1:0;
}
