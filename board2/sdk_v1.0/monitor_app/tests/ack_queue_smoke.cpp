#include "../candatasource.h"
#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include <vector>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void pump(int ms) {
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<ms) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    using namespace LineCan;
    CanDataSource source(QStringLiteral("39171"),true);
    pump(300);
    Transport peer; CHECK(peer.openUdp(39171,false));
    CHECK(peer.send(hello(100,true))); CHECK(peer.send(heartbeat(0,1,3,0)));
    std::vector<Frame> expected;
    for(unsigned i=0;i<60;++i) {
        Frame f=alarm(i%3+1,i%3+1,1,true,i);
        CHECK(peer.send(f)); f.id=Ack; expected.push_back(f);
    }
    QElapsedTimer timer; timer.start(); int received=0; qint64 first=-1,last=-1;
    while(timer.elapsed()<1500 && received<60) {
        QCoreApplication::processEvents(); Frame f;
        while(peer.receive(f)==1) {
            CHECK(received<60 && f.id==Ack && !std::memcmp(f.data,expected[received].data,8));
            if(first<0) first=timer.elapsed();
            last=timer.elapsed(); ++received;
        }
        QThread::msleep(1);
    }
    CHECK(received==60 && source.acknowledgements()==60 && source.pendingAcknowledgements()==0);
    CHECK(last-first>=230); // a burst must be paced, not submitted in one drain()
    CHECK(peer.send(alarm(1,1,1,true,61))); CHECK(peer.send(hello(101,true)));
    pump(30); Frame f; CHECK(peer.receive(f)==0); // old-session queued ACK cancelled
    CHECK(source.pendingAcknowledgements()==0);
    std::printf("ACK queue: PASS (60/60 ordered ACKs, span=%lldms, old-session cancellation)\n",static_cast<long long>(last-first));
    return 0;
}
