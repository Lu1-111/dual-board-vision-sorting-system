#include "sorting_observer.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>
namespace sorting {
namespace {
void require(bool yes,const char *reason){if(!yes)throw std::runtime_error(reason);}
double number(const QJsonValue &v,double lo,double hi){require(v.isDouble(),"number required");double n=v.toDouble();require(std::isfinite(n)&&n>=lo&&n<=hi,"number out of bounds");return n;}
bool truth(const QJsonObject &o,const char *key){return o.value(key).isBool()&&o.value(key).toBool();}
bool falsity(const QJsonObject &o,const char *key){return o.value(key).isBool()&&!o.value(key).toBool();}
quint64 sequence(const QJsonValue &v){require(v.isString(),"sequence string required");const QString s=v.toString();require(QRegularExpression("^[0-9]{1,20}$").match(s).hasMatch(),"invalid sequence");bool ok=false;const auto n=s.toULongLong(&ok);require(ok,"sequence overflow");return n;}
struct Frame {QString session;quint64 seq;qint64 captured;QJsonObject object;QPointF center;};
Frame parseFrame(const QJsonObject &o,qint64 now,double limit,double guard,bool tracking){
    require(o.value("schema").toDouble(-1)==1&&o.value("status").toString()=="ok","source unavailable");
    require(o.value("width").toDouble()==1920&&o.value("height").toDouble()==1080,"original dimensions required");
    require(o.value("source").toString()=="imx415","physical source required");
    Frame f;f.session=o.value("source_session").toString();require(QRegularExpression("^[A-Za-z0-9_-]{1,128}$").match(f.session).hasMatch(),"invalid session");f.seq=sequence(o.value("frame_sequence"));
    const double published=number(o.value("published_monotonic_ms"),0,double(now));
    const double age=number(o.value("source_frame_age_ms"),0,limit);
    require(age+double(now)-published<=limit,"source stale");f.captured=qint64(std::floor(published-age-guard));
    require(o.value("objects").isArray()&&o.value("objects").toArray().size()==1,"exactly one object required");
    require(o.value("objects").toArray()[0].isObject(),"object required");f.object=o.value("objects").toArray()[0].toObject();
    const auto c=f.object.value("center_xy").toArray(),b=f.object.value("bbox_xyxy").toArray();require(c.size()==2&&b.size()==4,"geometry required");
    const double x=number(c[0],0,1920),y=number(c[1],0,1080),x0=number(b[0],0,1920),y0=number(b[1],0,1080),x1=number(b[2],0,1920),y1=number(b[3],0,1080);
    require(x0>0&&y0>0&&x1<1920&&y1<1080&&x0<x1&&y0<y1&&x>=x0&&x<=x1&&y>=y0&&y<=y1,"partial/invalid geometry");f.center=QPointF(x,y);
    if(tracking){
        require(o.value("purpose").toString()=="localization_only"&&o.value("coordinate_frame").toString()=="camera_pixel"&&o.value("localization_width").toInt()==320&&o.value("sampling_step_px").toArray()==QJsonArray{6,6},"tracking producer contract mismatch");
        require(falsity(f.object,"partial")&&falsity(f.object,"ambiguous"),"partial/ambiguous tracking");sequence(f.object.value("track_id"));
    }
    return f;
}
double distance(const QPointF &a,const QPointF &b){return std::hypot(a.x()-b.x(),a.y()-b.y());}
bool inside(const QPointF &p,const QJsonArray &r){return p.x()>=r[0].toDouble()&&p.y()>=r[1].toDouble()&&p.x()<=r[2].toDouble()&&p.y()<=r[3].toDouble();}
}
QJsonObject readObject(const QString &path,qint64 maximum){QFile f(path);require(f.open(QIODevice::ReadOnly),"cannot open JSON");require(f.size()>0&&f.size()<=maximum,"JSON size bound");QJsonParseError e;auto d=QJsonDocument::fromJson(f.readAll(),&e);require(e.error==QJsonParseError::NoError&&d.isObject(),"incomplete JSON");return d.object();}
QJsonObject parseBeltStatus(const QByteArray &bytes){
    const QString text=QString::fromLatin1(bytes);QRegularExpression re("ID=0x([0-9a-fA-F]+) features=0x([0-9a-fA-F]+) status=0x([0-9a-fA-F]+) duty_ticks=([0-9]+).*encoder=([0-9]+) encoder_errors=([0-9]+)");auto m=re.match(text);require(m.hasMatch(),"belt readback format");
    bool ok=false;const auto id=m.captured(1).toUInt(&ok,16);require(ok&&id==0x434e5601,"belt ID mismatch");
    const uint features=m.captured(2).toUInt(&ok,16);require(ok,"features invalid");const uint status=m.captured(3).toUInt(&ok,16);require(ok,"status invalid");
    const auto duty=m.captured(4).toULongLong(&ok);require(ok,"duty invalid");const auto enc=m.captured(5).toULongLong(&ok);require(ok&&enc<=0xffffffffULL,"encoder invalid");const auto errors=m.captured(6).toULongLong(&ok);require(ok&&errors<=0xffffffffULL,"encoder errors invalid");
    return {{"ok",true},{"features",int(features)},{"status",double(status)},{"disabled",!(status&1)&&duty==0},{"fault",bool(status&0x700)||errors!=0},{"encoder",QString::number(enc)}};
}
QString currentMappingId(const QJsonObject &arm){
    const auto ref=arm.value("visionReference").toObject();auto axes=ref.value("axes").toArray();require(axes.size()==6&&arm.value("debug").isBool(),"mapping status missing");
    for(int i=0;i<6;++i){auto a=axes[i].toArray();require(a.size()==7,"mapping axis invalid");for(const auto &v:a)number(v,-100000,100000);
        if(arm.value("debug").toBool()){a[2]=0;a[3]=180;a[4]=500;a[5]=2499.2;a[6]=0;axes[i]=a;}}
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(axes).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
QJsonObject verifyActions(const QJsonObject &cfg,const QJsonObject &arm){
    const QString mapping=currentMappingId(arm);require(cfg.value("mapping_id").toString()==mapping,"current mapping differs from configured actions");
    const auto actions=cfg.value("actions").toObject();require(actions.size()==2&&actions.contains("NORMAL")&&actions.contains("DEFECT"),"two whitelisted actions required");
    const auto axes=arm.value("visionReference").toObject().value("axes").toArray();QJsonObject hashes;
    for(const QString &label:{QString("NORMAL"),QString("DEFECT")}){const auto a=actions.value(label).toObject();const QString path=a.value("path").toString(),expected=a.value("sha256").toString();QFileInfo info(path);require(info.isAbsolute()&&info.isFile()&&!info.isSymLink()&&info.size()<=1024*1024,"action path invalid");QFile f(path);require(f.open(QIODevice::ReadOnly),"action unreadable");const auto bytes=f.readAll();const QString hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());require(expected.size()==64&&expected==hash,"action hash changed");
        const auto obj=readObject(path,1024*1024);require(obj.value("version").toInt()==1&&obj.value("angleMappingId").toString()==mapping,"action mapping/version mismatch");const auto frames=obj.value("frames").toArray();require(frames.size()==7,"expected seven action frames");
        for(const auto &v:frames){const auto frame=v.toObject();const auto angles=frame.value("anglesDeg").toArray();require(angles.size()==6,"six action angles required");for(int i=0;i<6;++i){const auto axis=axes[i].toArray();number(angles[i],arm.value("debug").toBool()?0:axis[2].toDouble(),arm.value("debug").toBool()?180:axis[3].toDouble());}const double duration=number(frame.value("durationMs"),20,60000),hold=number(frame.value("holdMs"),0,60000);require(std::floor(duration)==duration&&std::floor(hold)==hold,"integer action times required");}hashes[label]=hash;
    }return {{"ok",true},{"mapping_id",mapping},{"hashes",hashes}};
}
bool snapshotBound(const QJsonObject &r,const QString &directory){
    const QString path=r.value("frame_path").toString(),session=r.value("source_session").toString(),seq=r.value("frame_sequence").toString();QFileInfo info(path);const QString parent=QFileInfo(directory).canonicalFilePath();
    if(parent.isEmpty()||!info.isAbsolute()||!info.isFile()||info.isSymLink()||info.canonicalPath()!=parent||info.fileName()!=QString("frame_%1_%2.jpg").arg(session,seq)||info.size()<4||info.size()>8*1024*1024)return false;
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return false;const QByteArray b=f.readAll();if(!b.startsWith(QByteArray::fromHex("ffd8"))||!b.endsWith(QByteArray::fromHex("ffd9")))return false;
    int i=2;while(i+3<b.size()){if(uchar(b[i++])!=255)return false;while(i<b.size()&&uchar(b[i])==255)++i;if(i>=b.size())return false;const int marker=uchar(b[i++]);if(marker==0xda||marker==0xd9)return false;if(marker==1||(marker>=0xd0&&marker<=0xd7))continue;if(i+2>b.size())return false;const int len=(uchar(b[i])<<8)|uchar(b[i+1]);if(len<2||i+len>b.size())return false;if((marker>=0xc0&&marker<=0xc3)||(marker>=0xc5&&marker<=0xc7)||(marker>=0xc9&&marker<=0xcb)||(marker>=0xcd&&marker<=0xcf)){if(len<8)return false;return ((uchar(b[i+3])<<8)|uchar(b[i+4]))==1080&&((uchar(b[i+5])<<8)|uchar(b[i+6]))==1920;}i+=len;}return false;
}
void Observer::resetStable(){stableBegin_=-1;fence_=-1;stableFrames_=0;trackId_.clear();stoppedObservedAt_=-1;}
QJsonObject Observer::sample(qint64 now,const QJsonObject &tracking,const QJsonObject &v28,bool verified,const QJsonObject &arm,const QJsonObject &belt){
    QJsonObject out{{"schema",1},{"mode","observe_only"},{"motion_allowed",false},{"mechanical_execution_lock","Existing control API has no atomic idle/load/play lease. No motion commands are implemented."},{"state","WAIT_TRACK"},{"published_monotonic_ms",double(now)}};
    try {
        require(now>=0&&now>=lastNow_,"monotonic clock unavailable/regressed");lastNow_=now;const auto t=parseFrame(tracking,now,500,0,true);out["center_xy"]=QJsonArray{t.center.x(),t.center.y()};out["source_session"]=t.session;out["frame_sequence"]=QString::number(t.seq);out["track_id"]=t.object.value("track_id");
        out["reference_only"]=config_.value("reference_only");
        const bool configured=truth(config_,"region_confirmed")&&truth(config_,"calibration_confirmed")&&truth(config_,"measured");
        if(!configured){resetStable();out["state"]="UNCONFIGURED_REFERENCE_ONLY";out["reason"]="Reference center and proposed pixel region are not measured stop/grasp calibration";return out;}
        const auto region=config_.value("region").toArray();require(region.size()==4,"parking region unset");const double x0=number(region[0],0,1920),y0=number(region[1],0,1080),x1=number(region[2],0,1920),y1=number(region[3],0,1080);require(x0<x1&&y0<y1,"parking region invalid");
        const double settle=number(config_.value("settle_ms"),1,60000),radius=number(config_.value("stable_radius_px"),1,1920),association=number(config_.value("association_px"),1,1920),guard=number(config_.value("capture_guard_ms"),0,5000),v28Age=number(config_.value("v28_max_age_ms"),1,30000);const double count=number(config_.value("stable_frames"),2,1000);require(std::floor(count)==count,"integer stable frames required");
        if(session_!=t.session){session_=t.session;haveSequence_=false;resetStable();}
        require(!haveSequence_||t.seq>=sequence_,"tracking sequence regressed");const bool fresh=!haveSequence_||t.seq>sequence_;require(fresh||lastTracking_==tracking,"duplicate frame metadata changed");sequence_=t.seq;haveSequence_=true;lastTracking_=tracking;
        const double beltRead=number(belt.value("read_monotonic_ms"),0,double(now));require(now-beltRead<=500&&truth(belt,"ok")&&belt.value("features").toInt()==6&&truth(belt,"disabled")&&falsity(belt,"fault"),"belt stopped readback unavailable");
        require(truth(arm,"ok")&&truth(arm,"connected")&&arm.value("mode").toString()=="hardware"&&falsity(arm,"moving")&&falsity(arm,"actionPlaying")&&arm.value("actionJobState").toString()!="running"&&!arm.value("error").toString().size()&&!arm.value("uiError").toString().size(),"arm not idle/healthy");
        out["arm_pre_enabled"]=truth(arm,"enabled")&&truth(arm,"initialPoseAcknowledged");
        if(!inside(t.center,region)){resetStable();out["state"]="OUTSIDE_REGION";out["reason"]="No jog issued by observer";return out;}
        if(trackId_!=t.object.value("track_id").toString()||distance(t.center,anchor_)>radius){resetStable();trackId_=t.object.value("track_id").toString();anchor_=t.center;}
        if(stoppedObservedAt_<0)stoppedObservedAt_=now;
        if(fresh&&t.captured-guard>stoppedObservedAt_){if(stableBegin_<0)stableBegin_=t.captured; ++stableFrames_;if(fence_<0&&stableFrames_>=count&&t.captured-stableBegin_>=settle){fence_=now;fenceSequence_=t.seq;}}
        out["stable_frames"]=stableFrames_;if(fence_<0){out["state"]="OBSERVE_SETTLING";return out;}out["fence_monotonic_ms"]=double(fence_);out["state"]="WAIT_POST_SETTLE_CLASSIFICATION";
        const auto c=parseFrame(v28,now,v28Age,guard,false);require(c.session==session_&&c.seq>fenceSequence_&&c.captured>fence_,"classification capture predates settle fence");
        require(v28.value("model").toString()=="V28"&&std::abs(number(v28.value("threshold"),0,1)-.88)<1e-6,"V28 model/threshold mismatch");require(verified,"snapshot binding not verified");
        const QString label=c.object.value("label").toString();const double score=number(c.object.value("score"),0,1);require((label=="NORMAL"&&score<.88)||(label=="DEFECT"&&score>=.88),"unscored or inconsistent defect probability");require(inside(c.center,region)&&distance(t.center,c.center)<=association,"classification/track geometric mismatch");
        out["decision"]=label;out["defect_probability"]=score;out["decision_frame_sequence"]=QString::number(c.seq);out["state"]="CLASSIFICATION_OBSERVED_EXECUTION_LOCKED";
        try{out["action_verification"]=verifyActions(config_,arm);}catch(const std::exception &e){out["action_verification"]=QJsonObject{{"ok",false},{"reason",QString::fromUtf8(e.what())}};}
        out["reason"]="No load/play/jog/stop/enable sent; observer cannot execute a cycle";
    }catch(const std::exception &e){out["reason"]=QString::fromUtf8(e.what());if(out.value("state").toString()!="WAIT_POST_SETTLE_CLASSIFICATION"){resetStable();out["state"]="HOLD_OBSERVATION";}}
    return out;
}
}
