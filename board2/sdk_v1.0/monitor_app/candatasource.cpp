#include "candatasource.h"
#include <QSocketNotifier>
#include <QDebug>
using namespace LineCan;
CanDataSource::CanDataSource(const QString &endpoint,bool udp,bool start,QObject *parent,bool loopback)
    : IDataSource(parent),endpoint_(endpoint),udp_(udp),autoStart_(start),loopback_(loopback) {
    connect(&timer_,&QTimer::timeout,this,&CanDataSource::poll); timer_.start(250);
    // This BSP uses one PTB without TX backpressure. Never burst ACK writes
    // from drain(), even when several received alarms arrive in one GUI turn.
    ackClock_.start(); ackTimer_.setSingleShot(true); ackTimer_.setTimerType(Qt::PreciseTimer);
    connect(&ackTimer_,&QTimer::timeout,this,&CanDataSource::flushAck);
}
CanDataSource::~CanDataSource() { delete notifier_; }
void CanDataSource::clearAcks() {
    pendingAcks_.clear(); ackTimer_.stop();
}
void CanDataSource::flushAck() {
    if(io_.fd()<0) { clearAcks(); return; }
    while(!pendingAcks_.isEmpty() && ackClock_.elapsed()-pendingAcks_.head().queuedAt>3000) {
        pendingAcks_.dequeue(); ++ackExpired_;
    }
    if(pendingAcks_.isEmpty()) return;
    if(io_.send(pendingAcks_.head().frame)) { pendingAcks_.dequeue(); ++acks_; }
    else { ++ackErrors_; } // retain the head for a later attempt, up to its expiry
    if(!pendingAcks_.isEmpty()) ackTimer_.start(5);
}
void CanDataSource::poll() {
    if(autoStart_ && io_.fd()<0) {
        const bool ok=udp_?io_.openUdp(endpoint_.toUShort(),true):io_.openCan(endpoint_.toStdString());
        if(ok) {
            notifier_=new QSocketNotifier(io_.fd(),QSocketNotifier::Read,this);
            connect(notifier_,&QSocketNotifier::activated,this,[this](int){drain();});
        }
    }
    const bool fresh=heartbeatClock_.isValid() && heartbeatClock_.elapsed()<=6000 && helloClock_.isValid() && helloClock_.elapsed()<=6000;
    if(!fresh) {
        emit linkState(false,heartbeatClock_.isValid()?QStringLiteral("通信超时 · 数据已过期"):QStringLiteral("等待第一块板数据"));
    }
    emit diagnostics(QStringLiteral("%1\n接收 %2 · 确认 %3\n丢弃 %4%5\n待确认 %6 · 发送失败 %7 · 过期 %8")
        .arg(udp_?QStringLiteral("CAN 协议模拟"):loopback_?QStringLiteral("CAN 内部回环"):QStringLiteral("CAN · %1").arg(endpoint_))
        .arg(received_).arg(acks_).arg(rejected_)
        .arg(autoStart_&&io_.fd()<0?QStringLiteral("\n接口未就绪"):QString())
        .arg(pendingAcks_.size()).arg(ackErrors_).arg(ackExpired_));
}
void CanDataSource::drain() {
    // Bound each event-loop turn so a busy bus cannot freeze touch or rendering.
    for(int i=0;i<128;++i) {
        LineCan::Frame f; const int result=io_.receive(f);
        if(!result) break;
        if(result==-1) {
            qWarning()<<"CAN transport:"<<io_.error().c_str();
            delete notifier_; notifier_=nullptr; io_.close(); heartbeatClock_.invalidate(); clearAcks();
            emit linkState(false,QStringLiteral("CAN 接口异常 · 数据已过期")); break;
        }
        if(result==-2) { ++rejected_; continue; }
        if(paused_) continue;
        ingest(f);
    }
}
void CanDataSource::ingest(const LineCan::Frame &f) {
    if(!valid(f)) { ++rejected_; return; }
    if(f.id==Ack) return;
    ++received_; const auto *p=f.data;
    if(f.id==Hello) {
        const uint32_t s=u32(p+4);
        if(s!=session_) {
            clearAcks();
            session_=s; events_.clear(); std::memset(latestByType_,0,sizeof(latestByType_));
            countMask_=0; haveBeat_=false; heartbeatClock_.invalidate();
            emit sessionReset();
            qInfo()<<"CAN session"<<session_;
        }
        peerSim_=p[1]; emit peerMode(isSimulation() || peerSim_); helloClock_.restart(); return;
    }
    if(!session_ || !helloClock_.isValid() || helloClock_.elapsed()>6000) { ++rejected_; return; }
    if(f.id==LineCan::Heartbeat) {
        uint32_t uptime=u32(p+4);
        if(haveBeat_ && ((p[1]==sequence_ && uptime<=lastUptime_) || (uint8_t(p[1]-sequence_)>128 && uptime<=lastUptime_))) { ++rejected_; return; }
        sequence_=p[1]; lastUptime_=uptime; haveBeat_=true; heartbeatClock_.restart();
        countMask_=0; countSequence_=sequence_;
        ::Heartbeat beat; beat.sequence=sequence_; beat.runState=RunState(p[2]); beat.activeAlarmCount=p[3]; beat.uptimeS=uptime;
        emit heartbeat(beat); emit linkState(true,QStringLiteral("正常 · 序号 %1").arg(sequence_)); return;
    }
    if(!haveBeat_ || !heartbeatClock_.isValid() || heartbeatClock_.elapsed()>6000) { ++rejected_; return; }
    if(f.id==Status) {
        if(p[1]!=sequence_) { ++rejected_; return; }
        LineStatus s; s.runState=RunState(p[2]); s.armState=p[3]; s.visionState=p[4]; s.currentColor=MaterialColor(p[5]); s.confidence100=p[6]; emit status(s); return;
    }
    if(f.id>=CountBase && f.id<CountBase+5) {
        if(p[1]!=countSequence_) { ++rejected_; return; }
        unsigned i=f.id-CountBase; counts_[i]=u32(p+4); countMask_|=1u<<i;
        if(countMask_==31) { emit counters(counts_[0],counts_[1],counts_[2],counts_[3],counts_[4]); countMask_=0; }
        return;
    }
    if(f.id==Alarm) {
        quint32 id=u32(p); const unsigned type=p[4]>>4; bool active=p[5];
        quint32 &last=latestByType_[type-1];
        if(id<last) { ++rejected_; return; }
        if(last && id>last && events_.contains(last) && events_[last].active) {
            // A later event proves the old event ended, but its recovery time was lost.
            AlarmEvent old=events_[last]; old.active=false; old.recoveredAt=QDateTime(); old.recoveryUnknown=true;
            events_[last]=old; emit alarmCleared(old);
        }
        if(last && last!=id) events_.remove(last);
        last=id;
        auto existing=events_.find(id);
        if(existing!=events_.end() && (existing->type!=AlarmType(type) || (!existing->active && active))) { ++rejected_; return; }
        AlarmEvent event; event.sessionId=session_; event.simulated=isSimulation() || peerSim_; event.eventId=id; event.type=AlarmType(type); event.color=MaterialColor(p[4]&15);
        event.active=active; event.durationS=u16(p+6);
        event.detectedAt=existing!=events_.end()?existing->detectedAt:QDateTime::currentDateTime().addSecs(-event.durationS);
        if(!active) event.recoveredAt=existing!=events_.end()&&!existing->active?existing->recoveredAt:QDateTime::currentDateTime();
        const bool isNew=existing==events_.end(); const bool changed=!isNew && existing->active!=active;
        events_.insert(id,event);
        if(isNew) emit alarmRaised(event);
        if(!active && (isNew||changed)) emit alarmCleared(event);
        if(io_.fd()>=0) {
            Frame ack=f; ack.id=Ack;
            if(pendingAcks_.size()>=64) { pendingAcks_.dequeue(); ++ackExpired_; }
            pendingAcks_.enqueue({ack,ackClock_.elapsed()});
            if(!ackTimer_.isActive()) ackTimer_.start(5);
        }
    }
}
