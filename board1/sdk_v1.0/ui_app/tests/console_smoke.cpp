#include "../arm_console.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QJsonDocument>
#include <cstdio>
#ifdef Q_OS_LINUX
#include <sys/stat.h>
#endif

class ClientThread : public QThread {
public:
    QString request;
    int result=-1;
protected:
    void run() override {result=ArmConsole::command(request);}
};

int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().size()==3&&app.arguments()[1]=="--client")return ArmConsole::command(app.arguments()[2]);
    int calls=0;QString error;
    ArmConsole server([&calls](const QJsonObject &request){++calls;return QJsonObject{{"ok",true},{"action",request.value("action")}};});
    if(!server.start(error)){std::fprintf(stderr,"%s\n",qPrintable(error));return 1;}
#ifdef Q_OS_LINUX
    const QByteArray customSocket=qgetenv("HDMI_UI_ARM_SOCKET");
    const QByteArray socket=customSocket.isEmpty()?QByteArray("/tmp/hdmi-ui-arm.sock"):customSocket;
    struct stat st;if(::lstat(socket.constData(),&st)!=0||(st.st_mode&0777)!=0600)return 2;
#endif
    ArmConsole duplicate([](const QJsonObject&){return QJsonObject{{"ok",false}};});
    if(duplicate.start(error))return 3;
    auto run=[](const QString &request,int expected){
        ClientThread child;child.request=request;QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
        QObject::connect(&child,&QThread::finished,&loop,&QEventLoop::quit);
        QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
        child.start();timeout.start(5000);loop.exec();
        // command() has a 2.5 second transport deadline, including failure paths.
        if(!child.wait(3000)){std::fputs("Console client exceeded its bounded timeout\n",stderr);return false;}
        if(child.result!=expected)std::fprintf(stderr,"Console client returned %d, expected %d\n",child.result,expected);
        return child.result==expected;
    };
    if(!run("{\"action\":\"status\"}",0)||calls!=1){std::fprintf(stderr,"Status dispatch count: %d\n",calls);return 4;}
    if(!run("{\"action\":\"shell\"}",2)||calls!=1)return 5;
    if(!run("{broken}",2)||calls!=1)return 6;
    if(!run(QString(4100,'x'),2)||calls!=1)return 7;
    if(!run("{\"action\":\"off\"}",0)||calls!=2)return 8;
    if(!run("{\"action\":\"xy-preview\"}",0)||calls!=3)return 9;
    if(!run("{\"action\":\"xy-move\"}",0)||calls!=4)return 10;
    std::puts("Local arm console smoke: PASS");return 0;
}
