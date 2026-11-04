#include "latest_frame_server.hpp"
#include <QGuiApplication>
#include <QPainter>
#include <QSocketNotifier>
#include <QTimer>
#include <cstdio>
#include <unistd.h>

int main(int argc,char**argv){
    qputenv("QT_QPA_PLATFORM","offscreen");QGuiApplication app(argc,argv);
    if(argc!=4)return 2;const bool tracking=QString::fromLocal8Bit(argv[3])=="1";
    LatestFrameServer server;QString error;
    if(!server.start("127.0.0.1",QString::fromLocal8Bit(argv[1]).toUShort(),3,95,error,15,640,75,tracking,QString::fromLocal8Bit(argv[2]))){std::fprintf(stderr,"%s\n",qPrintable(error));return 3;}
    QImage image(1920,1080,QImage::Format_RGB32);image.fill(qRgb(40,40,40));
    {QPainter painter(&image);painter.fillRect(QRect(900,420,240,240),QColor(0,0,255));}
    bool publishing=true;quint64 sequence=0;server.beginSession(true);
    QTimer frames;QObject::connect(&frames,&QTimer::timeout,&app,[&](){if(publishing)server.offerFrame(image,QDateTime::currentDateTimeUtc(),++sequence);});frames.start(30);
    QSocketNotifier input(STDIN_FILENO,QSocketNotifier::Read);QByteArray commands;
    QObject::connect(&input,&QSocketNotifier::activated,&app,[&](int){
        char bytes[128];ssize_t count=::read(STDIN_FILENO,bytes,sizeof(bytes));if(count<=0){app.quit();return;}commands.append(bytes,int(count));
        while(commands.contains('\n')){const int p=commands.indexOf('\n');const auto cmd=commands.left(p);commands.remove(0,p+1);
            if(cmd=="pause")publishing=false;
            if(cmd=="resume")publishing=true;
            if(cmd=="move"){image.fill(qRgb(40,40,40));QPainter painter(&image);painter.fillRect(QRect(1140,420,240,240),QColor(0,0,255));}
            if(cmd=="stop"){publishing=false;server.stopSession("fixture_stopped");}
            if(cmd=="begin"){sequence=0;server.beginSession(true);publishing=true;}
            if(cmd=="quit")app.quit();std::printf("ACK %s\n",cmd.constData());std::fflush(stdout);
        }
    });std::puts("READY");std::fflush(stdout);return app.exec();
}
