#include "sorting_observer.hpp"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QThread>
#include <csignal>
#include <cstdio>
#include <stdexcept>
#include <ctime>
static volatile sig_atomic_t stopped=0;
static void stopSignal(int){stopped=1;}
static qint64 nowMs(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))throw std::runtime_error("monotonic clock unavailable");return qint64(t.tv_sec)*1000+t.tv_nsec/1000000;}
static QJsonObject failure(const QString &s){return {{"ok",false},{"reason",s}};}
static QJsonObject armStatus(const QString &path){
    // Deliberately no generic command API: the only outbound arm message is status.
    QLocalSocket socket;socket.connectToServer(path);if(!socket.waitForConnected(250))return failure("arm socket unavailable");
    const QByteArray request="{\"action\":\"status\"}\n";
    if(socket.write(request)!=request.size()||!socket.waitForBytesWritten(250))return failure("arm status write timeout");
    QByteArray bytes;QElapsedTimer timer;timer.start();while(!bytes.contains('\n')&&timer.elapsed()<500&&!stopped){socket.waitForReadyRead(50);bytes+=socket.readAll();if(bytes.size()>4096)return failure("arm status exceeds packet bound");}
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())return failure("arm status incomplete");return doc.object();
}
static QJsonObject beltStatus(const QString &device){
    if(device.isEmpty())return failure("no explicit verified belt device; no device opened");
    if(!device.startsWith("/dev/uio")||device.mid(8).isEmpty())return failure("explicit /dev/uioN required");
    for(QChar c:device.mid(8))if(!c.isDigit())return failure("invalid UIO device");
    // Existing CLI returns before MotorDriver construction for --status.
    QProcess process;process.start("/usr/bin/conveyor-jog",{"--uio",device,"--status"});
    if(!process.waitForStarted(250)||!process.waitForFinished(500)){process.kill();process.waitForFinished(250);return failure("read-only belt status process timeout");}
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0)return failure("belt status failed");
    try{auto value=sorting::parseBeltStatus(process.readAllStandardOutput());value["read_monotonic_ms"]=double(nowMs());return value;}catch(const std::exception &e){return failure(e.what());}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("sorting-observer");QCommandLineParser p;p.addHelpOption();
    p.addOption({"config","Read-only configuration file","path","configuration_reference.json"});
    p.addOption({"tracking","Tracking JSON","path","/tmp/triangle-track/latest.json"});
    p.addOption({"v28","V28 JSON","path","/tmp/triangle-live/latest.json"});
    p.addOption({"snapshot-dir","V28 image directory","path","/tmp/triangle-live"});
    p.addOption({"arm-socket","Existing UI read-only status socket","path","/tmp/hdmi-ui-arm.sock"});
    p.addOption({"belt-device","Optional explicitly verified /dev/uioN for status only","path"});
    p.addOption({"watch-ms","Observe for at most 600000ms; default one sample","milliseconds","0"});
    p.addOption({"observe-only","Read-only mode (the only implemented mode)"});
    p.addOption({"single-cycle","Rejected: atomic controller ownership contract is not available"});p.process(app);
    if(p.isSet("single-cycle")){std::fprintf(stderr,"EXECUTION_LOCKED: no atomic controller lease, unmeasured stop/grasp tolerance; no hardware command sent\n");return 3;}
    bool ok=false;const int duration=p.value("watch-ms").toInt(&ok);if(!ok||duration<0||duration>600000)return 2;
    std::signal(SIGTERM,stopSignal);std::signal(SIGINT,stopSignal);
    try{
        const auto config=sorting::readObject(p.value("config"));sorting::Observer observer(config);
        QLockFile lock("/tmp/triangle-sorting-observer.lock");lock.setStaleLockTime(0);if(!lock.tryLock())throw std::runtime_error("another observer owns observer lock (this is not a motor lease)");
        const qint64 begin=nowMs();do{
            QJsonObject tracking,v28;try{tracking=sorting::readObject(p.value("tracking"));}catch(...){}try{v28=sorting::readObject(p.value("v28"));}catch(...){}
            const auto arm=armStatus(p.value("arm-socket")),belt=beltStatus(p.value("belt-device"));
            auto result=observer.sample(nowMs(),tracking,v28,sorting::snapshotBound(v28,p.value("snapshot-dir")),arm,belt);
            result["arm_status"]=arm;result["belt_status"]=belt;const QByteArray line=QJsonDocument(result).toJson(QJsonDocument::Compact);std::fwrite(line.constData(),1,size_t(line.size()),stdout);std::fputc('\n',stdout);std::fflush(stdout);
            if(!duration||stopped)break;
            QThread::msleep(100);
        }while(!stopped&&nowMs()-begin<duration);
    }catch(const std::exception &e){std::fprintf(stderr,"OBSERVER_ERROR %s\n",e.what());return 1;}return 0;
}
