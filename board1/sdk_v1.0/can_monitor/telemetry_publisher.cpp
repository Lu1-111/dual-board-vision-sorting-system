#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QTimer>
#include <QDebug>
#include <cmath>
#include <ctime>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
static QByteArray read(const QString &p) { QFile f(p); if(!f.open(QIODevice::ReadOnly)||f.size()>1024*1024) return {}; return f.readAll(); }
static QJsonObject object(const QString &p) { return QJsonDocument::fromJson(read(p)).object(); }
static bool save(const QString &p,const QByteArray &b) { QSaveFile f(p); return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit(); }
static qint64 monotonicMs() { timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000LL+t.tv_nsec/1000000; }
// Read-only observer: no actuator RPC, GPIO, or runtime control writes.
class Publisher {
    QString root_,out_,ledger_; QJsonObject ledgerData_,seen_; quint32 good_=0,bad_=0;
public:
    Publisher(QString root,QString out):root_(root),out_(out),ledger_(out+".ledger") {
        if(QFile::exists(ledger_)) {
            ledgerData_=object(ledger_);
            if(ledgerData_.value("schema").toInt()!=1) qFatal("Invalid telemetry ledger; refusing to reset counts");
            good_=quint32(ledgerData_.value("good").toDouble()); bad_=quint32(ledgerData_.value("bad").toDouble()); seen_=ledgerData_.value("seen").toObject();
        }
    }
    bool tick() {
        for(const QString &name:QDir(root_).entryList({"completed-*.json"},QDir::Files)) {
            const auto completed=read(root_+"/"+name);
            const auto done=QJsonDocument::fromJson(completed).object();
            QString cycleName=name; cycleName.replace("completed-","cycle-");
            const auto decision=read(root_+"/"+cycleName);
            const auto decisionObject=QJsonDocument::fromJson(decision).object();
            const QString label=decisionObject.value("decision").toString();
            // A restarted runtime reuses cycle filenames. Never join a new
            // decision to the previous cycle's still-present completion.
            if(!decisionObject.contains("before_job") || done.value("actionJobId").toInt(-1)!=decisionObject.value("before_job").toInt(-2)+1 || QFileInfo(root_+"/"+name).lastModified()<QFileInfo(root_+"/"+cycleName).lastModified()) continue;
            if(done.value("actionJobState")!="completed" || done.value("moving").toBool(true) || done.value("actionPlaying").toBool(true) || (label!="NORMAL"&&label!="DEFECT")) continue;
            const QString key=QString::fromLatin1(QCryptographicHash::hash(completed+decision,QCryptographicHash::Sha256).toHex());
            if(seen_.value(name)==key) continue;
            if(label=="NORMAL") { if(good_==0xffffffffu)return false; ++good_; } else { if(bad_==0xffffffffu)return false; ++bad_; }
            seen_[name]=key;
        }
        for(const auto &name:seen_.keys()) if(!QFile::exists(root_+"/"+name)) seen_.remove(name);
        ledgerData_={{"schema",1},{"good",double(good_)},{"bad",double(bad_)},{"seen",seen_}};
        if(!save(ledger_,QJsonDocument(ledgerData_).toJson(QJsonDocument::Compact)))return false;
        auto state=object(root_+"/state.json"); const QString phase=state.value("state").toString();
        const QString pid=QString::fromLatin1(read("/run/autosort.pid")).trimmed();
        const bool alive=!pid.isEmpty() && QFile::symLinkTarget("/proc/"+pid+"/exe")=="/opt/autosort/autosort" && !read("/proc/"+pid+"/stat").contains(") Z ");
        unsigned run=0,arm=0,vision=0,color=0,confidence=0;
        if(phase.startsWith("FAULT")) run=3;
        else if(alive) { if(phase=="PAUSED"||phase=="PAUSING")run=2; else if(!phase.isEmpty())run=1; }
        else if(!phase.isEmpty()&&phase!="PAUSED")run=3;
        if(run==3)arm=2; else if(alive&&(phase=="SORTING"||phase=="RETURNING"||phase=="PREPARING"))arm=1;
        const auto v=object("/tmp/triangle-live/latest.json");
        if(!v.isEmpty()) {
            const double age=monotonicMs()-v.value("published_monotonic_ms").toDouble(-1)+v.value("source_frame_age_ms").toDouble(1e9);
            vision=2;
            if(v.value("status")=="ok" && age>=0 && age<=5000 && !v.value("historical").toBool()) {
                vision=1; auto objects=v.value("objects").toArray();
                if(objects.size()==1) {
                    auto item=objects.first().toObject(); auto label=item.value("label").toString(); const double score=item.value("score").toDouble(-1);
                    if(std::isfinite(score)&&score>=0&&score<=1 && ((label=="NORMAL"&&score<.88)||(label=="DEFECT"&&score>=.88))) { color=label=="NORMAL"?4:5; confidence=unsigned(std::round((color==4?1-score:score)*100)); }
                }
            }
        }
        const auto uptime=read("/proc/uptime").split(' ').first().toDouble();
        QByteArray line=QString("%1 %2 %3 %4 %5 0 0 0 %6 %7 0 %8\n").arg(run).arg(arm).arg(vision).arg(color).arg(confidence).arg(good_).arg(bad_).arg(quint32(uptime)).toLatin1();
        return save(out_,line);
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QString root="/tmp/autosort",out="/run/line-state.txt";
    if(argc==3){root=QString::fromLocal8Bit(argv[1]);out=QString::fromLocal8Bit(argv[2]);}
    else if(argc!=1)return 2;
    int lock=open((out+".lock").toUtf8().constData(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB)!=0)return 2;
    Publisher publisher(root,out);
    auto tick=[&](){if(!publisher.tick()){qCritical()<<"Telemetry write failed; stopping";app.exit(1);return false;}return true;};
    if(!tick())return 1;
    QTimer timer; QObject::connect(&timer,&QTimer::timeout,tick);timer.start(500);
    return app.exec();
}
