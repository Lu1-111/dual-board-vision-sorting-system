#include "arm_console.hpp"
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <QDebug>
#include <QtAlgorithms>
#include <cstdio>
#include <cstring>
#ifdef Q_OS_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <chrono>

namespace {
QByteArray selectSocketPath(){
    const QByteArray requested=qgetenv("HDMI_UI_ARM_SOCKET");
    return requested.isEmpty()?QByteArray("/tmp/hdmi-ui-arm.sock"):requested;
}
const QByteArray socketPathValue=selectSocketPath();
const char *const socketPath=socketPathValue.constData();
bool validSocketPath(){
    struct sockaddr_un address={};
    return socketPathValue.startsWith('/')&&socketPathValue.size()<int(sizeof(address.sun_path))&&
           !socketPathValue.contains('\0');
}
const int packetLimit = 4096;
QJsonObject failure(const QString &s) { return {{"ok",false},{"error",s}}; }
bool parse(const QByteArray &bytes,QJsonObject &object,QString &error) {
    QJsonParseError issue;const QJsonDocument doc=QJsonDocument::fromJson(bytes,&issue);
    if(issue.error!=QJsonParseError::NoError||!doc.isObject()) {error=QStringLiteral("Expected one JSON object");return false;}
    object=doc.object();
    static const QSet<QString> allowed={"show","status","connect","debug-enable","debug-pulses","stop","off","load-actions","play-actions",
                                      "coordinate-zero","coordinate-base","coordinate-preview","coordinate-move","xy-preview","xy-move",
                                      "fixed-sort-preview","fixed-sort-run",
                                      "conveyor-status","conveyor-show","conveyor-start","conveyor-demo","conveyor-stop","conveyor-reset",
                                      "conveyor-release","conveyor-preview","conveyor-configure","conveyor-screenshot","conveyor-auto-start"};
    if(!object.value("action").isString()||!allowed.contains(object.value("action").toString())) {error=QStringLiteral("Unsupported action");return false;}
    return true;
}
QByteArray packet(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n'; }
void nonblocking(int fd) {fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);fcntl(fd,F_SETFD,FD_CLOEXEC);}

class Peer : public QObject {
public:
    Peer(int fd,const ArmConsole::Handler &handler,QObject *parent):QObject(parent),fd_(fd),handler_(handler) {
        nonblocking(fd_);read_=new QSocketNotifier(fd_,QSocketNotifier::Read,this);
        write_=new QSocketNotifier(fd_,QSocketNotifier::Write,this);write_->setEnabled(false);
        QObject::connect(read_,&QSocketNotifier::activated,this,[this](int){receive();});
        QObject::connect(write_,&QSocketNotifier::activated,this,[this](int){flush();});
        QTimer::singleShot(3000,this,[this](){finish();});
    }
    ~Peer() override {if(fd_>=0)::close(fd_);}
private:
    void finish(){if(fd_<0)return;read_->setEnabled(false);write_->setEnabled(false);::close(fd_);fd_=-1;deleteLater();}
    void reply(const QJsonObject &object){read_->setEnabled(false);output_=packet(object);if(output_.size()>packetLimit)output_=packet(failure("Response too large"));flush();}
    void receive(){
        char block[1024];
        for(;;){const ssize_t n=::recv(fd_,block,sizeof(block),0);
            if(n>0){input_.append(block,int(n));if(input_.size()>packetLimit){reply(failure("Request too large"));return;}
                const int end=input_.indexOf('\n');if(end>=0){
                    if(!input_.mid(end+1).trimmed().isEmpty()){reply(failure("Only one request is accepted"));return;}
                    QJsonObject request;QString error;
                    if(!parse(input_.left(end),request,error)){reply(failure(error));return;}
                    reply(handler_(request));return;
                }
            }else if(n==0){finish();return;}
            else if(errno==EINTR)continue;
            else if(errno==EAGAIN||errno==EWOULDBLOCK)return;
            else{finish();return;}
        }
    }
    void flush(){
        while(!output_.isEmpty()){
            const ssize_t n=::send(fd_,output_.constData(),size_t(output_.size()),MSG_NOSIGNAL);
            if(n>0)output_.remove(0,int(n));
            else if(n<0&&errno==EINTR)continue;
            else if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){write_->setEnabled(true);return;}
            else{finish();return;}
        }
        finish();
    }
    int fd_;ArmConsole::Handler handler_;QSocketNotifier *read_,*write_;QByteArray input_,output_;
};
}

