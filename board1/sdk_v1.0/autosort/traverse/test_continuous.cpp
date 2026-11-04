#include "drive.hpp"
#include "vision.hpp"
#include "../parking_region.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QVector>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
using namespace conveyor;
struct Fake:Registers {
    uint32_t features=14,limit=8000,clock=75000000,wd=18750000,period=3750,duty=0,control=0,activeDuty=0,fault=0,encErrors=0,encoderCount=0;
    uint64_t now=0,lastApply=0,started=0;bool enabled=false;QVector<uint32_t> reads,writes,controls;
    void setNow(uint64_t t)override{if(t<now)throw std::runtime_error("fake time reversed");now=t;if(enabled&&now-lastApply>=250){fault|=0x100;enabled=false;activeDuty=0;}if(enabled&&now-started>=8000){fault|=0x400;enabled=false;activeDuty=0;}}
    uint32_t read(uint32_t a)override{reads<<a;switch(a){case Id:return RegisterId;case Features:return features;case RunLimitMs:if(features!=14&&features!=30)throw std::runtime_error("legacy0x34 forbidden");return limit;case ClockHz:return clock;case Watchdog:return wd;case Period:return period;case Status:return fault|(enabled?1:0);case ActiveDuty:return activeDuty;case EncoderCount:return encoderCount;case EncoderErrors:return encErrors;default:return 0;}}
    void write(uint32_t a,uint32_t v)override{writes<<a;if(a==Duty)duty=v;else if(a==Control){control=v;controls<<v;}else if(a==Apply){if(v!=0xc0de0001u)throw std::runtime_error("apply invalid");lastApply=now;if(!control||!duty){enabled=false;activeDuty=0;}else if(!fault){if(!enabled)started=now;enabled=true;activeDuty=duty;}}else throw std::runtime_error("unexpected write");}
};
static QJsonObject tracking(int t=1000,int seq=1,double x=786,double y=555){return {{"schema",1},{"status","ok"},{"source","imx415"},{"source_session","fixture-session"},{"frame_sequence",QString::number(seq)},{"published_monotonic_ms",t},{"source_frame_age_ms",1},{"width",1920},{"height",1080},{"purpose","localization_only"},{"coordinate_frame","camera_pixel"},{"localization_width",320},{"sampling_step_px",QJsonArray{6,6}},{"objects",QJsonArray{QJsonObject{{"center_xy",QJsonArray{x,y}},{"bbox_xyxy",QJsonArray{x-100,y-70,x+100,y+70}},{"partial",false},{"ambiguous",false},{"track_id","44"}}}}};}
static QJsonObject arm(int job=10){QJsonArray axes;for(int i=0;i<6;++i)axes.append(QJsonArray{i,1,0,180,500,2499.2,0});return {{"ok",true},{"connected",true},{"mode","hardware"},{"enabled",true},{"debug",true},{"initialPoseAcknowledged",true},{"hardwareStatusAvailable",true},{"hardwareOutputActive",true},{"moving",false},{"actionPlaying",false},{"returningBeforeOff",false},{"holdingAfterTimeout",false},{"actionJobId",job},{"actionJobState","completed"},{"sortJobId",0},{"sortJobState","idle"},{"actionFile","/home/uisrc/qt_play/actions/fixed_pick_20261021/actions/perfect_selection.json"},{"targetPulses",QJsonArray{500,1495.7,539.2,500,1252.6,1656}},{"commandedPulses",QJsonArray{500,1495.7126666666666,539.2065333333334,500,1252.5877333333333,1655.9818666666667}},{"visionReference",QJsonObject{{"axes",axes}}}};}
int main(int argc,char**argv){QCoreApplication app(argc,argv);int cases=0;
    for(const auto &p:QVector<QPointF>{{1217,408},{1253,678},{1239,510},{1235,543}}){CHECK(autosort::inParkingRegion(p));++cases;}
    for(const auto &p:QVector<QPointF>{{1216,543},{1254,543},{1239,394},{1239,692}}){CHECK(!autosort::inParkingRegion(p));++cases;}
    for(int mode=0;mode<7;++mode){Fake f;if(mode==0)f.features=6;if(mode==1)f.features=0;if(mode==2)f.limit=1000;if(mode==3)f.clock=50000000;if(mode==4)f.wd=0;if(mode==5)f.period=2500;if(mode==6)f.encErrors=1;bool fail=false;try{traverse::Drive d(f);d.initialize(0);}catch(...){fail=true;}CHECK(fail&&f.writes.isEmpty());if(mode<2)CHECK(!f.reads.contains(RunLimitMs));++cases;}
    {Fake f;auto p=traverse::readProfile(f);CHECK(p["features"].toInt()==14&&f.writes.isEmpty());++cases;}
    {Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);CHECK(f.enabled&&f.activeDuty==2813);d.tick(20,50,true);CHECK(f.enabled&&f.activeDuty==1875);CHECK(f.controls==QVector<uint32_t>({0,1,1}));auto stopped=d.stop(21);CHECK(!f.enabled&&stopped["active_duty"].toInt()==0);bool fail=false;try{d.tick(30,75,true);}catch(...){fail=true;}CHECK(fail&&!f.enabled);++cases;}
    {Fake f;{traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);}CHECK(!f.enabled&&f.activeDuty==0);++cases;}
    {Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);bool fail=false;try{d.tick(111,75,true);}catch(...){fail=true;}CHECK(fail&&!f.enabled);++cases;}
    {Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);bool fail=false;try{d.tick(260,75,true);}catch(...){fail=true;}CHECK(fail&&!f.enabled&&(f.fault&0x100));CHECK(!f.controls.contains(4));++cases;}
    {Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);bool fail=false;for(int t=20;t<=7510;t+=10){try{d.tick(t,50,true);}catch(...){fail=true;break;}}CHECK(fail&&!f.enabled&&f.now==7510&&!(f.fault&0x400));++cases;}
    for(int mode=0;mode<4;++mode){Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);if(mode==0)f.fault=0x200;if(mode==1)f.encErrors=1;bool fail=false;try{d.tick(20,mode==3?100:50,mode!=2);}catch(...){fail=true;}CHECK(fail&&!f.enabled&&!f.controls.contains(4));++cases;}
    {Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);bool fail=false;try{d.tick(9,75,true);}catch(...){fail=true;}CHECK(fail&&!f.enabled);++cases;}
    {traverse::SpeedSchedule s;CHECK(s.update(1099)==75&&s.update(1100)==50&&s.update(1098)==50&&s.update(1200)==50);++cases;}
    {Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);d.tick(110,75,true);CHECK(f.enabled&&d.diagnostics()["max_heartbeat_gap_ms"].toInt()==100);d.stop(111);++cases;}
    // Encoder feedback replaces only duty selection; existing flow cases above remain.
    {Fake f;f.features=30;traverse::EncoderTuning tuning;tuning.filterMs=0;
     traverse::Drive d(f,tuning);d.initialize(0);d.tick(10,75,true);
     CHECK(f.activeDuty==2813);d.tick(30,75,true);CHECK(f.activeDuty>2813);
     f.encoderCount+=300;d.tick(50,75,true);CHECK(f.activeDuty<2813);
     f.encoderCount+=3000;d.tick(70,75,true);CHECK(f.activeDuty==0&&!f.enabled);
     CHECK(d.diagnostics()["encoder_control"].toBool());d.stop(71);++cases;}
    for(int sign: {1,-1}){
     Fake f;f.features=30;f.encoderCount=0xfffffff0u;
     traverse::Drive d(f);d.initialize(0);double velocity=0,counts=0;int64_t last=0;
     double beforeLoad=0;
     for(int t=10;t<=6000;t+=10){
         const double plant=t<2500?4.05366794:3.3;
         velocity+=(plant*f.activeDuty/3750.0-velocity)*.01/.09;
         counts+=velocity*.01/.0005451409279338169;
         const auto whole=int64_t(std::llround(counts));f.encoderCount+=uint32_t(sign*(whole-last));last=whole;
         d.tick(t,75,true);if(t==2400)beforeLoad=d.diagnostics()["duty"].toDouble();
     }
     auto q=d.diagnostics();CHECK(std::abs(q["target_cm_s"].toDouble()-q["measured_cm_s"].toDouble())<.25);
     CHECK(q["duty"].toDouble()>beforeLoad);d.tick(6010,50,true);
     CHECK(std::abs(d.diagnostics()["target_cm_s"].toDouble()-2.02683397)<.001);
     CHECK(traverse::successfulStop(d.stop(6020)));++cases;
    }
    const auto poly=trial::defaultPolygon();trial::validatePolygon(poly);auto good=trial::readFrame(tracking(),1000,poly);
    {trial::Guard g;CHECK(g.accept(good));CHECK(!g.accept(good));auto bad=good;bad.seq=2;bool fail=false;try{g.accept(bad);}catch(...){fail=true;}CHECK(fail);bad.captured++;CHECK(g.accept(bad));++cases;}
    for(int mode=0;mode<5;++mode){auto t=tracking();if(mode==0)t["source_frame_age_ms"]=501;if(mode==1)t["published_monotonic_ms"]=1001;if(mode==2)t=tracking(1000,1,1340,543);if(mode==3)t["objects"]=QJsonArray{};if(mode==4){auto a=t["objects"].toArray();a.append(a[0]);t["objects"]=a;}Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,75,true);bool fail=false;try{trial::readFrame(t,1000,poly);}catch(...){d.stop(20);fail=true;}CHECK(fail&&!f.enabled);++cases;}
    {QVector<QPointF> square{{100,300},{1500,300},{1500,800},{100,800}};auto t=tracking();auto o=t["objects"].toArray()[0].toObject();o["bbox_xyxy"]=QJsonArray{100,400,900,650};t["objects"]=QJsonArray{o};bool fail=false;try{trial::readFrame(t,1000,square);}catch(...){fail=true;}CHECK(fail);++cases;}
    trial::checkArm(arm(),10);auto reboot=arm(0);reboot["actionJobState"]="idle";trial::checkArm(reboot,0);++cases;
    for(int mode=0;mode<8;++mode){auto a=arm();if(mode==0)a["actionJobId"]=0;if(mode==1)a["sortJobId"]=1;if(mode==2)a.remove("sortJobState");if(mode==3)a["sortJobState"]="failed";if(mode==4)a["commandedPulses"]=QJsonArray{770.1,1500,640,511,1255,1656};if(mode==5)a["enabled"]=false;if(mode==6)a["actionJobState"]="interrupted";if(mode==7){auto ref=a["visionReference"].toObject();auto axes=ref["axes"].toArray();auto first=axes[0].toArray();first[1]=-1;axes[0]=first;ref["axes"]=axes;a["visionReference"]=ref;}bool fail=false;try{trial::checkArm(a,10);}catch(...){fail=true;}CHECK(fail);++cases;}
    {Fake f;traverse::Drive d(f);d.initialize(0);d.tick(10,50,true);CHECK(traverse::successfulStop(d.stop(20)));f.encErrors=1;CHECK(!traverse::successfulStop(d.stop(30)));++cases;}
    {auto a=arm();a["targetPulses"]=QJsonArray{769.9,1500,640,511,1255,1656};a["commandedPulses"]=a["targetPulses"];bool fail=false;try{trial::checkArm(a,10);}catch(...){fail=true;}CHECK(fail);++cases;}
    {auto a=arm();a["actionFile"]="/home/uisrc/qt_play/actions/fixed_pick_20261021/actions/POSE_02_pick_open.json";bool fail=false;try{trial::checkArm(a,10);}catch(...){fail=true;}CHECK(fail);++cases;}
    std::printf("CONTINUOUS_NATIVE_PASS cases=%d legacy_zero_writes,profile,watchdog,fault,7500ms,100msgap,no_enable_gap,latched_slow,stale,overshoot,strict_edge,expected_job,mapping,destructor_stop\n",cases);return 0;
}
