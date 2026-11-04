#include "../candatasource.h"
#include "../monitorwindow.h"
#include <QApplication>
#include <QProcess>
#include <QThread>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QFontDatabase>
#include <QFile>
#include <QTextStream>
#include <QPushButton>
#include <cstdio>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void pump(int ms) { QElapsedTimer t; t.start(); while(t.elapsed()<ms) { qApp->processEvents(); QThread::msleep(5); } }
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    const QString fontPath=argc>4?QString::fromLocal8Bit(argv[4]):QStringLiteral("/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc");
    int font=QFontDatabase::addApplicationFont(fontPath);
    if(font>=0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).first(),10));
    using namespace LineCan;
    Frame f=hello(0x12345678,true);
    CHECK(f.data[4]==0x78 && f.data[7]==0x12 && valid(f));
    uint8_t packed[13]; pack(f,packed); CHECK(unpack(packed).id==Hello);
    f.data[0]=2; CHECK(!valid(f)); f=heartbeat(255,3,3,0xffffffff); CHECK(valid(f));
    f.len=7; CHECK(!valid(f)); f.len=8; f.id|=CAN_RTR_FLAG; CHECK(!valid(f));
    CHECK(!valid(status(1,1,0,1,0,90))); CHECK(!valid(status(1,1,0,1,1,101)));
    CHECK(!valid(alarm(0,1,1,true,0)));
    CanDataSource source(QString(),false,false);
    MonitorWindow window(&source); window.resize(1024,600); window.show(); pump(20);
    unsigned raises=0,clears=0,counts=0,resets=0; quint32 red=0; bool online=false;
    QObject::connect(&source,&IDataSource::alarmRaised,[&](const AlarmEvent&){++raises;});
    QObject::connect(&source,&IDataSource::alarmCleared,[&](const AlarmEvent&){++clears;});
    QObject::connect(&source,&IDataSource::counters,[&](quint32 r,quint32,quint32,quint32,quint32){++counts;red=r;});
    QObject::connect(&source,&IDataSource::sessionReset,[&](){++resets;});
    QObject::connect(&source,&IDataSource::linkState,[&](bool up,const QString&){online=up;});
    source.ingest(alarm(1,1,1,true,0)); CHECK(!raises);
    source.ingest(hello(100,false)); source.ingest(heartbeat(254,1,1,10)); CHECK(online && resets==1);
    for(int i=0;i<4;++i) source.ingest(counter(254,i,100+i)); CHECK(counts==0);
    source.ingest(counter(253,4,104)); CHECK(counts==0);
    source.ingest(counter(254,4,104)); CHECK(counts==1 && red==100);
    source.ingest(alarm(300,1,1,true,2)); source.ingest(alarm(300,1,1,true,3)); CHECK(raises==1);
    source.ingest(alarm(300,1,1,false,4)); source.ingest(alarm(300,1,1,false,4)); CHECK(clears==1);
    source.ingest(alarm(300,1,1,true,3)); CHECK(raises==1); // old active snapshot cannot resurrect clear
    source.ingest(alarm(301,2,1,false,8)); CHECK(raises==2 && clears==2); // first activation was lost
    source.ingest(alarm(302,1,1,true,0)); source.ingest(alarm(303,1,1,true,0)); CHECK(clears==3);
    source.ingest(heartbeat(255,1,1,11)); source.ingest(heartbeat(0,1,1,12)); CHECK(online);
    pump(6300); CHECK(!online);
    auto *state=window.findChild<QLabel*>(QStringLiteral("bigState")); CHECK(state && state->text()==QStringLiteral("未知"));
    source.ingest(hello(100,false)); source.ingest(heartbeat(0,1,1,12)); CHECK(!online); // duplicate cannot keep connection alive
    source.ingest(heartbeat(1,1,1,13)); CHECK(online);
    source.ingest(hello(101,false)); CHECK(resets==2);
    auto *table=window.findChild<QTableWidget*>(QStringLiteral("alarmTable")); CHECK(table && table->rowCount()==0);
    source.ingest(heartbeat(0,1,0,0)); source.ingest(alarm(1,3,0,true,0)); CHECK(raises==5);
    // Genuine controller states must still display accurately, independent of alarms/pages.
    unsigned stateSequence=10;
    for(unsigned run: {1u,2u,3u,0u}) {
        source.ingest(hello(101,false)); ++stateSequence; source.ingest(heartbeat(uint8_t(stateSequence),run,1,stateSequence));
        pump(2050); pump(2050); pump(2050); CHECK(state->text()==runStateName(RunState(run)));
        for(auto *nav:window.findChildren<QPushButton*>()) if(nav->text()==QStringLiteral("实时视觉")) nav->click();
        CHECK(state->text()==runStateName(RunState(run)));
        for(auto *nav:window.findChildren<QPushButton*>()) if(nav->text()==QStringLiteral("监控总览")) nav->click();
        CHECK(state->text()==runStateName(RunState(run)));
    }
    // Separate process integration, actual transport encoding, ACK and receiver outage recovery.
    CHECK(argc>=3);
    const QString sender=QString::fromLocal8Bit(argv[1]); const QString endpoint=QString::fromLocal8Bit(argv[2]);
    const bool udp=!endpoint.startsWith(QStringLiteral("can"));
    CanDataSource live(endpoint,udp,true,nullptr,!udp);
    MonitorWindow liveWindow(&live); window.hide(); liveWindow.resize(1024,600); liveWindow.show();
    unsigned liveRaises=0,liveClears=0; bool liveOnline=false;
    unsigned demoAlarmMask=0; bool demoAlwaysRunning=true; quint32 firstBad=0,lastBad=0; bool badSeen=false;
    QObject::connect(&live,&IDataSource::heartbeat,[&](const ::Heartbeat &h){demoAlwaysRunning=demoAlwaysRunning && h.runState==RunState::Running;});
    QObject::connect(&live,&IDataSource::alarmRaised,[&](const AlarmEvent &e){demoAlarmMask|=1u<<(unsigned(e.type)-1);});
    QObject::connect(&live,&IDataSource::counters,[&](quint32,quint32,quint32,quint32,quint32 bad){if(bad && !badSeen) {firstBad=bad;badSeen=true;} lastBad=bad;});
    QObject::connect(&live,&IDataSource::alarmRaised,[&](const AlarmEvent&){++liveRaises;});
    QObject::connect(&live,&IDataSource::alarmCleared,[&](const AlarmEvent&){++liveClears;});
    QObject::connect(&live,&IDataSource::linkState,[&](bool up,const QString&){liveOnline=up;});
    pump(400);
    QProcess process; process.start(sender,{udp?QStringLiteral("--udp"):QStringLiteral("--can"),endpoint,QStringLiteral("--demo"),QStringLiteral("--interval-ms"),QStringLiteral("50"),QStringLiteral("--ticks"),QStringLiteral("140")});
    CHECK(process.waitForStarted(3000));
    QElapsedTimer limit; limit.start();
    while(process.state()!=QProcess::NotRunning && limit.elapsed()<16000) pump(10);
    if(process.state()!=QProcess::NotRunning) {process.kill();process.waitForFinished();CHECK(false);}
    QByteArray log=process.readAllStandardOutput()+process.readAllStandardError(); std::fwrite(log.data(),1,size_t(log.size()),stdout);
    CHECK(process.exitCode()==0 && log.contains("ERR=0")); CHECK(!log.contains("STOP TX=0"));
    const int ackIndex=log.lastIndexOf("ACK="); CHECK(ackIndex>=0 && log.mid(ackIndex+4).split(' ').first().toInt()>0);
    CHECK(liveRaises>=3 && liveClears>=3 && live.received()>100);
    CHECK(demoAlwaysRunning && demoAlarmMask==7 && badSeen && lastBad>firstBad);
    if(argc>3) CHECK(liveWindow.grab().save(QString::fromLocal8Bit(argv[3])));
    pump(6400); CHECK(!liveOnline);
    process.start(sender,{udp?QStringLiteral("--udp"):QStringLiteral("--can"),endpoint,QStringLiteral("--demo"),QStringLiteral("--ticks"),QStringLiteral("4"),QStringLiteral("--interval-ms"),QStringLiteral("100")});
    CHECK(process.waitForStarted(3000)); pump(650); CHECK(liveOnline);
    CHECK(process.waitForFinished(1000) || process.state()==QProcess::NotRunning);
    std::puts("CAN smoke: PASS (protocol, dedup, counts, restart, timeout, transport, ACK)"); return 0;
}
