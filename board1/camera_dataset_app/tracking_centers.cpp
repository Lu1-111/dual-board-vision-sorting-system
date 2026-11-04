#include "tracking_centers.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QPainter>
#include <QSaveFile>
#include <QFont>
#include <QPen>
#include <stdexcept>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

namespace {
qint64 nowMs(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)throw std::runtime_error("monotonic clock unavailable");return qint64(t.tv_sec)*1000+t.tv_nsec/1000000;}
}
TrackingCenters::TrackingCenters():tracker_(320) {}

QJsonObject TrackingCenters::update(const QImage& full,const QString& session,quint64 sequence) {
    if(full.size()!=QSize(1920,1080))throw std::runtime_error("tracking requires original 1920x1080 image");
    if(session.isEmpty())throw std::runtime_error("tracking session is empty");
    if(session_!=session){tracker_.reset();session_=session;sequence_=0;windowMs_=0;windowCount_=0;fps_=0;}
    else if(sequence<=sequence_)throw std::runtime_error("tracking sequence did not advance");
    QElapsedTimer timer;timer.start();
    const QImage bgra=full.format()==QImage::Format_RGB32||full.format()==QImage::Format_ARGB32
            ? full : full.convertToFormat(QImage::Format_RGB32);
    // FastTracker::update/detect only read this Mat; the const source is never painted.
    const cv::Mat matrix(bgra.height(),bgra.width(),CV_8UC4,
                         const_cast<uchar*>(bgra.constBits()),size_t(bgra.bytesPerLine()));
    const auto& tracks=tracker_.update(matrix);
    if(tracks.size()>128)throw std::runtime_error("tracking object limit exceeded");
    const double trackingMs=timer.nsecsElapsed()/1000000.0;
    const qint64 now=nowMs();
    if(!windowMs_)windowMs_=now;else ++windowCount_;
    if(now-windowMs_>=1000){fps_=1000.0*windowCount_/double(now-windowMs_);windowCount_=0;windowMs_=now;}
    sequence_=sequence;
    QJsonArray objects;
    for(const auto& track:tracks){
        QJsonObject item;item["track_id"]=QString::number(track.id);
        item["bbox_xyxy"]=QJsonArray{track.box.x,track.box.y,track.box.br().x,track.box.br().y};
        item["center_xy"]=QJsonArray{double(track.center.x),double(track.center.y)};
        item["partial"]=track.partial;item["ambiguous"]=track.ambiguous;
        objects.append(item);
    }
    QJsonObject result;result["schema"]=1;result["status"]="ok";
    result["source_session"]=session;result["frame_sequence"]=QString::number(sequence);
    result["width"]=full.width();result["height"]=full.height();result["objects"]=objects;
    result["tracking_ms"]=trackingMs;result["fps"]=fps_;
    result["coordinate_frame"]="camera_pixel";result["localization_width"]=320;
    result["sampling_step_px"]=QJsonArray{6,6};result["purpose"]="localization_only";
    return result;
}

QJsonObject TrackingCenters::loadReference(const QString& path) {
    const QFileInfo info(path);
    if(!info.isFile()||info.isSymLink()||info.size()<2||info.size()>4096)return {};
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    const QByteArray bytes=file.read(4097);if(bytes.size()>4096)return {};
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject())return {};
    const auto object=document.object();
    const QSet<QString> keys{"schema","width","height","center_xy","region_xyxy","calibrated","motion_enabled"};
    for(const auto& key:keys)if(!object.contains(key))return {};
    if(!object.value("schema").isDouble()||object.value("schema").toDouble()!=1||
       !object.value("width").isDouble()||object.value("width").toDouble()!=1920||
       !object.value("height").isDouble()||object.value("height").toDouble()!=1080||
       !object.value("calibrated").isBool()||object.value("calibrated").toBool()||
       !object.value("motion_enabled").isBool()||object.value("motion_enabled").toBool())return {};
    const auto center=object.value("center_xy").toArray(),region=object.value("region_xyxy").toArray();
    if(center.size()!=2||region.size()!=4)return {};
    for(const auto& value:center)if(!value.isDouble()||!std::isfinite(value.toDouble()))return {};
    for(const auto& value:region)if(!value.isDouble()||!std::isfinite(value.toDouble()))return {};
    const double x=center[0].toDouble(),y=center[1].toDouble(),left=region[0].toDouble(),top=region[1].toDouble(),right=region[2].toDouble(),bottom=region[3].toDouble();
    if(left<0||top<0||right>1920||bottom>1080||left>=right||top>=bottom||x<left||x>right||y<top||y>bottom||x>=1920||y>=1080)return {};
    QJsonObject validated;for(const auto& key:keys)validated[key]=object.value(key);
    return validated; // Audit metadata is permitted but cannot affect rendering.
}

