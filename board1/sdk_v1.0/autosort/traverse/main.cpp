#include "vision.hpp"
#include "../parking_region.hpp"
#include "drive.hpp"
#include "../sorting_runtime/sorting_observer.hpp"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QThread>
#include <QSettings>
#include <csignal>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
static volatile sig_atomic_t interrupted=0;
static void signalStop(int){interrupted=1;}
static qint64 nowMs(){timespec t{};trial::require(!clock_gettime(CLOCK_MONOTONIC,&t),"clock unavailable");return qint64(t.tv_sec)*1000+t.tv_nsec/1000000;}
static void print(const QJsonObject&o){const auto s=QJsonDocument(o).toJson(QJsonDocument::Compact);std::fwrite(s.constData(),1,size_t(s.size()),stdout);std::fputc('\n',stdout);std::fflush(stdout);}
static int armProbe(){
    QJsonArray samples;QVector<qint64> times;int failures=0,over100=0;
    for(int i=0;i<20;++i){QLocalSocket socket;QElapsedTimer timer;timer.start();socket.connectToServer("/tmp/hdmi-ui-arm.sock");QByteArray data;bool connected=socket.waitForConnected(1000);
        if(connected){socket.write("{\"action\":\"status\"}\n");while(timer.elapsed()<1000&&!data.contains('\n')){socket.waitForReadyRead(qMin(50,int(1000-timer.elapsed())));data+=socket.readAll();if(data.size()>4096||socket.state()==QLocalSocket::UnconnectedState)break;}}
        const qint64 elapsed=timer.elapsed();QJsonParseError error;const auto doc=QJsonDocument::fromJson(data,&error);const bool ok=connected&&data.size()<=4096&&error.error==QJsonParseError::NoError&&doc.isObject()&&doc.object().value("ok").toBool()&&elapsed<=1000;
        if(!ok)++failures;
        if(elapsed>100)++over100;
        times<<elapsed;samples.append(QJsonObject{{"sample",i+1},{"ok",ok},{"roundtrip_ms",double(elapsed)},{"above100ms",elapsed>100}});socket.abort();QThread::msleep(20);
    }
    std::sort(times.begin(),times.end());print({{"schema",1},{"status","arm_latency_readonly"},{"clock_source","CLOCK_MONOTONIC"},{"requests",20},{"failures",failures},{"above100ms",over100},{"median_ms",(times[9]+times[10])/2.0},{"p95_ms",double(times[18])},{"max_ms",double(times[19])},{"samples",samples},{"motor_device_opened",false}});return failures?1:0;
}
namespace ArmTiming {
constexpr qint64 StateAgeMs=1000;
constexpr qint64 RequestMs=500;
constexpr qint64 InitialWaitMs=500;
constexpr qint64 PollIntervalMs=40;
}
class ArmMonitor:public QObject {
    QLocalSocket socket_;QByteArray bytes_;QString failure_;bool pending_=false;
    qint64 requestAt_=-1,goodAt_=-1;QJsonObject current_,reference_;int expected_;bool enforce_;
    QString serverName_;qint64 (*clock_)();
    QString requiredGeneration_,requiredSession_;
    qint64 requests_=0,accepted_=0,lastReplyElapsed_=-1,maxReplyElapsed_=0;
public:
    ArmMonitor(int job,bool enforce,const QString &serverName=QStringLiteral("/tmp/hdmi-ui-arm.sock"),qint64 (*clock)()=nowMs,const QString &generation=QString(),const QString &session=QString()):expected_(job),enforce_(enforce),serverName_(serverName),clock_(clock),requiredGeneration_(generation),requiredSession_(session){
        connect(&socket_,&QLocalSocket::connected,this,[this](){socket_.write("{\"action\":\"status\"}\n");});
        connect(&socket_,&QLocalSocket::readyRead,this,[this](){bytes_+=socket_.readAll();if(bytes_.size()>4096){failure_="arm status packet exceeds4096";return;}if(!bytes_.contains('\n'))return;
            // Reject late replies before they can clear pending_ or refresh goodAt_.
            const qint64 receivedAt=clock_();
            lastReplyElapsed_=receivedAt-requestAt_;maxReplyElapsed_=qMax(maxReplyElapsed_,lastReplyElapsed_);
            if(!pending_||receivedAt<requestAt_||receivedAt-requestAt_>ArmTiming::RequestMs){failure_=QString("arm status reply exceeded%1ms").arg(ArmTiming::RequestMs);socket_.abort();return;}
            QJsonParseError e;const auto d=QJsonDocument::fromJson(bytes_,&e);if(e.error!=QJsonParseError::NoError||!d.isObject()){failure_="arm JSON invalid";return;}
            try{if(enforce_){if(!requiredGeneration_.isEmpty())trial::require(d.object().value("commandGeneration").toString()==requiredGeneration_&&d.object().value("consoleSessionId").toString()==requiredSession_,"automation ownership changed");trial::checkArm(d.object(),expected_);const auto ref=d.object().value("visionReference").toObject();if(!reference_.isEmpty())trial::require(ref==reference_,"arm reference/mapping/origin changed");reference_=ref;}current_=d.object();goodAt_=requestAt_;++accepted_;pending_=false;socket_.abort();}catch(const std::exception&error){failure_=error.what();}
        });
    }
    QJsonObject diagnostics() const {const qint64 now=clock_();return {{"requests",double(requests_)},{"accepted_replies",double(accepted_)},{"pending",pending_},{"last_reply_elapsed_ms",double(lastReplyElapsed_)},{"max_reply_elapsed_ms",double(maxReplyElapsed_)},{"current_request_age_ms",double(requestAt_<0?-1:now-requestAt_)},{"accepted_evidence_age_ms",double(goodAt_<0?-1:now-goodAt_)},{"request_timeout_ms",double(ArmTiming::RequestMs)},{"state_max_age_ms",double(ArmTiming::StateAgeMs)},{"initial_wait_ms",double(ArmTiming::InitialWaitMs)},{"request_interval_ms",double(ArmTiming::PollIntervalMs)}};}
    void poll(qint64 /* callerNow */){
        QCoreApplication::processEvents(QEventLoop::AllEvents,1);
        const qint64 now=clock_(); // Event handling may consume time; never test an old caller timestamp.
        trial::require(failure_.isEmpty(),failure_.toUtf8().constData());
        if(pending_)trial::require(now-requestAt_<=ArmTiming::RequestMs,QString("arm status request exceeded%1ms").arg(ArmTiming::RequestMs).toUtf8().constData());
        if(!pending_&&(requestAt_<0||now-requestAt_>=ArmTiming::PollIntervalMs)){bytes_.clear();pending_=true;requestAt_=now;++requests_;socket_.abort();socket_.connectToServer(serverName_);}
    }
    bool ready(qint64 now)const{return goodAt_>=0&&now>=goodAt_&&now-goodAt_<=ArmTiming::StateAgeMs;}
    qint64 stamp()const{return goodAt_;}
    QJsonObject value()const{return current_;}
};
static trial::Frame readTracking(){QJsonObject object;try{object=sorting::readObject("/tmp/triangle-track/latest.json");}catch(const std::runtime_error&e){throw trial::VisionUnavailable(e.what());}return trial::readFrame(object,nowMs(),trial::defaultPolygon());}
struct Stable {
    qint64 begin=-1;int count=0;QPointF anchor;
    bool update(const trial::Frame&f,bool fresh,qint64 after,int duration){if(fresh&&f.captured>after){if(begin<0||std::hypot(f.center.x()-anchor.x(),f.center.y()-anchor.y())>12){begin=f.captured;count=0;anchor=f.center;}++count;}return begin>=0&&count>=3&&f.captured-begin>=duration;}
};
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);QCommandLineParser p;p.addHelpOption();p.addOption({"execute","Explicit single continuous commissioning attempt"});p.addOption({"expected-arm-job","Explicit expected idle/completed arm job","N","10"});p.addOption({"status-readonly","Read profile+arm status; no initialize or motor writes"});p.addOption({"clock-readonly","CLOCK_MONOTONIC only, no socket or device"});p.addOption({"arm-probe-readonly","20 direct socket status timings, no UIO opened"});p.addOption({"expected-generation","Automation owner generation","G"});p.addOption({"expected-ui-session","Automation UI session","ID"});p.process(app);
    if(p.isSet("clock-readonly")){print({{"schema",1},{"clock_source","CLOCK_MONOTONIC"},{"read_monotonic_ms",double(nowMs())}});return 0;}
    if(p.isSet("arm-probe-readonly"))return armProbe();
    bool valid=false;const qint64 expected=p.value("expected-arm-job").toLongLong(&valid);if(!valid||expected<0||expected>2147483647||(p.isSet("execute")&&p.isSet("status-readonly")))return 2;
    std::signal(SIGTERM,signalStop);std::signal(SIGINT,signalStop);int lock=-1;std::unique_ptr<conveyor::Registers> registers;std::unique_ptr<traverse::Drive> drive;int exitCode=1;std::unique_ptr<ArmMonitor> armMonitor;
    try{
        registers=conveyor::openUio("/dev/uio0");const auto initial=traverse::readProfile(*registers); // Reads only, rejects Features6 before writes.
        const bool execute=p.isSet("execute"),statusOnly=p.isSet("status-readonly");armMonitor.reset(new ArmMonitor(int(expected),!statusOnly,QStringLiteral("/tmp/hdmi-ui-arm.sock"),nowMs,p.value("expected-generation"),p.value("expected-ui-session")));ArmMonitor &arm=*armMonitor;qint64 waitBegin=nowMs();
        do{trial::require(!interrupted,"signal before start");arm.poll(nowMs());if(arm.ready(nowMs()))break;trial::require(nowMs()-waitBegin<=ArmTiming::InitialWaitMs,QString("initial arm status exceeded%1ms").arg(ArmTiming::InitialWaitMs).toUtf8().constData());QThread::msleep(1);}while(true);
        if(statusOnly){const auto snapshot=traverse::readProfile(*registers);const qint64 readAt=nowMs();print({{"schema",1},{"status","readonly_status"},{"clock_source","CLOCK_MONOTONIC"},{"read_monotonic_ms",double(readAt)},{"arm_read_monotonic_ms",double(arm.stamp())},{"arm_status",arm.value()},{"firmware_features",snapshot.value("features")},{"hardware_max_continuous_ms",8000},{"stop_readback",snapshot}});return 0;}
        trial::require(initial.value("status").toDouble()==0&&initial.value("active_duty").toDouble()==0&&initial.value("encoder_errors").toDouble()==0,"initial conveyor not disabled/fault-free");
        trial::Guard guard;auto frame=readTracking();guard.accept(frame);
        if(!execute){print({{"schema",1},{"status","observe_only"},{"motion_sent",false},{"expected_arm_job",double(expected)},{"center_xy",QJsonArray{frame.center.x(),frame.center.y()}},{"firmware_features",initial.value("features")},{"hardware_max_continuous_ms",8000},{"stop_readback",initial}});return 0;}
        lock=::open("/tmp/integration-trial-jog.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);trial::require(lock>=0&&flock(lock,LOCK_EX|LOCK_NB)==0,"another commissioning owner holds trial lock");
        QSettings encoderConfig("/opt/autosort/encoder.ini",QSettings::IniFormat);
        traverse::EncoderTuning tuning;
        tuning.cmPerCount=encoderConfig.value("cm_per_count",tuning.cmPerCount).toDouble();
        tuning.fullSpeed=encoderConfig.value("full_speed_cm_s",tuning.fullSpeed).toDouble();
        tuning.kp=encoderConfig.value("speed_kp",tuning.kp).toDouble();
        tuning.ki=encoderConfig.value("speed_ki",tuning.ki).toDouble();
        tuning.filterMs=encoderConfig.value("speed_filter_ms",tuning.filterMs).toDouble();
        drive.reset(new traverse::Drive(*registers,tuning));drive->initialize(nowMs());const qint64 begin=nowMs();qint64 stoppedAt=begin;Stable initialStable,finalStable;traverse::SpeedSchedule speed;bool started=false,finalPhase=frame.center.x()>=1213,done=false;QJsonObject finalReadback;
        while(!done){
            trial::require(!interrupted,"interrupted by signal");if(nowMs()-begin>20000){if(!started||finalPhase)throw trial::VisionUnavailable("target did not become stable within20s");throw std::runtime_error("single continuous attempt wall deadline20s");}
            arm.poll(nowMs());trial::require(arm.ready(nowMs()),QString("arm status older than%1ms").arg(ArmTiming::StateAgeMs).toUtf8().constData());frame=readTracking();const bool fresh=guard.accept(frame);const qint64 now=nowMs();trial::require(arm.ready(now),QString("arm status aged past%1ms while reading tracking").arg(ArmTiming::StateAgeMs).toUtf8().constData());
            if(!finalPhase&&!started){if(frame.center.x()>=1213){finalPhase=true;stoppedAt=now;finalReadback=drive->stop(now);}else if(initialStable.update(frame,fresh,begin,500)){started=true;}}
            if(started&&!finalPhase){
                if(frame.center.x()>=1213){finalReadback=drive->stop(now);stoppedAt=now;finalPhase=true;}
                else drive->tick(now,speed.update(frame.center.x()),true);
            }
            if(finalPhase){
                finalReadback=drive->stop(now);trial::require(traverse::successfulStop(finalReadback),"fault/encoder error latched after stop");
                if(finalStable.update(frame,fresh,stoppedAt,1000)){trial::visionRequire(autosort::inParkingRegion(frame.center),"final stopped center outside permitted x/y region");done=true;}
            }
            if(!done)QThread::msleep(5);
        }
        arm.poll(nowMs());trial::require(arm.ready(nowMs()),QString("final arm status older than%1ms").arg(ArmTiming::StateAgeMs).toUtf8().constData());const auto last=readTracking();guard.accept(last);trial::visionRequire(autosort::inParkingRegion(last.center)&&std::hypot(last.center.x()-frame.center.x(),last.center.y()-frame.center.y())<=12,"final target changed");
        finalReadback=drive->stop(nowMs());trial::require(traverse::successfulStop(finalReadback),"final fault/encoder status");const qint64 fence=nowMs();
        print({{"schema",1},{"status","success"},{"mode","continuous_commissioning"},{"clock_source","CLOCK_MONOTONIC"},{"stop_verified",true},{"capture_fence_monotonic_ms",double(fence)},{"source_session",last.session},{"frame_sequence",QString::number(last.seq)},{"track_id",last.id},{"center_xy",QJsonArray{last.center.x(),last.center.y()}},{"expected_arm_job",double(expected)},{"firmware_features",finalReadback.value("features")},{"hardware_max_continuous_ms",8000},{"software_max_active_ms",7500},{"stop_readback",finalReadback},{"position_error_px",QJsonArray{last.center.x()-1235,last.center.y()-543}},{"stop_trigger_x",1213},{"drive_monitor",drive->diagnostics()},{"arm_motion_sent",false},{"arm_monitor",arm.diagnostics()}});exitCode=0;
    }catch(const std::exception&e){QJsonObject readback;QString stopError;bool verified=false;const bool visual=dynamic_cast<const trial::VisionUnavailable*>(&e)!=nullptr;try{if(drive)readback=drive->stop(nowMs());else if(registers)readback=traverse::readProfile(*registers);verified=traverse::successfulStop(readback);}catch(const std::exception&s){stopError=s.what();}print({{"schema",1},{"status","stopped_failure"},{"failure_class",visual?"vision":"control"},{"recoverable",visual&&verified},{"reason",e.what()},{"stop_verified",verified},{"stop_error",stopError},{"stop_readback",readback},{"arm_motion_sent",false},{"drive_monitor",drive?drive->diagnostics():QJsonObject()},{"arm_monitor",armMonitor?armMonitor->diagnostics():QJsonObject()}});}
    drive.reset();registers.reset();if(lock>=0)::close(lock);return exitCode;
}
