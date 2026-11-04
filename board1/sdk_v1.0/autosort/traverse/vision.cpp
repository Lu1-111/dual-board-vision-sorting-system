#include "vision.hpp"
#include "../sorting_runtime/sorting_observer.hpp"
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>
namespace trial {
void require(bool c,const char *r){if(!c)throw std::runtime_error(r);}
void visionRequire(bool c,const char *r){if(!c)throw VisionUnavailable(r);}
static double number(const QJsonValue &v,double lo,double hi){require(v.isDouble(),"number missing");double x=v.toDouble();require(std::isfinite(x)&&x>=lo&&x<=hi,"number invalid");return x;}
static bool yes(const QJsonObject&o,const char*k){return o.value(k).isBool()&&o.value(k).toBool();}
static bool no(const QJsonObject&o,const char*k){return o.value(k).isBool()&&!o.value(k).toBool();}
static double cross(QPointF a,QPointF b,QPointF p){return (b.x()-a.x())*(p.y()-a.y())-(b.y()-a.y())*(p.x()-a.x());}
static bool inside(QPointF p,const QVector<QPointF>& poly){for(int i=0;i<poly.size();++i)if(cross(poly[i],poly[(i+1)%poly.size()],p)<0)return false;return true;}
static bool strictlyInside(QPointF p,const QVector<QPointF>& poly){for(int i=0;i<poly.size();++i)if(cross(poly[i],poly[(i+1)%poly.size()],p)<=0)return false;return true;}
static bool intersects(QPointF a,QPointF b,QPointF c,QPointF d){return cross(a,b,c)*cross(a,b,d)<=0&&cross(c,d,a)*cross(c,d,b)<=0&&qMax(qMin(a.x(),b.x()),qMin(c.x(),d.x()))<=qMin(qMax(a.x(),b.x()),qMax(c.x(),d.x()))&&qMax(qMin(a.y(),b.y()),qMin(c.y(),d.y()))<=qMin(qMax(a.y(),b.y()),qMax(c.y(),d.y()));}
QVector<QPointF> defaultPolygon(){return {{0,300},{1470,390},{1470,660},{0,850}};}
void validatePolygon(const QVector<QPointF>& p){require(p.size()==4,"four polygon points required");for(int i=0;i<4;++i){require(std::isfinite(p[i].x())&&std::isfinite(p[i].y())&&p[i].x()>=0&&p[i].x()<=1920&&p[i].y()>=0&&p[i].y()<=1080,"polygon outside image");require(cross(p[i],p[(i+1)%4],p[(i+2)%4])>0,"polygon must be strictly convex clockwise image order");}}
static Frame readFrameChecked(const QJsonObject &o,qint64 now,const QVector<QPointF>& poly,bool allowEmpty){
    require(o.value("schema").toDouble(-1)==1&&o.value("status").toString()=="ok"&&o.value("source").toString()=="imx415","tracking unavailable/physical source mismatch");
    require(o.value("width").toInt()==1920&&o.value("height").toInt()==1080&&o.value("purpose").toString()=="localization_only"&&o.value("coordinate_frame").toString()=="camera_pixel"&&o.value("localization_width").toInt()==320&&o.value("sampling_step_px").toArray()==QJsonArray{6,6},"tracking producer contract mismatch");
    const double pub=number(o.value("published_monotonic_ms"),0,now),age=number(o.value("source_frame_age_ms"),0,500);require(age+now-pub<=500,"tracking older than500ms");Frame f;f.captured=qint64(std::floor(pub-age));f.raw=o;f.session=o.value("source_session").toString();require(QRegularExpression("^[A-Za-z0-9_-]{1,128}$").match(f.session).hasMatch(),"session invalid");
    auto decimal=[](QJsonValue v)->quint64{require(v.isString()&&QRegularExpression("^[0-9]{1,20}$").match(v.toString()).hasMatch(),"sequence/id invalid");bool ok=false;quint64 n=v.toString().toULongLong(&ok);require(ok,"sequence overflow");return n;};f.seq=decimal(o.value("frame_sequence"));
    require(o.value("objects").isArray()&&o.value("objects").toArray().size()<=128,"objects invalid");int candidates=0;
    for(const auto &v:o.value("objects").toArray()){
        require(v.isObject(),"object invalid");const auto obj=v.toObject();const auto center=obj.value("center_xy").toArray(),box=obj.value("bbox_xyxy").toArray();
        require(center.size()==2&&box.size()==4,"geometry missing");QPointF c(number(center[0],0,1920),number(center[1],0,1080));double x0=number(box[0],0,1920),y0=number(box[1],0,1080),x1=number(box[2],0,1920),y1=number(box[3],0,1080);require(x0<x1&&y0<y1&&c.x()>=x0&&c.x()<=x1&&c.y()>=y0&&c.y()<=y1,"geometry bounds");
        QVector<QPointF> corners{{x0,y0},{x1,y0},{x1,y1},{x0,y1}};bool full=true,hit=inside(c,poly);for(auto p:corners){full=full&&strictlyInside(p,poly);hit=hit||inside(p,poly);}for(auto p:poly)hit=hit||(p.x()>=x0&&p.x()<=x1&&p.y()>=y0&&p.y()<=y1);for(int i=0;i<4;++i)for(int j=0;j<4;++j)hit=hit||intersects(corners[i],corners[(i+1)%4],poly[j],poly[(j+1)%4]);
        if(!hit)continue; // Wholly off-belt false positives are not conveyor targets.
        require(full&&x0>0&&y0>0&&x1<1920&&y1<1080&&no(obj,"partial")&&no(obj,"ambiguous"),"on-belt object partial/ambiguous/crossing edge");
        ++candidates;f.center=c;decimal(obj.value("track_id"));f.id=obj.value("track_id").toString();
    }
    if(candidates==0&&allowEmpty)return f;
    require(candidates==1,"exactly one complete on-belt object required");require(f.center.x()<=1335,"overshoot x>1335");return f;
}
Frame readFrame(const QJsonObject &o,qint64 now,const QVector<QPointF>& poly,bool allowEmpty){
    try{return readFrameChecked(o,now,poly,allowEmpty);}
    catch(const std::runtime_error &e){throw VisionUnavailable(e.what());}
}
void checkArm(const QJsonObject&a,int expectedJob){
    require(yes(a,"ok")&&yes(a,"connected")&&a.value("mode").toString()=="hardware"&&yes(a,"enabled")&&yes(a,"debug")&&yes(a,"initialPoseAcknowledged")&&yes(a,"hardwareStatusAvailable")&&yes(a,"hardwareOutputActive"),"arm enable/debug/feedback baseline changed");
    require(no(a,"moving")&&no(a,"actionPlaying")&&no(a,"returningBeforeOff")&&no(a,"holdingAfterTimeout")&&a.value("actionJobId").toDouble(-1)==expectedJob&&(a.value("actionJobState").toString()=="completed"||a.value("actionJobState").toString()=="idle")&&a.value("error").toString().isEmpty()&&a.value("uiError").toString().isEmpty(),"arm job/motion/error changed");
    require(a.value("actionFile").toString()=="/home/uisrc/qt_play/actions/fixed_pick_20261021/actions/perfect_selection.json"&&a.value("sortJobState").toString()=="idle"&&a.value("sortJobId").toDouble(-1)==0,"arm selected action/sort job changed");
    const double expected[]={500,1495.7,539.2,500,1252.6,1656};const auto p=a.value("targetPulses").toArray();require(p.size()==6,"arm target missing");for(int i=0;i<6;++i)require(std::abs(number(p[i],500,2499.2)-expected[i])<1e-6,"arm target changed");
    const auto commanded=a.value("commandedPulses").toArray();require(commanded.size()==6,"commanded baseline missing");for(int i=0;i<6;++i)require(std::abs(number(commanded[i],500,2499.2)-expected[i])<=.1,"commanded baseline changed");
    require(sorting::currentMappingId(a)=="faf77b03777460fd3f10132c520221664204a884c7cc7ff3dee3f647e8340cd0","debug mapping changed");
}
bool Guard::accept(const Frame&f){visionRequire(f.captured>=0,"negative capture clock");if(!initialized_){session_=f.session;id_=f.id;seq_=f.seq;captured_=f.captured;last_=f.raw;initialized_=true;return true;}visionRequire(f.session==session_&&f.id==id_,"camera session/target ID changed");visionRequire(f.seq>=seq_,"sequence regressed");if(f.seq==seq_){visionRequire(f.raw==last_,"duplicate frame metadata changed");return false;}visionRequire(f.captured>captured_,"new sequence capture clock did not advance");seq_=f.seq;captured_=f.captured;last_=f.raw;return true;}

}
