#include "sorting_observer.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
static QJsonObject config(){return {{"measured",true},{"calibration_confirmed",true},{"region_confirmed",true},{"region",QJsonArray{900,400,1100,600}},
    {"settle_ms",150},{"stable_frames",3},{"stable_radius_px",12},{"association_px",12},{"capture_guard_ms",0},{"v28_max_age_ms",15000}};}
static QJsonObject track(int time,int seq){return {{"schema",1},{"status","ok"},{"source","imx415"},{"source_session","test-one"},{"frame_sequence",QString::number(seq)},
    {"published_monotonic_ms",time},{"source_frame_age_ms",1},{"width",1920},{"height",1080},{"purpose","localization_only"},{"coordinate_frame","camera_pixel"},{"localization_width",320},{"sampling_step_px",QJsonArray{6,6}},
    {"objects",QJsonArray{QJsonObject{{"center_xy",QJsonArray{1000,500}},{"bbox_xyxy",QJsonArray{950,450,1050,550}},{"track_id","7"},{"partial",false},{"ambiguous",false}}}}};}
static QJsonObject result(int time=1400,int seq=5){auto r=track(time,seq);auto obj=r["objects"].toArray()[0].toObject();obj.remove("partial");obj.remove("ambiguous");obj.remove("track_id");obj["label"]="NORMAL";obj["score"]=.0035;r["objects"]=QJsonArray{obj};r["model"]="V28";r["threshold"]=.88;return r;}
static QJsonObject arm(){QJsonArray axes;for(int i=0;i<6;++i)axes.append(QJsonArray{i,1,0,180,500,2499.2,0});return {{"ok",true},{"connected",true},{"mode","hardware"},{"moving",false},{"actionPlaying",false},{"enabled",false},{"initialPoseAcknowledged",false},{"debug",false},{"actionJobState","completed"},{"visionReference",QJsonObject{{"axes",axes}}}};}
static QJsonObject belt(int time){return {{"ok",true},{"features",6},{"disabled",true},{"fault",false},{"read_monotonic_ms",time}};}
static void settle(sorting::Observer &o){for(int i=0;i<4;++i)o.sample(1000+i*100,track(1000+i*100,i+1),QJsonObject(),false,arm(),belt(1000+i*100));}
static bool write(const QString &path,const QByteArray &bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();}
int main(int argc,char **argv){QCoreApplication app(argc,argv);int cases=0;
    {sorting::Observer o(QJsonObject{});auto out=o.sample(1000,track(1000,1),{},false,arm(),belt(1000));CHECK(out["state"]=="UNCONFIGURED_REFERENCE_ONLY"&&!out["motion_allowed"].toBool());++cases;}
    {auto cfg=config();cfg["measured"]=false;sorting::Observer o(cfg);CHECK(o.sample(1000,track(1000,1),{},false,arm(),belt(1000))["state"]=="UNCONFIGURED_REFERENCE_ONLY");++cases;}
    {sorting::Observer o(config());settle(o);auto out=o.sample(1400,track(1400,5),result(),true,arm(),belt(1400));CHECK(out["state"]=="CLASSIFICATION_OBSERVED_EXECUTION_LOCKED"&&out["decision"]=="NORMAL"&&!out["motion_allowed"].toBool());CHECK(!out["arm_pre_enabled"].toBool());++cases;}
    for(int mode=0;mode<13;++mode){sorting::Observer o(config());settle(o);auto t=track(1400,5),r=result(),a=arm(),b=belt(1400);bool bound=true;
        if(mode==0){auto x=r["objects"].toArray()[0].toObject();x["label"]="DEFECT";r["objects"]=QJsonArray{x};}
        if(mode==1){auto x=r["objects"].toArray()[0].toObject();x["label"]="REVIEW";x["score"]=QJsonValue::Null;r["objects"]=QJsonArray{x};}
        if(mode==2)r["source_frame_age_ms"]=200;
        if(mode==3)r["frame_sequence"]="4";
        if(mode==4)r["source_session"]="other";
        if(mode==5)r["objects"]=QJsonArray{};
        if(mode==6){auto objects=r["objects"].toArray();objects.append(objects[0]);r["objects"]=objects;}
        if(mode==7)bound=false;
        if(mode==8){auto x=t["objects"].toArray()[0].toObject();x["ambiguous"]=true;t["objects"]=QJsonArray{x};}
        if(mode==9){auto x=t["objects"].toArray()[0].toObject();x["partial"]=true;t["objects"]=QJsonArray{x};}
        if(mode==10)a["moving"]=true;
        if(mode==11)b["disabled"]=false;
        if(mode==12)t["source"]="simulation";
        auto out=o.sample(1400,t,r,bound,a,b);CHECK(!out.contains("decision")&&!out["motion_allowed"].toBool());++cases;
    }
    {sorting::Observer o(config());settle(o);auto r=result();auto x=r["objects"].toArray()[0].toObject();x["label"]="DEFECT";x["score"]=.88;r["objects"]=QJsonArray{x};CHECK(o.sample(1400,track(1400,5),r,true,arm(),belt(1400))["decision"]=="DEFECT");++cases;}
    {sorting::Observer o(config());o.sample(1000,track(1000,1),{},false,arm(),belt(1000));auto duplicate=track(1100,1);CHECK(o.sample(1100,duplicate,{},false,arm(),belt(1100))["state"]=="HOLD_OBSERVATION");++cases;}
    {sorting::Observer o(config());auto t=track(1000,1);CHECK(o.sample(1600,t,{},false,arm(),belt(1600))["state"]=="HOLD_OBSERVATION");++cases;}
    {sorting::Observer o(config());settle(o);auto t=track(1400,1);t["source_session"]="new-session";auto out=o.sample(1400,t,result(),true,arm(),belt(1400));CHECK(out["state"]=="OBSERVE_SETTLING"&&!out.contains("decision"));++cases;}
    {auto b=sorting::parseBeltStatus("ID=0x434e5601 features=0x6 status=0x0 duty_ticks=0 period=3750 clock_hz=75000000 watchdog_ticks=18750000 encoder=42 encoder_errors=0\n");CHECK(b["disabled"].toBool()&&!b["fault"].toBool()&&b["encoder"]=="42");
     b=sorting::parseBeltStatus("ID=0x434e5601 features=0x6 status=0x101 duty_ticks=3750 period=3750 encoder=42 encoder_errors=0");CHECK(!b["disabled"].toBool()&&b["fault"].toBool());++cases;}
    QTemporaryDir dir;CHECK(dir.isValid());const QString image=dir.path()+"/frame_test-one_5.jpg";auto r=result();r["frame_path"]=image;
    // Structural JPEG-header fixture. This test does not claim image decode/model accuracy.
    CHECK(write(image,QByteArray::fromHex("ffd8ffc0000b080438078001011100ffd9")));CHECK(sorting::snapshotBound(r,dir.path()));r["frame_sequence"]="6";CHECK(!sorting::snapshotBound(r,dir.path()));r["frame_sequence"]="5";
    CHECK(write(image,QByteArray::fromHex("ffd8ffc0000b080438078001011100")));CHECK(!sorting::snapshotBound(r,dir.path()));++cases;
    {auto cfg=config(),a=arm();const QString mapping=sorting::currentMappingId(a);cfg["mapping_id"]=mapping;QJsonArray frames;for(int i=0;i<7;++i)frames.append(QJsonObject{{"anglesDeg",QJsonArray{0,1,2,3,4,5}},{"durationMs",1000},{"holdMs",0}});
      const QByteArray bytes=QJsonDocument(QJsonObject{{"version",1},{"angleMappingId",mapping},{"frames",frames}}).toJson();const QString path=dir.path()+"/actions.json";CHECK(write(path,bytes));const QString hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());const QJsonObject item{{"path",path},{"sha256",hash}};cfg["actions"]=QJsonObject{{"NORMAL",item},{"DEFECT",item}};CHECK(sorting::verifyActions(cfg,a)["ok"].toBool());CHECK(write(path,bytes+" "));bool rejected=false;try{sorting::verifyActions(cfg,a);}catch(...){rejected=true;}CHECK(rejected);++cases;}
    std::printf("SORTING_OBSERVER_PASS cases=%d; native read-only decision fixtures; no movement APIs; not board validation\n",cases);return 0;
}