QImage TrackingCenters::overlay(const QImage& preview,const QJsonObject& tracking,const QJsonObject& reference) {
    QImage painted=preview;painted.detach();
    QPainter painter(&painted);painter.setRenderHint(QPainter::Antialiasing,false);
    QFont font("DejaVu Sans");font.setPixelSize(13);painter.setFont(font);
    const double sx=double(preview.width())/tracking.value("width").toInt();
    const double sy=double(preview.height())/tracking.value("height").toInt();
    for(const auto& value:tracking.value("objects").toArray()){
        const QJsonObject item=value.toObject();const auto box=item.value("bbox_xyxy").toArray();
        const auto center=item.value("center_xy").toArray();
        const bool uncertain=item.value("partial").toBool()||item.value("ambiguous").toBool();
        const QColor color=uncertain?QColor(255,190,40):QColor(20,255,180);
        painter.setPen(QPen(color,2));
        painter.drawRect(QRectF(box[0].toDouble()*sx,box[1].toDouble()*sy,
                               (box[2].toDouble()-box[0].toDouble())*sx,(box[3].toDouble()-box[1].toDouble())*sy));
        const QPointF c(center[0].toDouble()*sx,center[1].toDouble()*sy);
        painter.drawLine(c+QPointF(-5,0),c+QPointF(5,0));painter.drawLine(c+QPointF(0,-5),c+QPointF(0,5));
        QString label=QString("#%1 (%2,%3)%4").arg(item.value("track_id").toString())
                .arg(center[0].toDouble(),0,'f',0).arg(center[1].toDouble(),0,'f',0)
                .arg(uncertain?QStringLiteral(" ?"):QString());
        const int textWidth=painter.fontMetrics().horizontalAdvance(label)+6;
        const int x=qBound(0,int(box[0].toDouble()*sx),qMax(0,preview.width()-textWidth));
        const int y=qBound(16,int(box[1].toDouble()*sy)-3,qMax(16,preview.height()-2));
        painter.fillRect(QRect(x,y-14,qMin(textWidth,preview.width()),17),QColor(20,20,20,210));
        painter.drawText(QPoint(x+3,y),label);
    }
    // Reference coordinates come only from the validated, operator-written file.
    // They never depend on a detected object and never grant a motion permission.
    if(!reference.isEmpty()) {
        const auto box=reference.value("region_xyxy").toArray();const auto center=reference.value("center_xy").toArray();
        const QColor yellow(255,220,45);
        painter.setPen(QPen(yellow,2,Qt::DashLine));painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(box[0].toDouble()*sx,box[1].toDouble()*sy,
                               (box[2].toDouble()-box[0].toDouble())*sx,(box[3].toDouble()-box[1].toDouble())*sy));
        painter.setPen(QPen(yellow,2));
        const QPointF c(center[0].toDouble()*sx,center[1].toDouble()*sy);
        painter.drawLine(c+QPointF(-9,0),c+QPointF(9,0));painter.drawLine(c+QPointF(0,-9),c+QPointF(0,9));
        const QString label=QStringLiteral("REFERENCE / UNCALIBRATED");
        const int textWidth=painter.fontMetrics().horizontalAdvance(label)+8;
        const int x=qBound(0,int(box[0].toDouble()*sx),qMax(0,preview.width()-textWidth));
        const int y=qBound(16,int(box[3].toDouble()*sy)+20,qMax(16,preview.height()-3));
        painter.fillRect(QRect(x,y-14,qMin(textWidth,preview.width()),18),QColor(20,20,20,230));
        painter.drawText(QPoint(x+4,y),label);
    }
    painter.end();return painted;
}

TrackingPublisher::TrackingPublisher(const QString& directory):directory_(QDir::cleanPath(directory)) {
    if(!QDir::isAbsolutePath(directory_)||directory_=="/")throw std::runtime_error("tracking directory must be dedicated and absolute");
    if(!QDir().mkpath(directory_)||QFileInfo(directory_).isSymLink()||!QFileInfo(directory_).isDir())throw std::runtime_error("cannot create tracking directory");
    const QByteArray path=QFile::encodeName(directory_+"/.writer.lock");
    lockFd_=::open(path.constData(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lockFd_<0||flock(lockFd_,LOCK_EX|LOCK_NB)!=0){if(lockFd_>=0)::close(lockFd_);lockFd_=-1;throw std::runtime_error("tracking publisher already running");}
}
TrackingPublisher::~TrackingPublisher(){if(lockFd_>=0)::close(lockFd_);}
bool TrackingPublisher::write(const QJsonObject& object,QString* error){
    QSaveFile file(directory_+"/latest.json");file.setDirectWriteFallback(false);
    const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){
        if(error)*error=file.errorString();return false;
    }
    return true;
}
bool TrackingPublisher::publish(QJsonObject tracking,const QString& source,qint64 receivedMs,QString* error){
    const qint64 now=nowMs(),age=qMax<qint64>(0,now-receivedMs);
    tracking["published_monotonic_ms"]=double(now);tracking["source_frame_age_ms"]=double(age);
    tracking["source"]=source;
    if(age>500){tracking["status"]="source_stale";tracking["message"]="Source older than 500 ms; not eligible for fresh localization";tracking["objects"]=QJsonArray();}
    return write(tracking,error);
}
bool TrackingPublisher::invalidate(const QString& status,const QString& reason,QString* error){
    QJsonObject row;row["schema"]=1;row["status"]=status;row["message"]=reason;
    row["published_monotonic_ms"]=double(nowMs());row["source_frame_age_ms"]=QJsonValue();
    row["source_session"]="";row["frame_sequence"]="";row["width"]=1920;row["height"]=1080;
    row["objects"]=QJsonArray();row["tracking_ms"]=0;row["fps"]=0;row["purpose"]="localization_only";
    return write(row,error);
}