struct ArmConsole::Private {
    Handler handler;int fd=-1;dev_t device=0;ino_t inode=0;QSocketNotifier *notifier=nullptr;
};
ArmConsole::ArmConsole(Handler h,QObject *p):QObject(p),d(new Private){d->handler=h;}
ArmConsole::~ArmConsole(){
    if(d->notifier)d->notifier->setEnabled(false);
    const auto owned=children();qDeleteAll(owned);
    if(d->fd>=0)::close(d->fd);
    struct stat st;
    if(d->inode&&::lstat(socketPath,&st)==0&&st.st_dev==d->device&&st.st_ino==d->inode&&S_ISSOCK(st.st_mode))::unlink(socketPath);
    delete d;
}
bool ArmConsole::start(QString &error){
    if(!validSocketPath()){error="Console socket path must be absolute and fit sockaddr_un";return false;}
    struct stat prior;
    if(::lstat(socketPath,&prior)==0){
        if(!S_ISSOCK(prior.st_mode)||prior.st_uid!=::geteuid()||(prior.st_mode&0777)!=0600){error="Console path is occupied by an unrelated file";return false;}
        const int probe=::socket(AF_UNIX,SOCK_STREAM,0);if(probe<0){error=QString::fromLocal8Bit(::strerror(errno));return false;}
        nonblocking(probe);struct sockaddr_un addr={};addr.sun_family=AF_UNIX;std::strcpy(addr.sun_path,socketPath);
        const int result=::connect(probe,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));const int reason=errno;::close(probe);
        if(result==0||reason!=ECONNREFUSED){error="A console is already active at this path";return false;}
        struct stat again;
        if(::lstat(socketPath,&again)!=0||again.st_dev!=prior.st_dev||again.st_ino!=prior.st_ino||::unlink(socketPath)!=0){error="Console path changed during stale socket cleanup";return false;}
    }else if(errno!=ENOENT){error=QString::fromLocal8Bit(::strerror(errno));return false;}
    d->fd=::socket(AF_UNIX,SOCK_STREAM,0);if(d->fd<0){error=QString::fromLocal8Bit(::strerror(errno));return false;}
    nonblocking(d->fd);struct sockaddr_un addr={};addr.sun_family=AF_UNIX;std::strcpy(addr.sun_path,socketPath);
    const mode_t mask=::umask(0077);const int bound=::bind(d->fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));const int bindError=errno;::umask(mask);
    if(bound<0){error=QString::fromLocal8Bit(::strerror(bindError));return false;}
    struct stat own;if(::lstat(socketPath,&own)==0){d->device=own.st_dev;d->inode=own.st_ino;}
    if(::chmod(socketPath,0600)!=0||::listen(d->fd,4)!=0){error=QString::fromLocal8Bit(::strerror(errno));return false;}
    d->notifier=new QSocketNotifier(d->fd,QSocketNotifier::Read,this);
    QObject::connect(d->notifier,&QSocketNotifier::activated,this,[this](int){
        for(;;){const int fd=::accept(d->fd,nullptr,nullptr);if(fd<0){if(errno==EINTR)continue;return;}
            struct ucred credentials;socklen_t length=sizeof(credentials);
            if(::getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&credentials,&length)!=0||credentials.uid!=::geteuid()||children().size()>10){::close(fd);continue;}
            new Peer(fd,d->handler,this);
        }
    });
    return true;
}

int ArmConsole::command(const QString &json){
    QByteArray request=json.toUtf8();QJsonObject object;QString error;
    auto fail=[](const QString &s){const QByteArray result=packet(failure(s));std::fwrite(result.constData(),1,size_t(result.size()),stdout);return 2;};
    if(!validSocketPath())return fail("Console socket path must be absolute and fit sockaddr_un");
    if(request.size()+1>packetLimit||request.contains('\n')||!parse(request,object,error))return fail(error.isEmpty()?QStringLiteral("Invalid request size or framing"):error);
    request=packet(object);
    const int fd=::socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)return fail(QString::fromLocal8Bit(::strerror(errno)));
    struct Closer{int fd;~Closer(){::close(fd);}}closer{fd};nonblocking(fd);
    struct sockaddr_un addr={};addr.sun_family=AF_UNIX;std::strcpy(addr.sun_path,socketPath);
    if(::connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0)return fail(QStringLiteral("Cannot reach running Qt console: ")+QString::fromLocal8Bit(::strerror(errno)));
    struct ucred credentials;socklen_t length=sizeof(credentials);
    if(::getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&credentials,&length)!=0||credentials.uid!=::geteuid())return fail("Console peer UID does not match");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(2500);
    QByteArray response;int sent=0;
    while(std::chrono::steady_clock::now()<deadline){
        const int remaining=int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count());
        struct pollfd event={fd,short(sent<request.size()?POLLOUT:POLLIN),0};
        const int ready=::poll(&event,1,remaining);if(ready<0&&errno==EINTR)continue;if(ready<=0)break;
        if(sent<request.size()){
            const ssize_t n=::send(fd,request.constData()+sent,size_t(request.size()-sent),MSG_NOSIGNAL);
            if(n>0)sent+=int(n);else if(n<0&&(errno==EINTR||errno==EAGAIN))continue;else return fail("Console write failed");
        }else{
            char bytes[1024];const ssize_t n=::recv(fd,bytes,sizeof(bytes),0);
            if(n>0){response.append(bytes,int(n));if(response.size()>packetLimit)return fail("Console response too large");
                if(response.contains('\n')){QJsonParseError issue;const auto doc=QJsonDocument::fromJson(response.left(response.indexOf('\n')),&issue);
                    if(issue.error!=QJsonParseError::NoError||!doc.isObject())return fail("Invalid console response");
                    const QByteArray result=packet(doc.object());std::fwrite(result.constData(),1,size_t(result.size()),stdout);return doc.object().value("ok").toBool()?0:1;}
            }else if(n<0&&(errno==EINTR||errno==EAGAIN))continue;else return fail("Console closed before response");
        }
    }
    return fail("Console response timed out");
}
#else
struct ArmConsole::Private {};
ArmConsole::ArmConsole(Handler,QObject *p):QObject(p),d(new Private){}
ArmConsole::~ArmConsole(){delete d;}
bool ArmConsole::start(QString &error){error="Local console requires Linux";return false;}
int ArmConsole::command(const QString &){std::fputs("{\"ok\":false,\"error\":\"Local console requires Linux\"}\n",stdout);return 2;}
#endif
