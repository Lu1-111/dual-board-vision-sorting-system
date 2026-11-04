#include "camerapreviewclient.h"
#include <QBuffer>
#include <QHostAddress>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QtGlobal>
#include <ctime>

namespace {
const qint64 kMaxJpeg=8*1024*1024;
const qint64 kFreshMs=2000;
qint64 monoMs() {
    timespec t{};
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1;
    return qint64(t.tv_sec)*1000+t.tv_nsec/1000000;
}
bool digits(const QByteArray& value,quint64* result) {
    if(value.isEmpty()||value.size()>20)return false;
    for(char c:value)if(c<'0'||c>'9')return false;
    bool ok=false;*result=value.toULongLong(&ok);return ok;
}
bool jpegDimensions(const QByteArray& bytes,QSize* size) {
    const auto* b=reinterpret_cast<const uchar*>(bytes.constData());
    const int count=bytes.size();
    if(count<4||b[0]!=0xff||b[1]!=0xd8||b[count-2]!=0xff||b[count-1]!=0xd9)return false;
    int p=2;
    while(p+1<count) {
        if(b[p++]!=0xff)return false;
        while(p<count&&b[p]==0xff)++p;
        if(p>=count)return false;
        const int marker=b[p++];
        if(marker==0xd9||marker==0xda)return false;
        if(marker==1||(marker>=0xd0&&marker<=0xd7))continue;
        if(p+2>count)return false;
        const int length=(int(b[p])<<8)|b[p+1];
        if(length<2||length>count-p)return false;
        const bool sof=marker>=0xc0&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8&&marker!=0xcc;
        if(sof) {
            if(length<8||b[p+2]!=8)return false;
            const int height=(int(b[p+3])<<8)|b[p+4];
            const int width=(int(b[p+5])<<8)|b[p+6];
            if(width<=0||height<=0||width>4096||height>4096||qint64(width)*height>8000000)return false;
            *size=QSize(width,height);return true;
        }
        p+=length;
    }
    return false;
}
}

