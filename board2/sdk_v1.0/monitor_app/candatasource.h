#ifndef MONITOR_CANDATASOURCE_H
#define MONITOR_CANDATASOURCE_H
#include "datasource.h"
#include "../can_monitor/transport.h"
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QQueue>
class QSocketNotifier;
class CanDataSource : public IDataSource {
    Q_OBJECT
public:
    CanDataSource(const QString &endpoint,bool udp,bool start=true,QObject *parent=nullptr,bool loopback=false);
    ~CanDataSource() override;
    QString sourceName() const override { return udp_?QStringLiteral("CAN 模拟"):loopback_?QStringLiteral("CAN 自检"):QStringLiteral("CAN 接收"); }
    bool isCan() const override { return true; }
    bool isSimulation() const override { return udp_ || loopback_; }
    void ingest(const LineCan::Frame &frame);
    void setPaused(bool paused) { paused_=paused; }
    quint64 received() const { return received_; }
    quint64 rejected() const { return rejected_; }
    quint64 acknowledgements() const { return acks_; }
    int pendingAcknowledgements() const { return pendingAcks_.size(); }
signals:
    void diagnostics(const QString &text);
    void peerMode(bool simulated);
private:
    void poll();
    void drain();
    void flushAck();
    void clearAcks();
    QString endpoint_; bool udp_,autoStart_,loopback_,paused_=false;
    LineCan::Transport io_; QSocketNotifier *notifier_=nullptr; QTimer timer_;
    struct PendingAck { LineCan::Frame frame; qint64 queuedAt; };
    QQueue<PendingAck> pendingAcks_; QTimer ackTimer_; QElapsedTimer ackClock_;
    QElapsedTimer heartbeatClock_,helloClock_;
    uint32_t session_=0,lastUptime_=0; uint8_t sequence_=0,countSequence_=0; bool haveBeat_=false,peerSim_=false;
    quint64 received_=0,rejected_=0,acks_=0;
    quint64 ackErrors_=0,ackExpired_=0;
    uint32_t counts_[5]={}; unsigned countMask_=0;
    QHash<quint32,AlarmEvent> events_; quint32 latestByType_[3]={};
};
#endif
