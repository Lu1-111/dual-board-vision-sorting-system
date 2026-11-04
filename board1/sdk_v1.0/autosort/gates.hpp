#pragma once
#include "traverse/vision.hpp"
#include "sorting_runtime/sorting_observer.hpp"
#include "parking_region.hpp"
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QStringList>
#include <cmath>
namespace autosort {
using trial::require;
inline QJsonArray baseline(){return {500,1495.7,539.2,500,1252.6,1656};}
inline QJsonObject expectedState(const QJsonObject &a){
    QJsonObject out;
    for(const QString &k:QStringList{"consoleSessionId","commandGeneration","actionJobId","actionJobState","actionFile","loadedActionSha256","mode","enabled","debug","holdingAfterTimeout","visionReference"}){
        require(a.contains(k),"automation UI protocol fields missing");out[k]=a[k];
    }return out;
}
inline bool initialHardwareConnection(const QJsonObject &a){
    return a.value("connected")==true&&a.value("mode")=="hardware"&&a.value("hardwareStatusAvailable")==true&&a.value("enabled")==false&&a.value("hardwareOutputActive")==false&&a.value("actionJobId").toInt(-1)==0&&a.value("actionJobState")=="idle";
}
inline void idle(const QJsonObject &a,bool allowInitialConnection=false){
    require(a.value("ok")==true&&a.value("automationProtocol").toInt()==1,"guarded UI unavailable");
    for(const auto k:{"moving","actionPlaying","returningBeforeOff"})require(a.value(k)==false,"arm moving or timeout-latched");
    require(a.value("holdingAfterTimeout")==false||(allowInitialConnection&&initialHardwareConnection(a)),"arm moving or timeout-latched");
    require(a.value("error").toString().isEmpty()&&a.value("uiError").toString().isEmpty(),"arm error");
    require(a.value("sortJobId").toInt(-1)==0&&a.value("sortJobState").toString()=="idle","other sorting owner");
}
inline QJsonObject model(QJsonObject a){auto r=a.value("visionReference").toObject();r.remove("pulses");r["consoleSessionId"]=a.value("consoleSessionId");return r;}
inline void operating(const QJsonObject &a,int job,const QString &generation,const QJsonObject &reference){
    require(a.value("ok")==true&&a.value("automationProtocol").toInt()==1,"arm unavailable");
    require(a.value("actionJobId").toInt(-1)==job&&a.value("commandGeneration").toString()==generation,"arm ownership changed");
    require(a.value("enabled")==true&&a.value("debug")==true&&a.value("initialPoseAcknowledged")==true&&a.value("mode")=="hardware"&&a.value("hardwareOutputActive")==true&&a.value("hardwareStatusAvailable")==true,"arm output state changed");
    require(a.value("holdingAfterTimeout")==false&&a.value("returningBeforeOff")==false&&a.value("error").toString().isEmpty()&&a.value("uiError").toString().isEmpty(),"arm fault");
    require(model(a)==reference,"arm model changed");
}
// Repeated packets never satisfy time or frame-count stability requirements.
class Presence {
    QString session_,id_;quint64 seq_=0;qint64 capture_=-1,begin_=-1;int count_=0;QJsonObject raw_;QPointF anchor_;
public:
    void reset(){*this=Presence();}
    bool accept(const trial::Frame &f,int duration=500){
        if(session_!=f.session||id_!=f.id){reset();session_=f.session;id_=f.id;}
        if(count_){require(f.seq>=seq_,"presence sequence regressed");if(f.seq==seq_){require(f.raw==raw_,"duplicate changed");return false;}require(f.captured>capture_,"capture did not advance");}
        if(begin_<0||std::hypot(f.center.x()-anchor_.x(),f.center.y()-anchor_.y())>12){begin_=f.captured;count_=0;anchor_=f.center;}
        seq_=f.seq;capture_=f.captured;raw_=f.raw;++count_;
        return count_>=3&&capture_-begin_>=duration;
    }
};
inline QString classification(const QJsonObject &v,qint64 now,const QJsonObject &arrival,const trial::Frame &t,const QString &imageDir,bool transit=false){
    try{
    require(v.value("schema")==1&&v.value("status")=="ok"&&v.value("source")=="imx415"&&v.value("model")=="V28"&&v.value("threshold")==0.88,"V28 producer mismatch");
    require(v.value("source_session").toString()==t.session&&t.session==arrival.value("source_session").toString()&&t.id==arrival.value("track_id").toString(),"classification source or target changed");
    const auto pub=v.value("published_monotonic_ms"),age=v.value("source_frame_age_ms");
    require(pub.isDouble()&&age.isDouble()&&std::isfinite(pub.toDouble())&&std::isfinite(age.toDouble())&&pub.toDouble()>=0&&pub.toDouble()<=now&&age.toDouble()>=0&&age.toDouble()+now-pub.toDouble()<=30000,"V28 stale/future");
    require((transit?double(t.captured)-250:pub.toDouble()-age.toDouble()-50)>arrival.value("capture_fence_monotonic_ms").toDouble(),"classification before capture fence");
    // Reuse the belt polygon and full-box/uniqueness checks without treating the
    // slow inference result as the fast live localization stream.
    QJsonObject adapted=v;adapted["purpose"]="localization_only";adapted["coordinate_frame"]="camera_pixel";adapted["localization_width"]=320;adapted["sampling_step_px"]=QJsonArray{6,6};adapted["published_monotonic_ms"]=double(now);adapted["source_frame_age_ms"]=0;
    QJsonArray objects;for(auto value:v.value("objects").toArray()){auto o=value.toObject();o["partial"]=false;o["ambiguous"]=false;o["track_id"]="0";objects.append(o);}adapted["objects"]=objects;
    const auto classified=trial::readFrame(adapted,now,trial::defaultPolygon());
    bool ok=false;const auto fenceSeq=arrival.value("frame_sequence").toString().toULongLong(&ok);require(ok&&classified.seq>fenceSeq,"classification sequence before fence");
    require(t.seq>=classified.seq&&t.captured+1>=pub.toDouble()-age.toDouble(),"tracking predates classification");
    if(transit)require(t.seq==classified.seq&&t.captured-(pub.toDouble()-age.toDouble())<=2000,"transit frame association failed");
    if(!transit)require(inParkingRegion(t.center),"target outside parking region");
    require(std::hypot(t.center.x()-classified.center.x(),t.center.y()-classified.center.y())<=24,"classification association failed");
    require(sorting::snapshotBound(v,imageDir),"classification image binding failed");
    QString label;for(const auto &value:v.value("objects").toArray()){
        auto o=value.toObject();auto c=o.value("center_xy").toArray();if(c.size()!=2||std::hypot(c[0].toDouble()-classified.center.x(),c[1].toDouble()-classified.center.y())>.01)continue;
        const auto score=o.value("score");label=o.value("label").toString();require(score.isDouble()&&std::isfinite(score.toDouble())&&score.toDouble()>=0&&score.toDouble()<=1&&((label=="NORMAL"&&score.toDouble()<.88)||(label=="DEFECT"&&score.toDouble()>=.88)),"unknown or inconsistent classification");
    }require(!label.isEmpty(),"no scored target");return label;
    }catch(const trial::VisionUnavailable&){throw;}catch(const std::exception &e){throw trial::VisionUnavailable(e.what());}
}
}