CameraPreviewClient::CameraPreviewClient(QString url,int targetFps,QObject* parent)
    :QObject(parent),url_(url),intervalMs_(1000/qBound(1,targetFps,30)) {
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<QJsonObject>("QJsonObject");
}
void CameraPreviewClient::state(const QString& status,const QString& message) {
    if(status_==status&&message_==message)return;
    status_=status;message_=message;emit stateChanged(status,message);
}
void CameraPreviewClient::start() {
    if(running_)return;
    QHostAddress address;
    if(url_.scheme()!=QStringLiteral("http")||!url_.userInfo().isEmpty()||url_.hasFragment()||
       !address.setAddress(url_.host())||address.protocol()!=QAbstractSocket::IPv4Protocol||
       url_.port(80)<=0||url_.port(80)>65535) {
        state(QStringLiteral("error"),QStringLiteral("预览地址必须是有效的 HTTP IPv4 地址。"));return;
    }
    if(!manager_) {
        manager_=new QNetworkAccessManager(this);
        manager_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        next_=new QTimer(this);next_->setSingleShot(true);
        deadline_=new QTimer(this);deadline_->setSingleShot(true);
        freshness_=new QTimer(this);freshness_->setInterval(100);
        connect(next_,&QTimer::timeout,this,&CameraPreviewClient::requestFrame);
        connect(deadline_,&QTimer::timeout,this,&CameraPreviewClient::requestTimedOut);
        connect(freshness_,&QTimer::timeout,this,&CameraPreviewClient::checkFreshness);
    }
    running_=true;waitingAck_=false;startedMs_=monoMs();lastUniqueMs_=-1;fpsWindowMs_=0;fpsWindowFrames_=0;fps_=0;
    currentSession_.clear();retiredSessions_.clear();currentSequence_=0;
    if(startedMs_<0){running_=false;state(QStringLiteral("error"),QStringLiteral("单调时钟不可用。"));return;}
    state(QStringLiteral("connecting"),QStringLiteral("正在连接独立相机预览。"));
    freshness_->start();scheduleRequest();
}
void CameraPreviewClient::stop() {
    running_=false;waitingAck_=false;
    if(next_)next_->stop();if(deadline_)deadline_->stop();if(freshness_)freshness_->stop();
    clearReply();bytes_.clear();
    state(QStringLiteral("stopped"),QStringLiteral("相机预览已停止。"));
}
void CameraPreviewClient::clearReply() {
    if(!reply_)return;
    QNetworkReply* old=reply_;reply_=nullptr;
    disconnect(old,nullptr,this,nullptr);
    if(old->isRunning())old->abort();
    old->deleteLater();
}
void CameraPreviewClient::acknowledgeFrame() {
    if(!running_||!waitingAck_)return;
    waitingAck_=false;scheduleRequest();
}
void CameraPreviewClient::scheduleRequest(int minimumDelayMs) {
    if(!running_||reply_||waitingAck_)return;
    const qint64 elapsed=qMax<qint64>(0,monoMs()-requestStartedMs_);
    next_->start(qMax(minimumDelayMs,int(qMax<qint64>(0,intervalMs_-elapsed))));
}
void CameraPreviewClient::requestFrame() {
    if(!running_||reply_||waitingAck_)return;
    requestStartedMs_=monoMs();bytes_.clear();expectedBytes_=-1;headersValid_=false;rejected_=false;headerStale_=false;
    QNetworkRequest request(url_);
    request.setRawHeader("Accept","image/jpeg");
    request.setRawHeader("Connection","close");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute,false);
    reply_=manager_->get(request);
    reply_->setReadBufferSize(65536);
    connect(reply_,&QNetworkReply::readyRead,this,&CameraPreviewClient::readAvailable);
    connect(reply_,&QNetworkReply::finished,this,&CameraPreviewClient::requestFinished);
    deadline_->start(2000);
}
bool CameraPreviewClient::inspectHeaders(QString* error) {
    if(headersValid_)return true;
    if(reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){*error=QStringLiteral("相机 HTTP 状态不是 200。");return false;}
    QSet<QByteArray> seen;qint64 headerBytes=0;
    for(const auto& header:reply_->rawHeaderPairs()) {
        QByteArray key=header.first.toLower();headerBytes+=key.size()+header.second.size()+4;
        if(seen.contains(key)||headerBytes>65536){*error=QStringLiteral("相机响应头重复或过大。");return false;}seen.insert(key);
    }
    if(reply_->hasRawHeader("Transfer-Encoding")){*error=QStringLiteral("相机预览要求 Content-Length 响应。");return false;}
    if(reply_->rawHeader("Content-Type").split(';').first().trimmed().toLower()!="image/jpeg"){*error=QStringLiteral("相机未返回 JPEG。");return false;}
    if(reply_->rawHeader("X-Camera-Protocol")!="1"){*error=QStringLiteral("相机协议版本不匹配。");return false;}
    quint64 width=0,height=0;
    if(!digits(reply_->rawHeader("X-Frame-Width"),&width)||!digits(reply_->rawHeader("X-Frame-Height"),&height)||
       width==0||height==0||width>4096||height>4096||width*height>8000000){*error=QStringLiteral("相机响应头图像尺寸无效。");return false;}
    incomingWidth_=int(width);incomingHeight_=int(height);
    quint64 length=0,age=0;
    if(!digits(reply_->rawHeader("Content-Length"),&length)||length<4||length>quint64(kMaxJpeg)){*error=QStringLiteral("相机 JPEG 大小无效。");return false;}
    if(!digits(reply_->rawHeader("X-Frame-Age-Ms"),&age)){*error=QStringLiteral("相机源帧年龄无效。");return false;}
    if(age>quint64(kFreshMs)){headerStale_=true;*error=QStringLiteral("相机源帧已经过期。");return false;}
    if(!digits(reply_->rawHeader("X-Frame-Sequence"),&incomingSequence_)){*error=QStringLiteral("相机帧号无效。");return false;}
    incomingSession_=QString::fromLatin1(reply_->rawHeader("X-Camera-Session"));
    if(!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,128}$")).match(incomingSession_).hasMatch()){*error=QStringLiteral("相机会话编号无效。");return false;}
    if(reply_->rawHeader("X-Camera-Source")!="imx415"){*error=QStringLiteral("相机来源不匹配。");return false;}
    incomingCaptured_=QString::fromLatin1(reply_->rawHeader("X-Captured-Utc"));
    if(incomingCaptured_.isEmpty()||incomingCaptured_.size()>128){*error=QStringLiteral("相机采集时间缺失。");return false;}
    expectedBytes_=qint64(length);incomingAgeMs_=qint64(age);headersValid_=true;return true;
}
void CameraPreviewClient::readAvailable() {
    if(!reply_||rejected_)return;
    if(monoMs()-requestStartedMs_>=2000){requestTimedOut();return;}
    QString error;
    if(!inspectHeaders(&error)){rejectRequest(headerStale_?QStringLiteral("source_stale"):QStringLiteral("connecting"),error);return;}
    while(reply_&&reply_->bytesAvailable()>0) {
        QByteArray part=reply_->read(qMin<qint64>(65536,reply_->bytesAvailable()));
        if(part.isEmpty())break;
        if(bytes_.size()>expectedBytes_-part.size()||bytes_.size()>kMaxJpeg-part.size()){rejectRequest(QStringLiteral("error"),QStringLiteral("相机 JPEG 超过声明大小。"));return;}
        bytes_.append(part);
    }
}
void CameraPreviewClient::rejectRequest(const QString& status,const QString& message) {
    rejected_=true;
    state(status,message);
    // abort() may synchronously emit finished; make cleanup and retry explicit.
    if(deadline_)deadline_->stop();
    clearReply();bytes_.clear();
    scheduleRequest(qMin(2000,qMax(250,intervalMs_)));
}
void CameraPreviewClient::requestTimedOut() {
    if(reply_)rejectRequest(QStringLiteral("connecting"),QStringLiteral("相机预览请求超过 2 秒。"));
}
void CameraPreviewClient::requestFinished() {
    if(!reply_||rejected_)return;
    deadline_->stop();
    if(monoMs()-requestStartedMs_>=2000){rejectRequest(QStringLiteral("connecting"),QStringLiteral("相机预览请求超过 2 秒。"));return;}
    if(reply_->error()!=QNetworkReply::NoError){rejectRequest(QStringLiteral("connecting"),QStringLiteral("相机预览连接失败，正在重试。"));return;}
    readAvailable();if(!reply_||rejected_)return;
    QString error;
    if(!inspectHeaders(&error)){rejectRequest(headerStale_?QStringLiteral("source_stale"):QStringLiteral("connecting"),error);return;}
    if(bytes_.size()!=expectedBytes_){rejectRequest(QStringLiteral("connecting"),QStringLiteral("相机 JPEG 接收不完整。"));return;}
    clearReply();
    if(!finishImage(&error)) {
        if(!error.isEmpty())state(QStringLiteral("source_stale"),error);
        bytes_.clear();scheduleRequest();return;
    }
    bytes_.clear();
    // A successful frame is followed only by acknowledgeFrame(), never a queued burst.
}
bool CameraPreviewClient::finishImage(QString* error) {
    const qint64 now=monoMs();
    if(incomingAgeMs_+qMax<qint64>(0,now-requestStartedMs_)>kFreshMs){*error=QStringLiteral("相机源帧累计超过 2 秒。");return false;}
    if(incomingSession_==currentSession_) {
        if(incomingSequence_==currentSequence_)return false; // Normal duplicate; do not refresh freshness.
        if(incomingSequence_<currentSequence_){*error=QStringLiteral("拒绝倒退的相机帧号。");return false;}
    } else if(retiredSessions_.contains(incomingSession_)){*error=QStringLiteral("拒绝已结束的相机会话回放。");return false;}
    if(incomingSession_!=currentSession_&&!currentSession_.isEmpty()&&retiredSessions_.size()>=64){*error=QStringLiteral("相机会话重启过多，请重启预览。");return false;}
    QSize sourceSize;
    if(!jpegDimensions(bytes_,&sourceSize)){*error=QStringLiteral("相机 JPEG 尺寸或结束标记无效。");return false;}
    if(sourceSize!=QSize(incomingWidth_,incomingHeight_)){*error=QStringLiteral("相机响应头与 JPEG 尺寸不一致。");return false;}
    QBuffer buffer(&bytes_);buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer,"jpg");reader.setAutoTransform(true);
    if(reader.size()!=sourceSize){*error=QStringLiteral("相机 JPEG 尺寸信息不一致。");return false;}
    QSize decodeSize=sourceSize;
    if(sourceSize.width()>960||sourceSize.height()>540)
        decodeSize=sourceSize.scaled(QSize(960,540),Qt::KeepAspectRatio);
    reader.setScaledSize(decodeSize);
    QImage image=reader.read();
    if(image.isNull()){*error=QStringLiteral("相机 JPEG 解码失败。");return false;}
    if(image.width()>960||image.height()>540)image=image.scaled(QSize(960,540),Qt::KeepAspectRatio,Qt::FastTransformation);
    image=image.convertToFormat(QImage::Format_RGB888);
    const qint64 published=monoMs(),age=incomingAgeMs_+qMax<qint64>(0,published-requestStartedMs_);
    if(age>kFreshMs){*error=QStringLiteral("相机 JPEG 解码后已过期。");return false;}
    const bool newSession=incomingSession_!=currentSession_;
    if(newSession) {
        if(!currentSession_.isEmpty())retiredSessions_.insert(currentSession_);
        currentSession_=incomingSession_;fpsWindowMs_=published;fpsWindowFrames_=0;fps_=0;
    }
    currentSequence_=incomingSequence_;lastUniqueMs_=published;lastSourceAgeMs_=age;
    if(!newSession)++fpsWindowFrames_;
    if(fpsWindowMs_==0)fpsWindowMs_=published;
    if(published-fpsWindowMs_>=1000){fps_=1000.0*fpsWindowFrames_/double(published-fpsWindowMs_);fpsWindowFrames_=0;fpsWindowMs_=published;}
    QJsonObject meta;
    meta.insert(QStringLiteral("source_session"),currentSession_);
    meta.insert(QStringLiteral("frame_sequence"),QString::number(currentSequence_));
    meta.insert(QStringLiteral("source_frame_age_ms"),double(age));
    meta.insert(QStringLiteral("published_monotonic_ms"),double(published));
    meta.insert(QStringLiteral("captured_utc"),incomingCaptured_);
    meta.insert(QStringLiteral("source"),QStringLiteral("imx415"));
    meta.insert(QStringLiteral("width"),sourceSize.width());meta.insert(QStringLiteral("height"),sourceSize.height());
    meta.insert(QStringLiteral("fps"),fps_);
    waitingAck_=true;
    state(QStringLiteral("ok"),QStringLiteral("独立实时预览，不等待模型推理。"));
    emit frameReady(image,meta);
    return true;
}
void CameraPreviewClient::checkFreshness() {
    if(!running_)return;
    const qint64 now=monoMs();
    if((lastUniqueMs_<0&&now-startedMs_>kFreshMs)||
       (lastUniqueMs_>=0&&(now-lastUniqueMs_>kFreshMs||lastSourceAgeMs_+qMax<qint64>(0,now-lastUniqueMs_)>kFreshMs)))
        state(QStringLiteral("source_stale"),QStringLiteral("超过 2 秒没有新的有效相机画面。"));
}
