#include "gates.hpp"
#include "traverse/drive.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QProcess>
#include <QLocalSocket>
#include <QThread>
#include <QDateTime>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/socket.h>
using namespace autosort;
static volatile sig_atomic_t stopped=0;
static void signalStop(int){stopped=1;}
static qint64 now(){timespec t{};require(clock_gettime(CLOCK_MONOTONIC,&t)==0,"clock failed");return t.tv_sec*1000LL+t.tv_nsec/1000000;}
static QByteArray bytes(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly)&&f.size()<=8*1024*1024,"file unavailable");return f.readAll();}
static QString hash(const QString &path){return QString::fromLatin1(QCryptographicHash::hash(bytes(path),QCryptographicHash::Sha256).toHex());}
static void write(const QString &path,const QJsonObject &o){QSaveFile f(path);require(f.open(QIODevice::WriteOnly),"cannot write state");const auto b=QJsonDocument(o).toJson(QJsonDocument::Compact);require(f.write(b)==b.size()&&f.commit(),"state write failed");}
class Child:public QProcess {void setupChildProcess() override {const auto parent=getppid();prctl(PR_SET_PDEATHSIG,SIGTERM);if(getppid()!=parent||parent==1)_exit(125);}};
struct PauseRequested {};
class RpcTimeout:public std::runtime_error {public:using std::runtime_error::runtime_error;};
class Runtime {
    QJsonObject cfg_,arm_,reference_,arrival_,lastClass_;QString root_,sock_,track_,vision_,imageDir_,worker_,generation_,selected_,control_;int job_=0;bool execute_,armed_=false;int cycle_=0;
    trial::Guard trackGuard_;Child child_;QString phase_="STARTING",pendingRequest_;bool actionSubmitted_=false;qint64 waitCaptureAfter_=-1;
    QVector<trial::Frame> feedHistory_;trial::Guard feedGuard_;trial::Frame decisionTrack_;QJsonObject feedFence_;bool transitDecision_=false,postStopDecision_=false;QString lastRejectedSequence_;
public:
    Runtime(QJsonObject c,bool execute):cfg_(c),execute_(execute){
        root_="/tmp/autosort";sock_="/tmp/hdmi-ui-arm.sock";track_="/tmp/triangle-track/latest.json";vision_="/tmp/triangle-live/latest.json";imageDir_="/tmp/triangle-live";worker_="/opt/autosort/traverse-to-reference";control_="/run/autosort-control.json";
#ifdef AUTOSORT_TEST
        root_=c.value("test_root").toString();sock_=c.value("test_socket").toString();track_=root_+"/track.json";vision_=root_+"/vision.json";imageDir_=root_;worker_=c.value("test_worker").toString();control_=root_+"/control.json";
#endif
        require(QDir().mkpath(root_),"state directory failed");
    }
    ~Runtime(){stopChild();}
    void state(const QString &s,const QString &why=QString()){
        phase_=s;write(root_+"/state.json",{{"schema",1},{"state",s},{"reason",why},{"cycle",cycle_},{"motion_enabled",execute_},{"published_monotonic_ms",double(now())},{"arm_job",job_}});
    }
    void recordRpc(const QJsonObject &command,qint64 start,qint64 connectMs,bool complete,const QString &result,const QString &stage,const QString &timeoutStage,const QString &error=QString(),const QJsonObject &serverTiming=QJsonObject()){
        const auto end=now(),elapsed=end-start;
        if(command.value("action")=="status"&&elapsed<100&&result=="ok")return;
        const QJsonObject entry{{"action",command.value("action")},{"requestId",command.value("requestId").toString()},{"started_monotonic_ms",double(start)},{"elapsed_ms",double(elapsed)},{"connect_ms",double(connectMs)},{"reply_complete",complete},{"result",result},{"stage",stage},{"timeout_stage",timeoutStage},{"error",error},{"phase",phase_},{"server_timing",serverTiming}};
        QFile f(root_+"/rpc-timing.jsonl");if(f.open(QIODevice::WriteOnly|QIODevice::Append))f.write(QJsonDocument(entry).toJson(QJsonDocument::Compact)+'\n');
    }
    QJsonObject rpc(QJsonObject command){
        const auto start=now();qint64 connectMs=-1;bool complete=false;QString stage="connect",timeoutStage;
        try{
            require(!stopped||command.value("action")=="status"||command.value("action")=="stop","service stopping");
            QLocalSocket s;s.connectToServer(sock_);const bool connected=s.waitForConnected(500);connectMs=now()-start;
            if(!connected&&s.error()==QLocalSocket::SocketTimeoutError){timeoutStage="connect";throw RpcTimeout("arm connection timeout");}
            require(connected,"arm socket unavailable");stage="peer";
            ucred peer{};socklen_t len=sizeof peer;require(getsockopt(int(s.socketDescriptor()),SOL_SOCKET,SO_PEERCRED,&peer,&len)==0&&peer.uid==geteuid(),"arm peer mismatch");
            stage="request";auto b=QJsonDocument(command).toJson(QJsonDocument::Compact)+'\n';require(b.size()<=4096,"request too large");s.write(b);QByteArray reply;stage="reply";
            while(!complete&&now()-start<=500){s.waitForReadyRead(10);reply+=s.readAll();complete=reply.contains('\n');require(reply.size()<=4096,"arm reply too large");
                require(complete||s.state()!=QLocalSocket::UnconnectedState,"arm socket disconnected before complete reply");
            }
            if(now()-start>500||!complete){timeoutStage="reply";throw RpcTimeout("arm ACK missing or late; never repeat motion");}
            stage="decode";QJsonParseError err;auto d=QJsonDocument::fromJson(reply,&err);require(err.error==QJsonParseError::NoError&&d.isObject(),"bad arm JSON");stage="response";require(d.object().value("ok")==true,"arm request rejected");
            recordRpc(command,start,connectMs,complete,"ok",stage,timeoutStage,QString(),d.object().value("rpcTiming").toObject());return d.object();
        }catch(const std::exception &e){recordRpc(command,start,connectMs,complete,timeoutStage.isEmpty()?"error":"timeout",stage,timeoutStage,QString::fromUtf8(e.what()));throw;}
    }
    void readArm(){arm_=rpc({{"action","status"}});}
    void mutation(QJsonObject command){pendingRequest_=QUuid::createUuid().toString();command["requestId"]=pendingRequest_;command["expectedState"]=expectedState(arm_);arm_=rpc(command);require(arm_.value("lastAutomationRequest").toString()==pendingRequest_,"request ownership missing");pendingRequest_.clear();generation_=arm_.value("commandGeneration").toString();job_=arm_.value("actionJobId").toInt(-1);}
    QJsonObject trackingPacket(){try{return sorting::readObject(track_);}catch(const std::exception &e){throw trial::VisionUnavailable(e.what());}}
    trial::Frame tracking(bool empty=false){return trial::readFrame(trackingPacket(),now(),trial::defaultPolygon(),empty);}
    trial::Frame cameraHealth(){auto raw=trackingPacket();trial::visionRequire(raw.value("objects").isArray(),"camera packet malformed");raw["objects"]=QJsonArray();return trial::readFrame(raw,now(),trial::defaultPolygon(),true);}
    void prune(){
        for(const auto &name:QDir(root_).entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
            const auto m=QRegularExpression("^transit-([0-9]+)$").match(name);
            if(m.hasMatch()&&m.captured(1).toInt()<=cycle_-10){
                require(!QFileInfo(root_+"/"+name).isSymLink(),"unexpected evidence symlink");
                require(QDir(root_+"/"+name).removeRecursively(),"transit retention failed");
            }
        }
        const QRegularExpression pattern("^(?:cycle|completed)-([0-9]+)(?:-frame-[0-9]+\\.jpg|\\.json)$");
        for(const auto &name:QDir(root_).entryList(QDir::Files)){auto m=pattern.match(name);if(m.hasMatch()&&m.captured(1).toInt()<=cycle_-10)require(QFile::remove(root_+"/"+name),"evidence retention failed");}
    }
    void beltStopped(){
#ifdef AUTOSORT_TEST
        require(sorting::readObject(root_+"/belt.json").value("stopped")==true,"belt not stopped");
#else
        const auto r=conveyor::openUio("/dev/uio0");require(traverse::successfulStop(traverse::readProfile(*r)),"belt not stopped/healthy");
#endif
    }
    void stopChild(){if(child_.state()!=QProcess::NotRunning){child_.terminate();if(!child_.waitForFinished(1000)){child_.kill();child_.waitForFinished(1000);}}}
    void pause(int ms=100){require(!stopped,"service stopped");QThread::msleep(unsigned(ms));require(!stopped,"service stopped");}
    bool controlEnabled(){QFile f(control_);if(!f.open(QIODevice::ReadOnly))return false;return QJsonDocument::fromJson(f.readAll()).object().value("enabled").toBool(false);}
    void checkPause(){if(!controlEnabled())throw PauseRequested();}
    void own(){readArm();operating(arm_,job_,generation_,reference_);}
    void checkTarget(){const auto t=tracking();trial::visionRequire(t.session==arrival_.value("source_session").toString()&&t.id==arrival_.value("track_id").toString(),"arrival target replaced");trackGuard_.accept(t);trial::visionRequire(inParkingRegion(t.center),"target outside parking region");}
    void waitIdle(bool action){
        const auto deadline=now()+20000;const auto activePhase=phase_;trial::Guard health;
        while(true){pause();if(actionSubmitted_){if(!controlEnabled()){if(phase_!="PAUSING")state("PAUSING");}else if(phase_=="PAUSING")state(activePhase);}
            bool freshStatus=true;
            try{own();}catch(const RpcTimeout&){if(!actionSubmitted_)throw;freshStatus=false;}
            require(now()<deadline,"arm completion timeout");
            beltStopped();const auto camera=cameraHealth();health.accept(camera);if(!arrival_.isEmpty())trial::visionRequire(camera.session==arrival_.value("source_session").toString(),"camera restarted during motion");
            // Only a fresh status can confirm completion. A late status query
            // leaves the submitted action alone and consumes its existing wait.
            if(!freshStatus)continue;
            if(action){const auto s=arm_.value("actionJobState").toString();require(s=="running"||s=="completed","action interrupted");if(s=="completed"&&arm_.value("moving")==false&&arm_.value("actionPlaying")==false)break;}
            else if(arm_.value("moving")==false&&arm_.value("actionPlaying")==false)break;
        }idle(arm_);
    }
    void verifyFiles(){sorting::verifyActions(cfg_,arm_);require(hash(worker_)==cfg_.value("worker_sha256").toString(),"traverse executable changed");}
    void load(const QString &label){const auto a=cfg_.value("actions").toObject().value(label).toObject();require(hash(a.value("path").toString())==a.value("sha256").toString(),"action file changed");mutation({{"action","load-actions"},{"path",a.value("path")}});require(arm_.value("loadedActionSha256")==a.value("sha256")&&arm_.value("actionFrameCount").toInt()==7,"loaded action mismatch");}
    void prepare(const trial::Frame &target){
        checkPause();state("PREPARING");beltStopped();readArm();idle(arm_,!armed_);require(hash(worker_)==cfg_.value("worker_sha256").toString(),"traverse executable changed");
        if(!armed_){require(arm_.value("enabled")==false&&arm_.value("hardwareOutputActive")==false,"unexpected preexisting arm output");require(arm_.value("actionJobId").toInt(-1)==0&&(arm_.value("mode")=="simulation"||arm_.value("mode")=="hardware"),"startup requires an unused disabled arm session");
            if(arm_.value("mode")=="simulation")mutation({{"action","connect"},{"mode","hardware"}});
            // A newly opened real GPIO backend ALWAYS requires an explicit OFF
            // handshake, even on a cold boot with no output. This is permitted
            // in this initial disabled session, including an existing hardware
            // connection made through the UI before pressing Start. Existing
            // hardware holds and later timeout latches still fault without rearm.
            require(arm_.value("mode")=="hardware"&&arm_.value("hardwareStatusAvailable")==true&&arm_.value("hardwareOutputActive")==false&&arm_.value("enabled")==false&&arm_.value("moving")==false&&arm_.value("actionPlaying")==false&&arm_.value("actionJobId").toInt(-1)==0,"new hardware session is not output-disabled");
            beltStopped();
            mutation({{"action","off"}});idle(arm_);require(arm_.value("hardwareOutputActive")==false&&arm_.value("enabled")==false,"initial OFF not confirmed");
            // Before enabling, verify the saved mapping in its intended debug mode.
            auto mapping=arm_;mapping["debug"]=true;sorting::verifyActions(cfg_,mapping);
            // A stable target already triggered preparation. Finish this owned
            // baseline setup even if detection briefly disappears, then wait.
            mutation({{"action","debug-enable"},{"confirm",true},{"baseline",baseline()}});reference_=model(arm_);armed_=true;
        }else operating(arm_,job_,generation_,reference_);
        waitIdle(false);load("NORMAL");trial::checkArm(arm_,job_);verifyFiles();checkPause();
        const auto t=tracking();trial::visionRequire(t.session==target.session&&t.id==target.id,"target changed during preparation");
    }
    void sampleTransit(const trial::Frame &target){
        const auto t=tracking();
        trial::visionRequire(t.session==target.session&&t.id==target.id,"feeding identity changed");
        if(feedGuard_.accept(t)){feedHistory_.append(t);if(feedHistory_.size()>3000)feedHistory_.removeFirst();}
        collectTransit();
    }
    void collectTransit(){
        if(transitDecision_)return;
        try{
            const auto v=sorting::readObject(vision_);
            bool sequenceOk=false;const auto sequence=v.value("frame_sequence").toString().toULongLong(&sequenceOk);
            if(!sequenceOk)return;
            QVector<trial::Frame> candidates;
            for(int i=0;i<feedHistory_.size();++i){
                const auto &h=feedHistory_[i];
                if(h.seq==sequence){candidates.append(h);break;}
                if(i==0||h.seq<=sequence||feedHistory_[i-1].seq>=sequence)continue;
                const auto &lo=feedHistory_[i-1];
                // Preview localization and inference JPEG have different sampling
                // rates. Associate only inside a short, continuous same-ID segment.
                if(h.captured-lo.captured>250||std::hypot(h.center.x()-lo.center.x(),h.center.y()-lo.center.y())>48)continue;
                const double fraction=double(sequence-lo.seq)/double(h.seq-lo.seq);
                auto matched=lo;matched.seq=sequence;matched.center=lo.center+(h.center-lo.center)*fraction;
                matched.captured=lo.captured+qint64((h.captured-lo.captured)*fraction);
                matched.raw=QJsonObject{{"association","bounded_sequence_interpolation"},{"lower",lo.raw},{"upper",h.raw},{"frame_sequence",QString::number(sequence)}};
                candidates.append(matched);break;
            }
            for(const auto &h:candidates){
                const auto label=classification(v,now(),feedFence_,h,imageDir_,true);
                // Preserve the exact bound image before the producer rotates it.
                const QString dir=root_+QString("/transit-%1").arg(cycle_);
                require(QDir().mkpath(dir),"transit evidence directory failed");
                const QString copy=dir+"/"+QFileInfo(v.value("frame_path").toString()).fileName();
                if(!QFile::exists(copy))require(QFile::copy(v.value("frame_path").toString(),copy),"transit image copy failed");
                lastClass_=v;lastClass_["frame_path"]=copy;lastClass_["saved_image"]=copy;lastClass_["saved_image_sha256"]=hash(copy);
                selected_=label;decisionTrack_=h;transitDecision_=true;return;
            }
        }catch(const std::exception&){/* No matched decision yet: retain the last valid frame. */}
    }
    void collectStopped(){
        if(transitDecision_)return;
        QJsonObject v;
        try{
            v=sorting::readObject(vision_);const auto current=tracking();
            const auto label=classification(v,now(),arrival_,current,imageDir_);
            const QString dir=root_+QString("/transit-%1").arg(cycle_);
            require(QDir().mkpath(dir),"classification evidence directory failed");
            const QString copy=dir+"/"+QFileInfo(v.value("frame_path").toString()).fileName();
            if(!QFile::exists(copy))require(QFile::copy(v.value("frame_path").toString(),copy),"classification image copy failed");
            lastClass_=v;lastClass_["frame_path"]=copy;lastClass_["saved_image"]=copy;lastClass_["saved_image_sha256"]=hash(copy);
            selected_=label;decisionTrack_=current;postStopDecision_=true;transitDecision_=true;
        }catch(const std::exception &e){
            const auto seq=v.value("frame_sequence").toString();
            if(seq!=lastRejectedSequence_){lastRejectedSequence_=seq;
                write(root_+"/classification-rejection.json",{{"cycle",cycle_},{"frame_sequence",seq},{"reason",QString::fromUtf8(e.what())},{"arrival",arrival_},{"published_monotonic_ms",double(now())}});
            }
        }
    }
    QString transitLabel(){
        trial::visionRequire(arrival_.value("source_session")==feedFence_.value("source_session")&&arrival_.value("track_id")==feedFence_.value("track_id"),"transit arrival identity mismatch");
        require(hash(lastClass_.value("saved_image").toString())==lastClass_.value("saved_image_sha256").toString(),"transit evidence changed");
        return classification(lastClass_,now(),postStopDecision_?arrival_:feedFence_,postStopDecision_?tracking():decisionTrack_,QFileInfo(lastClass_.value("frame_path").toString()).absolutePath(),!postStopDecision_);
    }
    void feed(const trial::Frame &target){
        checkPause();state("FEEDING");const auto t=tracking();trial::visionRequire(t.session==target.session&&t.id==target.id,"target changed before feeding");own();trial::checkArm(arm_,job_);beltStopped();
        feedHistory_.clear();feedGuard_=trial::Guard();transitDecision_=false;postStopDecision_=false;lastRejectedSequence_.clear();
        feedFence_=QJsonObject{{"source_session",target.session},{"track_id",target.id},{"frame_sequence",QString::number(t.seq)},{"capture_fence_monotonic_ms",double(now())}};
        child_.start(worker_,{"--expected-arm-job",QString::number(job_),"--expected-generation",generation_,"--expected-ui-session",arm_.value("consoleSessionId").toString(),"--execute"});require(child_.waitForStarted(1000),"cannot start conveyor worker");const auto deadline=now()+25000;
        while(!child_.waitForFinished(10)){checkPause();require(!stopped&&now()<deadline,"conveyor worker timeout/stop");sampleTransit(target);}checkPause();
        const auto out=child_.readAllStandardOutput();write(root_+"/worker.json",{{"stdout",QString::fromUtf8(out)},{"stderr",QString::fromUtf8(child_.readAllStandardError())}});
        require(child_.exitStatus()==QProcess::NormalExit,"conveyor worker terminated unexpectedly");QJsonParseError error;const auto doc=QJsonDocument::fromJson(out,&error);require(error.error==QJsonParseError::NoError&&doc.isObject(),"bad arrival result");arrival_=doc.object();
        if(child_.exitCode()!=0&&arrival_.value("failure_class")=="vision"&&arrival_.value("recoverable")==true&&arrival_.value("stop_verified")==true&&traverse::successfulStop(arrival_.value("stop_readback").toObject()))throw trial::VisionUnavailable(arrival_.value("reason").toString().toStdString());
        require(child_.exitCode()==0,"conveyor control failure; no retry");
        require(arrival_.value("status")=="success"&&arrival_.value("stop_verified")==true,"arrival stop not verified");trial::visionRequire(arrival_.value("source_session")==target.session&&arrival_.value("track_id")==target.id,"arrival identity changed");beltStopped();own();trackGuard_=trial::Guard();checkTarget();
    }
    void classify(){
        state("CLASSIFYING");const auto deadline=now()+30000;
        // Join two independent completions: verified arrival and a result from
        // the current object, whether captured during feeding or after arrival.
        while(now()<deadline){
            checkPause();own();idle(arm_);beltStopped();checkTarget();collectTransit();collectStopped();
            if(transitDecision_){
                trial::visionRequire(transitLabel()==selected_,"transit decision changed");
                bool ok=false;const auto arrivalSeq=arrival_.value("frame_sequence").toString().toULongLong(&ok);
                if(!postStopDecision_)trial::visionRequire(ok&&decisionTrack_.seq<=arrivalSeq&&decisionTrack_.captured<=arrival_.value("capture_fence_monotonic_ms").toDouble(),"classification captured after arrival");
                write(root_+QString("/cycle-%1.json").arg(cycle_),{{"arrival",arrival_},{"classification",QJsonArray{lastClass_}},{"decision",selected_},{"decision_policy",postStopDecision_?"valid_current_object_after_arrival":"matched_in_transit_frame"},{"association_tracking",decisionTrack_.raw},{"feed_fence",feedFence_},{"before_job",job_},{"ready_monotonic_ms",double(now())}});return;
            }
            pause(50);
        }
        // Missing inference is not a reason to feed the same object again.
        throw std::runtime_error("no valid current-object classification within30s; stopped for inspection");
    }

