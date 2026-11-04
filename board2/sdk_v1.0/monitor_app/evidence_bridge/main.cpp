#include "../evidence_store.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSocketNotifier>
#include <QTimer>
#include <QFileInfo>
#include <cstdio>
#include <unistd.h>

static QByteArray token() { QFile f(qEnvironmentVariable("MONITOR_LINK_TOKEN_FILE","/etc/monitor-link.token")); return f.open(QIODevice::ReadOnly)?f.readAll().trimmed():QByteArray(); }
static QByteArray encode(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact)+"\n"; }
static QJsonObject process(const QByteArray &b,bool network) {
    QJsonParseError err; QJsonDocument doc=QJsonDocument::fromJson(b,&err);
    if(err.error!=QJsonParseError::NoError || !doc.isObject()) return Evidence::error("invalid_json");
    QJsonObject o=doc.object();
    if(network && (token().size()<32 || o["token"].toString().toUtf8()!=token())) return Evidence::error("authentication_failed");
    QJsonObject reply=Evidence::rpc(o,network); reply["rpc_id"]=o["rpc_id"]; return reply;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    app.addLibraryPath("/opt/monitor-ui/plugins");
    app.addLibraryPath("/opt/evidence-bridge/plugins");
    QStringList args=app.arguments();
    auto value=[&](const QString &flag,const QString &fallback=QString()) { int n=args.indexOf(flag); return n>=0 && n+1<args.size()?args[n+1]:fallback; };
    if(args.contains("--send") || args.contains("--events")) {
        QJsonObject o{{"op","events"},{"token",QString::fromUtf8(token())}};
        if(args.contains("--send")) {
            QFile f(value("--send")); if(!f.open(QIODevice::ReadOnly) || f.size()>Evidence::MaxImage) { fprintf(stderr,"Image missing or larger than 512 KiB\n"); return 2; }
            QByteArray b=f.readAll(); QString err=Evidence::imageError(b); if(!err.isEmpty()) { fprintf(stderr,"%s\n",qPrintable(err)); return 2; }
            o["op"]="put_image"; o["session_id"]=value("--session").toDouble(); o["event_id"]=value("--event").toDouble();
            o["alarm_type"]=value("--type").toInt(); o["is_anomaly"]=true; o["source"]=value("--source","camera");
            o["captured_at"]=value("--captured-at","unknown"); o["sha256"]=Evidence::sha(b); o["image_b64"]=QString::fromLatin1(b.toBase64());
        }
        QTcpSocket s; s.connectToHost(value("--host","172.22.203.2"),value("--port","47002").toUShort());
        if(!s.waitForConnected(5000)) { fprintf(stderr,"Connection failed\n"); return 3; }
        s.write(encode(o)); while(s.bytesToWrite()>0) if(!s.waitForBytesWritten(10000)) return 3;
        QByteArray reply; while(!reply.contains('\n')) { if(!s.waitForReadyRead(10000)) return 3; reply+=s.readAll(); if(reply.size()>1024*1024) return 3; }
        fwrite(reply.data(),1,reply.size(),stdout); return QJsonDocument::fromJson(reply).object()["ok"].toBool()?0:4;
    }
    if(args.contains("--stdio")) {
        QByteArray buffer; QSocketNotifier notifier(STDIN_FILENO,QSocketNotifier::Read);
        QObject::connect(&notifier,&QSocketNotifier::activated,&app,[&](int) {
            char b[4096]; ssize_t n=::read(STDIN_FILENO,b,sizeof b); if(n<=0) { app.quit(); return; } buffer.append(b,n);
            if(buffer.size()>65536) { buffer.clear(); printf("@MONITOR@{\"ok\":false,\"error\":\"line_too_long\"}\n"); fflush(stdout); return; }
            int end; while((end=buffer.indexOf('\n'))>=0) {
                QByteArray line=buffer.left(end); buffer.remove(0,end+1);
                if(line=="QUIT") { app.quit(); return; }
                QByteArray reply="@MONITOR@"+encode(process(line,false)); fwrite(reply.data(),1,reply.size(),stdout); fflush(stdout);
            }
        });
        printf("@MONITOR@{\"ok\":true,\"ready\":true,\"protocol\":1}\n"); fflush(stdout);
        return app.exec();
    }
    if(args.contains("--listen")) {
        if(token().size()<32) { fprintf(stderr,"Missing link token\n"); return 2; }
        QTcpServer server;
        if(!server.listen(QHostAddress(value("--listen")),value("--port","47002").toUShort())) { fprintf(stderr,"Bind failed\n"); return 2; }
        int connections=0;
        QObject::connect(&server,&QTcpServer::newConnection,&app,[&]() {
            while(server.hasPendingConnections()) {
                QTcpSocket *s=server.nextPendingConnection();
                if(connections>=4) { s->abort(); s->deleteLater(); continue; } ++connections;
                auto buffer=new QByteArray;
                QObject::connect(s,&QTcpSocket::disconnected,&app,[&,s,buffer]() { --connections; delete buffer; s->deleteLater(); });
                QTimer::singleShot(15000,s,[s]() { s->disconnectFromHost(); });
                QObject::connect(s,&QTcpSocket::readyRead,&app,[s,buffer]() {
                    buffer->append(s->readAll());
                    if(buffer->size()>720000) { s->abort(); return; }
                    int end=buffer->indexOf('\n'); if(end<0) return;
                    s->write(encode(process(buffer->left(end),true))); s->disconnectFromHost();
                });
            }
        });
        return app.exec();
    }
    fprintf(stderr,"Use --listen IP | --stdio | --events | --send JPEG --session N --event N --type 1..3 [--source test]\n"); return 2;
}
