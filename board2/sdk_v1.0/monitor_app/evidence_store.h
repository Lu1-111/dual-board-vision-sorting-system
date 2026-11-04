#pragma once
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <time.h>

// Both UI and relay use atomic files. No caller-supplied filesystem paths.
namespace Evidence {
constexpr int MaxImage = 512 * 1024;
inline QString root() { return qEnvironmentVariable("MONITOR_EVIDENCE_DIR", "/var/lib/monitor-ui/evidence"); }
inline QString path(const QString &name) { return root()+"/"+name; }
inline QJsonObject read(const QString &name) {
    QFile f(path(name)); if(!f.open(QIODevice::ReadOnly) || f.size()>1024*1024) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
inline bool bytes(const QString &name,const QByteArray &data) {
    if(!QDir().mkpath(root())) return false;
    QSaveFile f(path(name)); return f.open(QIODevice::WriteOnly) && f.write(data)==data.size() && f.commit();
}
inline bool write(const QString &name,const QJsonObject &o) { return bytes(name,QJsonDocument(o).toJson(QJsonDocument::Compact)); }
inline QString key(const QJsonObject &o) {
    double s=o["session_id"].toDouble(), e=o["event_id"].toDouble();
    if(s<1 || s>4294967295.0 || e<1 || e>4294967295.0 || s!=quint32(s) || e!=quint32(e)) return {};
    return QString::number(quint32(s))+"_"+QString::number(quint32(e));
}
inline bool uuid(const QString &s) { return QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$").match(s).hasMatch(); }
inline QString sha(const QByteArray &b) { return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex()); }
inline QJsonArray events() { return read("index.json")["events"].toArray(); }
inline QJsonObject event(const QString &k) {
    for(auto v:events()) if(key(v.toObject())==k) return v.toObject();
    return {};
}
inline QString imageError(const QByteArray &b) {
    if(b.isEmpty() || b.size()>MaxImage) return "image_size_limit";
    QBuffer buf; buf.setData(b); buf.open(QIODevice::ReadOnly); QImageReader r(&buf);
    if(r.format()!="jpeg") return "jpeg_required";
    QSize s=r.size();
    if(s.width()<1 || s.height()<1 || s.width()>4096 || s.height()>4096 || qint64(s.width())*s.height()>8000000) return "image_dimensions_limit";
    if(r.read().isNull()) return "invalid_jpeg";
    return {};
}
inline QJsonObject error(const QString &s) { return {{"ok",false},{"error",s}}; }
inline QJsonObject accept(const QJsonObject &o) {
    QString k=key(o); QJsonObject ev=event(k);
    if(k.isEmpty() || ev.isEmpty()) return error("unknown_event_retry_after_CAN");
    if(o["is_anomaly"]!=QJsonValue(true) || o["alarm_type"]!=ev["alarm_type"]) return error("event_type_mismatch_or_normal_frame");
    QString source=o["source"].toString();
    if(source!="camera" && source!="test") return error("source_must_be_camera_or_test");
    if(ev["simulated"].toBool() && source!="test") return error("simulation_requires_test_image_label");
    QByteArray b=QByteArray::fromBase64(o["image_b64"].toString().toLatin1());
    QString digest=sha(b), err=imageError(b);
    if(!err.isEmpty()) return error(err);
    if(digest!=o["sha256"].toString()) return error("sha256_mismatch");
    QJsonObject previous=read(k+".image.json");
    if(!previous.isEmpty()) {
        if(previous["sha256"]!=digest) return error("event_image_is_immutable");
        return {{"ok",true},{"sha256",digest},{"duplicate",true}};
    }
    // Only the network service writes evidence. Metadata is the commit marker.
    QJsonObject meta=o; meta.remove("image_b64"); meta.remove("token"); meta.remove("rpc_id"); meta.remove("op"); meta["bytes"]=b.size(); meta["mime"]="image/jpeg";
    if(!bytes(k+".jpg",b) || !write(k+".image.json",meta)) return error("storage_write_failed");
    return {{"ok",true},{"sha256",digest},{"bytes",b.size()}};
}
inline bool expired(const QJsonObject &j) { return QDateTime::currentSecsSinceEpoch()>qint64(j["deadline"].toDouble()); }
inline QJsonObject job(const QString &id) { return uuid(id)?read(id+".job.json"):QJsonObject(); }
inline QJsonObject rpc(const QJsonObject &o,bool network) {
    QString op=o["op"].toString();
    if(op=="hello") return {{"ok",true},{"protocol",1},{"max_image_bytes",MaxImage}};
    if(op=="events") return {{"ok",true},{"events",events()}};
    if(network) return op=="put_image"?accept(o):error("network_operation_not_allowed");
    if(op=="sync_time") {
        double value=o["epoch_ms"].toDouble();
        if(value<1735689600000.0 || value>4102444800000.0 || value!=qint64(value)) return error("invalid_time");
        qint64 ms=qint64(value); struct timespec ts;
        ts.tv_sec=time_t(ms/1000); ts.tv_nsec=long(ms%1000)*1000000L;
        if(clock_settime(CLOCK_REALTIME,&ts)!=0) return error("clock_update_failed");
        return {{"ok",true},{"epoch_ms",double(QDateTime::currentMSecsSinceEpoch())}};
    }
    if(op=="poll") {
        write("pc-status.json",{{"last_seen",double(QDateTime::currentSecsSinceEpoch())},{"provider",o["provider"]}});
        QJsonArray jobs;
        for(QString f:QDir(root()).entryList({"*.job.json"},QDir::Files,QDir::Time|QDir::Reversed)) {
            QString id=f.left(f.size()-9); QJsonObject j=read(f);
            if(!expired(j) && !event(key(j)).isEmpty() && read(id+".result.json").isEmpty()) { jobs.append(j); if(jobs.size()>=5) break; }
        }
        return {{"ok",true},{"jobs",jobs}};
    }
    QString id=o["request_id"].toString(); QJsonObject j=job(id);
    if(j.isEmpty()) return error("unknown_request");
    if(expired(j)) return error("request_expired");
    if(event(key(j)).isEmpty()) return error("event_no_longer_available");
    if(op=="image_chunk") {
        int offset=o["offset"].toInt(-1); if(offset<0 || offset>MaxImage) return error("invalid_offset");
        QFile f(path(key(j)+".jpg")); if(!f.open(QIODevice::ReadOnly) || !f.seek(offset)) return error("image_missing");
        return {{"ok",true},{"offset",offset},{"data",QString::fromLatin1(f.read(1536).toBase64())},{"size",double(f.size())},{"sha256",j["image_sha256"]}};
    }
    if(op=="complete") {
        if(o["image_sha256"]!=j["image_sha256"] || key(o)!=key(j)) return error("result_identity_mismatch");
        QString status=o["status"].toString(), analysis=o["analysis"].toString();
        if((status!="ok" && status!="mock" && status!="error") || analysis.size()>12000 || analysis.isEmpty()) return error("invalid_result");
        QJsonObject r={{"request_id",id},{"session_id",j["session_id"]},{"event_id",j["event_id"]},{"image_sha256",j["image_sha256"]},{"status",status},{"analysis",analysis},{"provider",o["provider"].toString().left(80)},{"model",o["model"].toString().left(120)}};
        if(!read(id+".result.json").isEmpty()) return {{"ok",true},{"duplicate",true}};
        return write(id+".result.json",r)?QJsonObject{{"ok",true}}:error("storage_write_failed");
    }
    return error("unknown_operation");
}
inline QString enqueue(const QJsonObject &ev) {
    QString k=key(ev); QJsonObject meta=read(k+".image.json"); if(k.isEmpty() || meta.isEmpty()) return {};
    // Keep at most 100 completed/expired jobs; never silently discard active jobs.
    QStringList files=QDir(root()).entryList({"*.job.json"},QDir::Files,QDir::Time);
    for(int i=80;i<files.size();++i) {
        QString id=files[i].left(files[i].size()-9);
        if(expired(read(files[i])) || !read(id+".result.json").isEmpty()) { QFile::remove(path(files[i])); QFile::remove(path(id+".result.json")); }
    }
    if(QDir(root()).entryList({"*.job.json"},QDir::Files).size()>=100) return {};
    QString id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject j=ev; j["request_id"]=id; j["image_sha256"]=meta["sha256"]; j["image_bytes"]=meta["bytes"];
    j["image_source"]=meta["source"]; j["deadline"]=double(QDateTime::currentSecsSinceEpoch()+300);
    j["prompt"]=QStringLiteral("请结合异常事件信息和图片，用中文给出观察到的事实、可能原因、还需核实的信息及排查建议。区分事实与推测；单张图片无法确认的运动状态不要断言；测试图片不代表真实故障。只给工作人员建议，不发出控制设备命令。");
    return write(id+".job.json",j)?id:QString();
}
}