    void sort(){
        checkPause();state("SORTING");own();idle(arm_);beltStopped();checkTarget();verifyFiles();load(selected_);checkTarget();beltStopped();
        trial::visionRequire(transitDecision_&&transitLabel()==selected_,"classification changed before play");
        const int previous=job_;const auto sha=cfg_.value("actions").toObject().value(selected_).toObject().value("sha256");checkPause();actionSubmitted_=true;mutation({{"action","play-actions"},{"confirm",true},{"actionSha256",sha}});require(job_==previous+1,"unexpected new action job");waitIdle(true);
        write(root_+QString("/completed-%1.json").arg(cycle_),arm_);
        // Expected occlusion while picking does not erase the camera-health guard.
        state("RETURNING");own();idle(arm_);beltStopped();mutation({{"action","debug-pulses"},{"pulses",baseline()},{"durationMs",2000}});waitIdle(false);load("NORMAL");trial::checkArm(arm_,job_);
    }
    void visionStandby(const QString &reason){
        stopChild();beltStopped();readArm();idle(arm_,!armed_);
        if(armed_){
            operating(arm_,job_,generation_,reference_);verifyFiles();
            const auto normal=cfg_.value("actions").toObject().value("NORMAL").toObject();
            if(arm_.value("actionFile")!=normal.value("path")||arm_.value("loadedActionSha256")!=normal.value("sha256"))load("NORMAL");
            trial::checkArm(arm_,job_);
        }else require(arm_.value("enabled")==false&&arm_.value("hardwareOutputActive")==false,"unowned arm output");
        arrival_=QJsonObject();lastClass_=QJsonObject();selected_.clear();trackGuard_=trial::Guard();waitCaptureAfter_=now();
        state("WAIT_OBJECT",reason);
    }
    void waitObject(bool clearFirst){
        Presence presence;state(controlEnabled()?(clearFirst?"WAIT_CLEAR":"WAIT_OBJECT"):"PAUSED");
        while(true){pause();
            if(!controlEnabled()){presence.reset();if(phase_!="PAUSED")state("PAUSED");continue;}
            if(phase_=="PAUSED"){presence.reset();state(clearFirst?"WAIT_CLEAR":"WAIT_OBJECT");}
            beltStopped();readArm();idle(arm_,!armed_);if(armed_)operating(arm_,job_,generation_,reference_);else require(arm_.value("enabled")==false&&arm_.value("hardwareOutputActive")==false,"unowned arm output");
            trial::Frame t;try{
                t=tracking(true);if(t.captured<=waitCaptureAfter_){presence.reset();continue;}
                if(clearFirst){if(!t.id.isEmpty()){presence.reset();continue;}if(presence.accept(t)){clearFirst=false;presence.reset();state("WAIT_OBJECT");}continue;}
                if(t.id.isEmpty()||(t.center.x()>=1213&&!inParkingRegion(t.center))){presence.reset();continue;}
                if(!presence.accept(t))continue;
            }catch(const std::exception&){presence.reset();continue;}
            if(!execute_){state("OBJECT_READY_OBSERVE_ONLY");continue;}
            ++cycle_;prune();actionSubmitted_=false;arrival_=QJsonObject();lastClass_=QJsonObject();selected_.clear();
            try{prepare(t);feed(t);classify();sort();}
            catch(const PauseRequested&){visionStandby("user paused");presence.reset();state("PAUSED");continue;}
            catch(const trial::VisionUnavailable &e){if(actionSubmitted_)throw;visionStandby(QString::fromUtf8(e.what()));clearFirst=false;presence.reset();continue;}
            actionSubmitted_=false;clearFirst=true;presence.reset();state(controlEnabled()?"WAIT_CLEAR":"PAUSED");
        }
    }
    void run(const QJsonObject &resume=QJsonObject()){
        state("PAUSED");while(!controlEnabled())pause();
        state("WAIT_SERVICES");auto deadline=now()+60000;
        while(true){if(!controlEnabled()){state("PAUSED");while(!controlEnabled())pause();state("WAIT_SERVICES");deadline=now()+60000;}try{readArm();require(arm_.value("automationProtocol").toInt()==1,"guarded UI not ready");break;}catch(const std::exception&){require(now()<deadline,"guarded UI unavailable after60s");pause(250);}}
        if(!resume.isEmpty()){
            // Explicit operator resume only. Adopt a verified idle first-pose
            // hold without disabling/re-enabling outputs or clearing a fault.
            require(expectedState(arm_)==resume,"ready-session snapshot changed before explicit resume");
            idle(arm_);job_=arm_.value("actionJobId").toInt(-1);trial::checkArm(arm_,job_);beltStopped();
            generation_=arm_.value("commandGeneration").toString();reference_=model(arm_);verifyFiles();armed_=true;
        }
        waitObject(false);
    }
    void fault(const QString &reason){
        stopChild();
        // Only stop a task still owned by this service; never disable holding PWM.
        try{readArm();if(!pendingRequest_.isEmpty()&&arm_.value("lastAutomationRequest").toString()==pendingRequest_){armed_=true;job_=arm_.value("actionJobId").toInt(-1);generation_=arm_.value("commandGeneration").toString();reference_=model(arm_);}
            if(armed_&&model(arm_)==reference_&&arm_.value("actionJobId").toInt(-1)==job_&&arm_.value("commandGeneration").toString()==generation_){auto cmd=QJsonObject{{"action","stop"},{"expectedState",expectedState(arm_)}};rpc(cmd);}}catch(...){}
        try{beltStopped();state("FAULT",reason);}catch(...){try{state("FAULT_STOP_UNVERIFIED",reason);}catch(...){std::fputs("Fault state write failed; check stderr\n",stderr);}}
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);std::signal(SIGTERM,signalStop);std::signal(SIGINT,signalStop);
    const auto args=app.arguments();const bool resume=args.size()==4&&args[1]=="--resume-ready";
    if(!resume&&(args.size()!=3||(args[1]!="--boot"&&args[1]!="--observe")))return 2;
    int lock=-1;std::unique_ptr<Runtime> runtime;
    try{
        const auto cfg=sorting::readObject(args[2]);const bool execute=args[1]!="--observe";
        require(cfg.value("schema")==1&&cfg.value("startup_pose_confirmed")==true,"startup pose strategy unconfirmed");
        QString run="/run";
#ifdef AUTOSORT_TEST
        run=cfg.value("test_root").toString();
#endif
        QDir().mkpath(run);lock=::open((run+"/autosort.lock").toUtf8().constData(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);require(lock>=0&&flock(lock,LOCK_EX|LOCK_NB)==0,"autosort owner already running");
        if(execute){
#ifndef AUTOSORT_TEST
            require(QString::fromUtf8(bytes("/proc/sys/kernel/random/boot_id")).trimmed()!=cfg.value("installed_boot_id").toString(),"activation is for next boot; current held session unchanged");
#endif
            const int once=::open((run+"/autosort.started").toUtf8().constData(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);require(once>=0,"same-boot restart blocked; inspect fault");::close(once);
        }
        const auto snapshot=resume?sorting::readObject(args[3]):QJsonObject();
        require(!resume||!snapshot.isEmpty(),"explicit resume snapshot is empty");
        runtime.reset(new Runtime(cfg,execute));runtime->run(snapshot);
    }catch(const std::exception &e){if(runtime)runtime->fault(QString::fromUtf8(e.what()));std::fprintf(stderr,"autosort stopped: %s\n",e.what());return 1;}
    return 0;
}
