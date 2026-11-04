#include "../camerapreviewclient.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const QStringList args=app.arguments();
    if((args.size()-1)%2!=0)return 2;
    QString url=QStringLiteral("http://127.0.0.1:47003/v1/frame.jpg");int target=10,frames=5,timeout=15000;
    for(int i=1;i+1<args.size();i+=2) {
        if(args[i]=="--url")url=args[i+1];
        else if(args[i]=="--fps")target=args[i+1].toInt();
        else if(args[i]=="--frames")frames=args[i+1].toInt();
        else if(args[i]=="--timeout-ms")timeout=args[i+1].toInt();
        else return 2;
    }
    if(frames<1||timeout<1000||timeout>600000)return 2;
    QThread thread;auto* client=new CameraPreviewClient(url,target);
    client->moveToThread(&thread);
    QObject::connect(&thread,&QThread::started,client,&CameraPreviewClient::start);
    QObject::connect(&thread,&QThread::finished,client,&QObject::deleteLater);
    int count=0,exitCode=1;
    QObject::connect(client,&CameraPreviewClient::stateChanged,&app,[](const QString& status,const QString& message) {
        QJsonObject s;s.insert("status",status);s.insert("message",message);
        const auto text=QJsonDocument(s).toJson(QJsonDocument::Compact);
        std::printf("STATE %s\n",text.constData());std::fflush(stdout);
    });
    QObject::connect(client,&CameraPreviewClient::frameReady,&app,[&](const QImage& image,const QJsonObject& meta) {
        if(image.isNull()||image.width()>960||image.height()>540||image.format()!=QImage::Format_RGB888||
           image.width()>meta.value("width").toInt()||image.height()>meta.value("height").toInt()||
           meta.value("source_frame_age_ms").toDouble(-1)<0||meta.value("source_frame_age_ms").toDouble(3000)>2000||
           meta.value("source_session").toString().isEmpty()||meta.value("frame_sequence").toString().isEmpty()) {
            std::fprintf(stderr,"FRAME_CONTRACT_FAILED\n");exitCode=2;app.quit();return;
        }
        QJsonObject row=meta;row.insert("display_width",image.width());row.insert("display_height",image.height());
        const auto text=QJsonDocument(row).toJson(QJsonDocument::Compact);
        std::printf("FRAME %s\n",text.constData());std::fflush(stdout);
        ++count;QMetaObject::invokeMethod(client,"acknowledgeFrame",Qt::QueuedConnection);
        if(count>=frames){exitCode=0;app.quit();}
    });
    QTimer deadline;deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,&app,[&](){std::fprintf(stderr,"SMOKE_TIMEOUT frames=%d\n",count);exitCode=1;app.quit();});
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&app,[&](){
        if(thread.isRunning()){QMetaObject::invokeMethod(client,"stop",Qt::BlockingQueuedConnection);thread.quit();thread.wait();}
    });
    thread.start();deadline.start(timeout);app.exec();
    std::printf("SMOKE_EXIT=%d frames=%d\n",exitCode,count);return exitCode;
}
