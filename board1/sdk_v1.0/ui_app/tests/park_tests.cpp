#include "../armpage.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <cstdio>

static int failures=0;
static void check(bool ok,const char *name){if(!ok){std::fprintf(stderr,"FAIL: %s\n",name);++failures;}}
static void pump(int ms){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QApplication::processEvents();QThread::msleep(5);}}
static bool sameCodes(const QJsonArray &values,const arm::Pulses &expected){
    if(values.size()!=6)return false;
    for(int i=0;i<6;++i){uint8_t a=0,b=0;std::string error;
        if(!arm::quantizePulse(values[i].toDouble(),a,error)||!arm::quantizePulse(expected[i],b,error)||a!=b)return false;
    }
    return true;
}
static void exerciseDebugButton(const arm::ArmConfig &config){
    const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(folder);std::string error;
    if(!config.save((folder+"/arm_config.conf").toStdString(),error)){check(false,"save UI enable config");return;}
    ArmPage page;page.show();pump(40);
    auto state=[&](){return page.handleConsoleCommand({{"action","status"}});};
    auto *debug=page.findChild<QPushButton*>("armDebugEnable");
    auto *home=page.findChild<QPushButton*>("armHome");
    auto *duration=page.findChild<QSpinBox*>("armDuration");
    if(!debug||!home||!duration){check(false,"debug button controls present");return;}
    const QJsonArray unrelated{1000,1000,1000,1000,1000,1000};
    for(int i=0;i<6;++i)page.findChild<QDoubleSpinBox*>(QString("armPulse%1").arg(i))->setValue(unrelated[i].toDouble());
    duration->setValue(20);
    QTimer::singleShot(0,[](){auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(box&&box->button(QMessageBox::No))box->button(QMessageBox::No)->click();});
    debug->click();auto result=state();
    check(!result["enabled"].toBool()&&!result["moving"].toBool(),"declining UI enable never starts PWM or home");
    check(result["targetPulses"].toArray()==unrelated,"declining UI enable preserves edited targets");
    QTimer::singleShot(0,[](){auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(box&&box->button(QMessageBox::Yes))box->button(QMessageBox::Yes)->click();});
    debug->click();result=state();
    check(result["ok"].toBool()&&result["enabled"].toBool()&&result["debug"].toBool()&&!result["moving"].toBool(),"UI debug enable sends home without a fictitious motion");
    check(result["targetPulses"].toArray()!=unrelated,"UI enable replaces stale inputs with reset target");
    const auto enableTargets=result["targetPulses"].toArray();
    pump(200);check(!state()["moving"].toBool(),"enable does not advertise a physical ramp without feedback");
    QElapsedTimer wait;wait.start();while(state()["moving"].toBool()&&wait.elapsed()<4200)pump(10);
    result=state();
    check(result["enabled"].toBool()&&result["debug"].toBool()&&!result["moving"].toBool()&&!result["returningBeforeOff"].toBool(),"enable reset finishes and keeps PWM on");
    arm::Pulses enabledHome;for(int i=0;i<6;++i)enabledHome[i]=result["commandedPulses"].toArray()[i].toDouble();
    check(sameCodes(enableTargets,enabledHome),"UI enable target and final reset output agree");
    home->click();pump(100);result=state();
    check(sameCodes(result["commandedPulses"].toArray(),enabledHome),"later reset button reproduces UI enable endpoint");
    check(result["calibrationConfirmed"].toBool()==config.calibrationConfirmed,"UI debug enable preserves calibration confirmation");
    page.handleConsoleCommand({{"action","off"}});
    // Direct pulse commissioning remains available through the explicit console API.
    result=page.handleConsoleCommand({{"action","debug-enable"},{"confirm",true},{"baseline",unrelated}});
    check(result["commandedPulses"].toArray()==unrelated&&!result["moving"].toBool(),"explicit console baseline retains its original meaning");
    page.handleConsoleCommand({{"action","off"}});
    std::puts("Actual UI debug enable and reset equivalence: tested");
}
static void exercise(const arm::ArmConfig &config,bool debug){
    const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(folder);std::string error;
    if(!config.save((folder+"/arm_config.conf").toStdString(),error)){check(false,"save isolated test config");return;}
    ArmPage page;page.show();pump(40);
    auto state=[&](){return page.handleConsoleCommand({{"action","status"}});};
    auto *home=page.findChild<QPushButton*>("armHome");
    auto *stop=page.findChild<QPushButton*>("armStop");
    auto *duration=page.findChild<QSpinBox*>("armDuration");
    QPushButton *off=nullptr;for(auto *b:page.findChildren<QPushButton*>())if(b->property("armOff").toBool())off=b;
    check(home&&stop&&duration&&off,"UI controls present");if(!home||!stop||!duration||!off)return;
    check(!state()["enabled"].toBool(),"loading presets never enables PWM");
    const QJsonArray preset=state()["targetPulses"].toArray();
    if(debug){
        check(page.handleConsoleCommand({{"action","debug-enable"},{"confirm",true},{"baseline",preset}})["ok"].toBool(),"debug enable at preset");
    }else{
        page.findChild<QPushButton*>("armEnable")->click();check(state()["enabled"].toBool(),"normal enable at preset");
    }
    duration->setValue(20);home->click();pump(100);
    arm::Pulses homePulses;const auto homeState=state();
    for(int i=0;i<6;++i)homePulses[i]=homeState["commandedPulses"].toArray()[i].toDouble();
    check(sameCodes(preset,homePulses),"enable preset and reset button produce identical PL codes");
    // Begin at a different commanded pose to prove interpolation precedes disable.
    page.findChild<QDoubleSpinBox*>("armPulse1")->setValue(1000);
    page.findChild<QPushButton*>("armMovePulses")->click();pump(100);
    const double start=state()["commandedPulses"].toArray()[1].toDouble();
    off->click();auto result=state();
    check(result["returningBeforeOff"].toBool()&&result["enabled"].toBool()&&result["moving"].toBool(),"close starts return with PWM enabled");
    check(off->text()==QStringLiteral("立即关闭 PWM"),"second click offers immediate disable");
    pump(160);result=state();const double halfway=result["commandedPulses"].toArray()[1].toDouble();
    check(halfway>std::min(start,homePulses[1])&&halfway<std::max(start,homePulses[1]),"return interpolates without jumping to home");
    pump(2600);check(state()["enabled"].toBool()&&state()["moving"].toBool(),"return lasts at least three seconds despite 20 ms manual duration");
    QElapsedTimer wait;wait.start();while(state()["moving"].toBool()&&wait.elapsed()<1500)pump(5);
    result=state();check(result["enabled"].toBool()&&result["returningBeforeOff"].toBool(),"PWM stays on during settling hold");
    check(sameCodes(result["commandedPulses"].toArray(),homePulses),"return endpoint equals reset button in current mapping");
    check(sameCodes(result["targetPulses"].toArray(),homePulses),"return updates UI presets for the next enable");
    pump(380);result=state();
    check(!result["enabled"].toBool()&&!result["moving"].toBool()&&!result["returningBeforeOff"].toBool(),"PWM disabled after endpoint and settling hold");
    off->click();check(!state()["enabled"].toBool()&&!state()["returningBeforeOff"].toBool(),"already disabled close never enables outputs");
    auto enable=[&](){
        if(debug)page.handleConsoleCommand({{"action","debug-enable"},{"confirm",true},{"baseline",preset}});
        else page.findChild<QPushButton*>("armEnable")->click();
    };
    enable();off->click();stop->click();pump(3400);result=state();
    check(result["enabled"].toBool()&&!result["moving"].toBool()&&!result["returningBeforeOff"].toBool(),"stop cancels return and pending disable, holding PWM");
    off->click();off->click();result=state();
    check(!result["enabled"].toBool()&&!result["returningBeforeOff"].toBool(),"second close click immediately disables");
    enable();off->click();page.handleConsoleCommand({{"action","off"}});result=state();
    check(!result["enabled"].toBool()&&!result["returningBeforeOff"].toBool(),"console off remains immediate and cancels return");
    enable();off->click();page.handleConsoleCommand({{"action","connect"},{"mode","simulation"}});pump(380);result=state();
    check(result["enabled"].toBool()&&result["returningBeforeOff"].toBool(),"duplicate connect preserves active session and pending return");
    page.handleConsoleCommand({{"action","off"}});
    std::printf("Return-before-off %s mapping: tested\n",debug?"debug":"normal");
}
int main(int argc,char **argv){
    QApplication app(argc,argv);QTemporaryDir isolated;
    if(!isolated.isValid())return 2;
    qputenv("XDG_DATA_HOME",isolated.path().toUtf8());
    app.setOrganizationName("DR1ParkTests");app.setApplicationName("arm_park_tests");
    auto config=arm::ArmConfig::defaults();config.axes[1].direction=-1;config.axes[1].homeAngleDeg=60;
    config.axes[1].offsetUs=40;config.axes[5].homeAngleDeg=104.08163265306123;
    exercise(config,true);
    exerciseDebugButton(config);
    config.calibrationConfirmed=true;for(auto &axis:config.axes)axis.calibrated=true;
    exercise(config,false);
    config.axes[0].direction=-1;
    exerciseDebugButton(config);
    std::printf("Qt return-before-off tests: %s (%d failures, simulation only)\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
