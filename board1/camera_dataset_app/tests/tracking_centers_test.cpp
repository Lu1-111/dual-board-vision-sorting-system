#include "tracking_centers.hpp"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>
#include <time.h>

void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
QByteArray pixels(const QImage& image){return QByteArray(reinterpret_cast<const char*>(image.constBits()),image.bytesPerLine()*image.height());}
QImage scene(int mode){
    QImage image(1920,1080,QImage::Format_RGB32);image.fill(qRgb(40,40,40));
    QPainter painter(&image);
    if(mode==0)painter.fillRect(QRect(900,420,240,240),QColor(0,0,255));
    if(mode==1)painter.fillRect(QRect(0,420,240,240),QColor(0,0,255));
    if(mode==2){painter.fillRect(QRect(720,420,180,240),QColor(0,0,255));painter.fillRect(QRect(960,420,180,240),QColor(0,0,255));}
    if(mode==3)painter.fillRect(QRect(720,420,420,240),QColor(0,0,255));
    painter.end();return image;
}
int main(int argc,char**argv){
    qputenv("QT_QPA_PLATFORM","offscreen");QGuiApplication app(argc,argv);
    try {
        TrackingCenters tracker;const auto image=scene(0);const auto before=pixels(image);
        const auto first=tracker.update(image,"a",1);const auto objects=first.value("objects").toArray();
        require(objects.size()==1,"single object expected");const auto obj=objects[0].toObject();
        const auto center=obj.value("center_xy").toArray();require(center[0].toDouble()==1020&&center[1].toDouble()==540,"center must use original coordinates");
        const auto small=image.scaled(640,360);const auto smallBefore=pixels(small);
        const auto drawn=TrackingCenters::overlay(small,first);
        require(pixels(image)==before,"full image mutated");require(pixels(small)==smallBefore,"shared preview source mutated");
        require(pixels(drawn)!=smallBefore,"overlay did not draw");
        QTemporaryDir referenceDirectory;require(referenceDirectory.isValid(),"reference temporary directory failed");
        const QString referencePath=referenceDirectory.path()+"/reference.json";
        QJsonObject reference{{"schema",1},{"width",1920},{"height",1080},
                              {"center_xy",QJsonArray{1235,543}},{"region_xyxy",QJsonArray{1135,531,1335,555}},
                              {"calibrated",false},{"motion_enabled",false},{"audit_note","ignored metadata"}};
        const auto writeReference=[&](const QByteArray& bytes){QFile file(referencePath);require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"reference write failed");require(file.write(bytes)==bytes.size(),"reference short write");};
        writeReference(QJsonDocument(reference).toJson(QJsonDocument::Compact));
        const auto validated=TrackingCenters::loadReference(referencePath);
        require(!validated.isEmpty()&&!validated.contains("audit_note"),"reference critical fields rejected or audit fields propagated");
        const auto referenceDrawn=TrackingCenters::overlay(small,first,validated);
        require(pixels(referenceDrawn)!=pixels(drawn),"reference overlay missing");
        require(pixels(image)==before&&pixels(small)==smallBefore,"reference changed source pixels");
        require(validated.value("center_xy").toArray()==QJsonArray({1235,543}),"reference center followed detected center");
        for(const QString key:{QString("calibrated"),QString("motion_enabled")}){
            auto invalid=reference;invalid[key]=true;writeReference(QJsonDocument(invalid).toJson());
            require(TrackingCenters::loadReference(referencePath).isEmpty(),"permission-like reference was accepted");
        }
        auto wrongSize=reference;wrongSize["width"]=640;writeReference(QJsonDocument(wrongSize).toJson());
        require(TrackingCenters::loadReference(referencePath).isEmpty(),"wrong reference dimensions accepted");
        auto wrongCenter=reference;wrongCenter["center_xy"]=QJsonArray{1500,543};writeReference(QJsonDocument(wrongCenter).toJson());
        require(TrackingCenters::loadReference(referencePath).isEmpty(),"center outside reference region accepted");
        auto wrongSchema=reference;wrongSchema["schema"]=2;writeReference(QJsonDocument(wrongSchema).toJson());
        require(TrackingCenters::loadReference(referencePath).isEmpty(),"unknown reference schema accepted");
        auto missingFlag=reference;missingFlag.remove("motion_enabled");writeReference(QJsonDocument(missingFlag).toJson());
        require(TrackingCenters::loadReference(referencePath).isEmpty(),"missing permission flag accepted");
        writeReference("{broken");require(TrackingCenters::loadReference(referencePath).isEmpty(),"bad JSON accepted");
        require(pixels(TrackingCenters::overlay(small,first,TrackingCenters::loadReference(referencePath)))==pixels(drawn),"bad reference changed overlay");
        writeReference(QByteArray(4097,'x'));require(TrackingCenters::loadReference(referencePath).isEmpty(),"oversized reference accepted");
        QFile::remove(referencePath);require(TrackingCenters::loadReference(referencePath).isEmpty(),"missing reference accepted");
        const auto next=tracker.update(image,"a",2);require(next.value("objects").toArray()[0].toObject().value("track_id")==obj.value("track_id"),"stationary ID changed");
        const auto fresh=tracker.update(image,"b",1);require(fresh.value("objects").toArray()[0].toObject().value("track_id")!=obj.value("track_id"),"session reused previous track ID");
        const auto partial=tracker.update(scene(1),"c",1);require(partial.value("objects").toArray()[0].toObject().value("partial").toBool(),"border partial missing");
        TrackingCenters merger;require(merger.update(scene(2),"d",1).value("objects").toArray().size()==2,"pair expected");
        const auto merged=merger.update(scene(3),"d",2).value("objects").toArray();
        require(merged.size()==1&&merged[0].toObject().value("ambiguous").toBool(),"merged identity must be ambiguous");
        QTemporaryDir directory;require(directory.isValid(),"temporary directory failed");
        TrackingPublisher publisher(directory.path());QString error;
        timespec ts{};clock_gettime(CLOCK_MONOTONIC,&ts);qint64 now=qint64(ts.tv_sec)*1000+ts.tv_nsec/1000000;
        require(publisher.publish(first,"simulation",now,&error),"publish failed");
        QFile file(directory.path()+"/latest.json");require(file.open(QIODevice::ReadOnly),"JSON read failed");
        auto saved=QJsonDocument::fromJson(file.readAll()).object();file.close();require(saved.value("frame_sequence").toString()=="1"&&saved.value("status").toString()=="ok","published frame mismatch");
        require(publisher.publish(first,"simulation",now-600,&error),"stale publish failed");
        file.open(QIODevice::ReadOnly);saved=QJsonDocument::fromJson(file.readAll()).object();file.close();
        require(saved.value("status").toString()=="source_stale"&&saved.value("objects").toArray().isEmpty(),"stale positions remained valid");
        require(publisher.invalidate("unavailable","session stopped",&error),"invalidate failed");
        file.open(QIODevice::ReadOnly);saved=QJsonDocument::fromJson(file.readAll()).object();
        require(saved.value("source_session").toString().isEmpty()&&saved.value("objects").toArray().isEmpty(),"stop did not invalidate session");
        std::puts("TRACKING_CENTERS_PASS original_pixel_center=1020,540 partial ambiguous source_immutable session_reset stale_invalidation reference_fixed_readonly strict_invalid_reference");return 0;
    } catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
}
