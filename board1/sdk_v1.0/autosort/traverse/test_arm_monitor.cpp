// Compile the real production ArmMonitor, not a reimplementation.
#define main traverse_production_main
#include "main.cpp"
#undef main
#include <QLocalServer>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
static qint64 fixtureNow=1000;
static qint64 fixtureClock(){return fixtureNow;}
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL line%d %s\n",__LINE__,#c);return 1;}}while(0)
static bool pump(const std::function<bool()> &done){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<1000){QCoreApplication::processEvents(QEventLoop::AllEvents,1);QThread::msleep(1);}return done();}
struct Server {
 QTemporaryDir dir;QLocalServer listener;QList<QLocalSocket*> requests;QLocalSocket *lastSent=nullptr;int count=0;
 Server(){if(!dir.isValid()||!listener.listen(dir.path()+"/arm.sock"))throw std::runtime_error("fixture listen failed");QObject::connect(&listener,&QLocalServer::newConnection,[this](){while(listener.hasPendingConnections()){auto *s=listener.nextPendingConnection();QObject::connect(s,&QLocalSocket::readyRead,[this,s](){if(s->canReadLine()){s->readLine();requests.append(s);++count;}});}});}
 QString name()const{return listener.fullServerName();}
 bool respond(const char *tag){if(requests.isEmpty())return false;auto *s=requests.takeFirst();lastSent=s;s->write(QJsonDocument(QJsonObject{{"ok",true},{"tag",QString::fromLatin1(tag)}}).toJson(QJsonDocument::Compact)+"\n");s->flush();return true;}
};
int main(int argc,char**argv){QCoreApplication app(argc,argv);int cases=0;
 {Server s;fixtureNow=1000;ArmMonitor m(0,false,s.name(),fixtureClock);m.poll(fixtureNow);CHECK(pump([&](){return s.count==1;}));fixtureNow=1005;CHECK(s.respond("first"));CHECK(pump([&](){return m.stamp()==1000;}));
  fixtureNow=1999;m.poll(fixtureNow);CHECK(m.ready(1999)&&m.ready(2000)&&!m.ready(2001)&&m.stamp()==1000);CHECK(pump([&](){return s.count==2;}));fixtureNow=2001;m.poll(fixtureNow);CHECK(!m.ready(fixtureNow));++cases;}
 {Server s;fixtureNow=1000;ArmMonitor m(0,false,s.name(),fixtureClock);m.poll(fixtureNow);CHECK(pump([&](){return s.count==1;}));fixtureNow=1500;CHECK(s.respond("exact500"));CHECK(pump([&](){return m.stamp()==1000;}));CHECK(m.value()["tag"].toString()=="exact500");++cases;}
 {Server s;fixtureNow=1000;ArmMonitor m(0,false,s.name(),fixtureClock);m.poll(fixtureNow);CHECK(pump([&](){return s.count==1;}));fixtureNow=1005;CHECK(s.respond("old"));CHECK(pump([&](){return m.stamp()==1000;}));fixtureNow=1040;m.poll(fixtureNow);CHECK(pump([&](){return s.count==2;}));fixtureNow=1541;CHECK(s.respond("late"));CHECK(pump([&](){return s.lastSent->state()==QLocalSocket::UnconnectedState;}));bool rejected=false;try{m.poll(fixtureNow);}catch(const std::exception&e){rejected=QString::fromUtf8(e.what())=="arm status reply exceeded500ms";}CHECK(rejected);CHECK(m.stamp()==1000&&m.value()["tag"].toString()=="old");++cases;}
 {Server s;fixtureNow=1000;ArmMonitor m(0,false,s.name(),fixtureClock);m.poll(fixtureNow);CHECK(pump([&](){return s.count==1;}));QTimer::singleShot(0,[](){fixtureNow=1501;});bool rejected=false;try{m.poll(1000);}catch(const std::exception&e){rejected=QString::fromUtf8(e.what())=="arm status request exceeded500ms";}CHECK(rejected&&m.stamp()==-1);++cases;}
 {Server s;fixtureNow=1000;ArmMonitor m(0,false,s.name(),fixtureClock);m.poll(fixtureNow);CHECK(pump([&](){return s.count==1;}));fixtureNow=1001;CHECK(s.respond("interval"));CHECK(pump([&](){return m.stamp()==1000;}));fixtureNow=1039;m.poll(fixtureNow);CHECK(s.count==1);fixtureNow=1040;m.poll(fixtureNow);CHECK(pump([&](){return s.count==2;}));++cases;}
 {Server s;fixtureNow=1000;ArmMonitor m(0,false,s.name(),fixtureClock);m.poll(fixtureNow);CHECK(pump([&](){return s.count==1;}));fixtureNow=1499;CHECK(s.respond("under499"));CHECK(pump([&](){return m.stamp()==1000;}));CHECK(m.value()["tag"].toString()=="under499"&&m.diagnostics()["last_reply_elapsed_ms"].toInt()==499);++cases;}
 std::printf("ARM_MONITOR_NATIVE_PASS cases=%d actualAsyncClass realUnixSockets clockInjection:999/1000 accepted1001 rejected; exact500 reply accepted; late501 reply rejected without stamp/value renewal; postEventClock deadline;40ms interval\n",cases);return 0;}
