#include "latest_frame_server.hpp"
#include "tracking_centers.hpp"

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QWaitCondition>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
const qint64 MaxJpegBytes = 8 * 1024 * 1024;
const qint64 StaleMs = 1500;
const qint64 DemandLeaseMs = 1200;
const qint64 FirstFrameWaitMs = 1200;
const qint64 ReadTimeoutMs = 2000;
const qint64 SendTimeoutMs = 3000;
const int MaxClients = 4;
const int MaxHeaderBytes = 4096;

qint64 monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool nonblocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    const int descriptorFlags = ::fcntl(fd, F_GETFD, 0);
    return flags >= 0 && descriptorFlags >= 0
            && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0
            && ::fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0;
}

struct RawFrame {
    QImage image;
    QString session;
    QString source;
    QDateTime utc;
    quint64 sequence = 0;
    qint64 receivedMs = 0;
};

struct EncodedFrame {
    QByteArray jpeg;
    QString session;
    QString source;
    QDateTime utc;
    quint64 sequence = 0;
    qint64 receivedMs = 0;
    int width = 0;
    int height = 0;
};

struct SharedState {
    QMutex mutex;
    QWaitCondition wake;
    bool quitting = false;
    bool active = false;
    bool hasPending = false;
    bool encoding = false;
    QString session;
    QString source;
    QString reason = QStringLiteral("camera_not_open");
    QString encodeError;
    qint64 demandUntilMs = 0;
    qint64 nextEncodeMs = 0;
    int periodMs = 334;
    int quality = 95;
    bool demandDriven = true;
    int resizeEdge = 0; // zero preserves full-resolution bytes and encoding policy
    bool trackCenters = false;
    QString trackingReferencePath;
    std::unique_ptr<TrackingPublisher> trackingPublisher;
    quint64 encodeCount = 0;
    quint64 replacedCount = 0;
    quint64 offeredCount = 0;
    qint64 lastEncodeMs = 0;
    RawFrame pending;
    EncodedFrame latest;
};

class EncoderThread : public QThread {
public:
    explicit EncoderThread(SharedState &state) : s(state) {}
protected:
    void run() override
    {
        for (;;) {
            RawFrame frame;
            int quality = 95;
            int resizeEdge = 0;
            bool trackCenters = false;
            QString trackingReferencePath;
            {
                QMutexLocker lock(&s.mutex);
                for (;;) {
                    if (s.quitting) return;
                    const qint64 now = monotonicMs();
                    if (!s.active || (s.demandDriven && now >= s.demandUntilMs)) {
                        s.pending = RawFrame();
                        s.hasPending = false;
                    }
                    if (s.active && s.hasPending && (!s.demandDriven || now < s.demandUntilMs)) {
                        if (now < s.nextEncodeMs) {
                            s.wake.wait(&s.mutex, static_cast<unsigned long>(s.nextEncodeMs - now));
                            continue;
                        }
                        frame = s.pending;
                        s.pending = RawFrame();
                        s.hasPending = false;
                        s.encoding = true;
                        s.nextEncodeMs = now + s.periodMs;
                        quality = s.quality;
                        resizeEdge = s.resizeEdge;
                        trackCenters = s.trackCenters;
                        trackingReferencePath = s.trackingReferencePath;
                        break;
                    }
                    s.wake.wait(&s.mutex);
                }
            }
            // No mutex or GUI/capture object is held during the expensive work.
            const qint64 started = monotonicMs();
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, "JPEG");
            writer.setQuality(quality);
            // Preview favors throughput; resizeEdge==0 keeps inference JPEG untouched.
            // The immutable source remains shared; no format conversion/deep copy here.
            QImage encodedImage = resizeEdge > 0 && qMax(frame.image.width(), frame.image.height()) > resizeEdge
                    ? frame.image.scaled(resizeEdge, resizeEdge, Qt::KeepAspectRatio, Qt::FastTransformation)
                    : frame.image;
            QJsonObject tracking;
            QString trackingFailure;
            if(trackCenters) {
                try {
                    if(!tracker)tracker.reset(new TrackingCenters);
                    tracking=tracker->update(frame.image,frame.session,frame.sequence);
                    const auto reference=TrackingCenters::loadReference(trackingReferencePath);
                    if(!reference.isEmpty())tracking["reference_overlay"]=reference;
                    encodedImage=TrackingCenters::overlay(encodedImage,tracking,reference);
                } catch(const std::exception& error) {trackingFailure=QString::fromUtf8(error.what());}
            }
            const bool ok = writer.write(encodedImage);
            const QString failure = !ok ? writer.errorString()
                    : (bytes.size() > MaxJpegBytes ? QStringLiteral("jpeg_too_large") : QString());
            {
                QMutexLocker lock(&s.mutex);
                s.encoding = false;
                ++s.encodeCount;
                s.lastEncodeMs = monotonicMs() - started;
                // Closing/reopening a camera invalidates even an in-flight encode.
                if (s.active && frame.session == s.session) {
                    s.encodeError = failure;
                    if(s.trackingPublisher) {
                        QString publicationError;
                        if(failure.isEmpty()&&trackingFailure.isEmpty()) {
                            if(!s.trackingPublisher->publish(tracking,frame.source,frame.receivedMs,&publicationError))
                                s.encodeError=QStringLiteral("tracking_publication_failed: ")+publicationError;
                        } else {
                            s.trackingPublisher->invalidate("unavailable",failure.isEmpty()?trackingFailure:failure,&publicationError);
                        }
                    }
                    if (failure.isEmpty()) {
                        s.latest.jpeg = bytes;
                        s.latest.session = frame.session;
                        s.latest.source = frame.source;
                        s.latest.utc = frame.utc;
                        s.latest.sequence = frame.sequence;
                        s.latest.receivedMs = frame.receivedMs;
                        s.latest.width = encodedImage.width();
                        s.latest.height = encodedImage.height();
                    }
                }
            }
        }
    }
private:
    SharedState &s;
    std::unique_ptr<TrackingCenters> tracker;
};
}

class LatestFrameServer::Impl {
public:
    class Peer;
    explicit Impl(LatestFrameServer *owner) : q(owner), encoder(s), previewEncoder(preview) {}
    ~Impl();
    bool start(const QString &address, quint16 port, int fps, int quality, QString &error, int previewFps, int previewWidth, int previewQuality,
               bool trackCenters,const QString& trackingOutputDir);
    void acceptClients();
    void requestFrame();
    bool frameSnapshot(EncodedFrame &frame, QString &reason, bool isPreview);
    QJsonObject status();
    LatestFrameServer *q;
    SharedState s;
    SharedState preview;
    EncoderThread encoder;
    EncoderThread previewEncoder;
    int listener = -1;
    QSocketNotifier *acceptNotifier = nullptr;
    QSet<Peer *> peers;
    QString bindAddress;
    quint16 bindPort = 0;
};

class LatestFrameServer::Impl::Peer : public QObject {
public:
    Peer(Impl &server, int fd) : QObject(server.q), server(server), fd(fd)
    {
        server.peers.insert(this);
        readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
        writeNotifier->setEnabled(false);
        connect(readNotifier, &QSocketNotifier::activated, this, [this](int) { readRequest(); });
        connect(writeNotifier, &QSocketNotifier::activated, this, [this](int) { writeResponse(); });
        connect(&timer, &QTimer::timeout, this, [this]() { tick(); });
        timer.start(25);
        deadline = monotonicMs() + ReadTimeoutMs;
    }
    ~Peer() override
    {
        if (fd >= 0) ::close(fd);
        server.peers.remove(this);
    }
private:
    enum State { Reading, Waiting, Sending, Closed } state = Reading;
    Impl &server;
    int fd;
    QSocketNotifier *readNotifier;
    QSocketNotifier *writeNotifier;
    QTimer timer;
    qint64 deadline = 0;
    QByteArray request;
    QByteArray header;
    QByteArray body;
    qint64 headerOffset = 0;
    qint64 bodyOffset = 0;
    bool isPreview = false;

    void finish()
    {
        if (state == Closed) return;
        state = Closed;
        timer.stop();
        readNotifier->setEnabled(false);
        writeNotifier->setEnabled(false);
        ::close(fd);
        fd = -1;
        // Stay in peers until deferred deletion, so connections are always bounded.
        deleteLater();
    }
    void reply(int code, const QByteArray &reason, const QByteArray &type,
               const QByteArray &bytes, const QByteArray &extra = QByteArray())
    {
        if (state == Closed || state == Sending) return;
        readNotifier->setEnabled(false);
        header = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n"
                "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(bytes.size())
                + "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Camera-Protocol: 1\r\n"
                + extra + "\r\n";
        body = bytes; // QByteArray shares the bounded encoded cache until sent.
        state = Sending;
        deadline = monotonicMs() + SendTimeoutMs;
        writeNotifier->setEnabled(true);
        writeResponse();
    }
    void jsonError(int code, const QByteArray &reason, const QString &error)
    {
        QJsonObject value;
        value["ok"] = false;
        value["error"] = error;
        reply(code, reason, "application/json", QJsonDocument(value).toJson(QJsonDocument::Compact),
              code == 503 ? QByteArray("Retry-After: 1\r\n") : QByteArray());
    }
    void tryFrame()
    {
        EncodedFrame frame;
        QString reason;
        if (server.frameSnapshot(frame, reason, isPreview)) {
            const QByteArray extra = "X-Camera-Session: " + frame.session.toLatin1()
                    + "\r\nX-Frame-Sequence: " + QByteArray::number(frame.sequence)
                    + "\r\nX-Frame-Age-Ms: " + QByteArray::number(qMax<qint64>(0, monotonicMs() - frame.receivedMs))
                    + "\r\nX-Captured-Utc: " + frame.utc.toUTC().toString(Qt::ISODateWithMs).toLatin1()
                    + "\r\nX-Frame-Width: " + QByteArray::number(frame.width)
                    + "\r\nX-Frame-Height: " + QByteArray::number(frame.height)
                    + "\r\nX-Camera-Source: " + frame.source.toLatin1() + "\r\n";
            reply(200, "OK", "image/jpeg", frame.jpeg, extra);
        } else if (reason != "frame_stale" && reason != "frame_not_ready") {
            jsonError(503, "Service Unavailable", reason);
        } else if (monotonicMs() >= deadline) {
            jsonError(503, "Service Unavailable", reason);
        }
    }
    void tick()
    {
        if (state == Waiting) tryFrame();
        else if (state != Closed && monotonicMs() >= deadline) finish();
    }
    void readRequest()
    {
        if (state != Reading) return;
        char buffer[1024];
        // At most one small header per event; no unbounded reads on the GUI thread.
        for (int i = 0; i < 5; ++i) {
            const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
            if (n == 0) { finish(); return; }
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) finish();
                return;
            }
            request.append(buffer, static_cast<int>(n));
            if (request.size() > MaxHeaderBytes) {
                jsonError(431, "Request Header Fields Too Large", "header_too_large"); return;
            }
            const int end = request.indexOf("\r\n\r\n");
            if (end < 0) continue;
            const QList<QByteArray> first = request.left(request.indexOf("\r\n")).split(' ');
            if (first.size() != 3 || (first[2] != "HTTP/1.0" && first[2] != "HTTP/1.1")) {
                jsonError(400, "Bad Request", "invalid_request_line"); return;
            }
            if (first[0] != "GET") { jsonError(405, "Method Not Allowed", "get_only"); return; }
            if (first[1] == "/v1/status.json") {
                reply(200, "OK", "application/json", QJsonDocument(server.status()).toJson(QJsonDocument::Compact));
                return;
            }
            if (first[1] != "/v1/frame.jpg" && first[1] != "/v1/preview.jpg") { jsonError(404, "Not Found", "unknown_path"); return; }
            readNotifier->setEnabled(false);
            request.clear();
            state = Waiting;
            deadline = monotonicMs() + FirstFrameWaitMs;
            isPreview = first[1] == "/v1/preview.jpg";
            if (!isPreview) server.requestFrame(); // preview HTTP never schedules an encode
            tryFrame();
            return;
        }
    }
    void writeResponse()
    {
        if (state != Sending) return;
        // Bound each writable event to 64 KiB; slow peers never spin or block.
        qint64 budget = 64 * 1024;
        while (budget > 0) {
            const bool sendingHeader = headerOffset < header.size();
            const QByteArray &bytes = sendingHeader ? header : body;
            qint64 &offset = sendingHeader ? headerOffset : bodyOffset;
            const qint64 available = bytes.size() - offset;
            if (!available) { finish(); return; }
            const ssize_t n = ::send(fd, bytes.constData() + offset,
                                     static_cast<size_t>(qMin(available, budget)), MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) return;
                if (errno != EAGAIN && errno != EWOULDBLOCK) finish();
                return;
            }
            if (n == 0) return;
            offset += n;
            budget -= n;
            if (headerOffset == header.size() && bodyOffset == body.size()) { finish(); return; }
        }
    }
};

LatestFrameServer::Impl::~Impl()
{
    delete acceptNotifier;
    if (listener >= 0) ::close(listener);
    const auto clients = peers;
    for (Peer *peer : clients) delete peer;
    {
        QMutexLocker lock(&s.mutex);
        s.quitting = true;
        s.wake.wakeAll();
    }
    {
        QMutexLocker lock(&preview.mutex);
        preview.active = false;
        if(preview.trackingPublisher)preview.trackingPublisher->invalidate("unavailable","camera_server_stopped");
        preview.quitting = true;
        preview.wake.wakeAll();
    }
    encoder.wait();
    previewEncoder.wait();
}

bool LatestFrameServer::Impl::start(const QString &address, quint16 port, int fps, int quality, QString &error, int previewFps, int previewWidth, int previewQuality,
                                   bool trackCenters,const QString& trackingOutputDir)
{
    if (listener >= 0) { error = "already_listening"; return false; }
    if(trackCenters) {
        try {
            preview.trackingPublisher.reset(new TrackingPublisher(trackingOutputDir));
            if(!preview.trackingPublisher->invalidate("unavailable","camera_not_open",&error))return false;
            preview.trackCenters=true;
            preview.trackingReferencePath=QDir::cleanPath(trackingOutputDir)+"/reference.json";
        } catch(const std::exception& exception){error=QString::fromUtf8(exception.what());return false;}
    }
    sockaddr_in endpoint;
    std::memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (fps < 1 || fps > 5 || quality < 1 || quality > 100 || port == 0
            || previewFps < 1 || previewFps > 30 || previewWidth < 160 || previewWidth > 1280
            || previewQuality < 1 || previewQuality > 100
            || ::inet_pton(AF_INET, address.toLatin1().constData(), &endpoint.sin_addr) != 1) {
        error = "invalid_stream_options"; return false;
    }
    listener = ::socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    if (listener >= 0) ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (listener < 0 || !nonblocking(listener)
            || ::bind(listener, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) != 0
            || ::listen(listener, MaxClients) != 0) {
        error = QStringLiteral("HTTP listen failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        if (listener >= 0) ::close(listener);
        listener = -1;
        return false;
    }
    s.periodMs = (1000 + fps - 1) / fps;
    s.quality = quality;
    preview.periodMs = (1000 + previewFps - 1) / previewFps;
    preview.quality = previewQuality;
    preview.resizeEdge = previewWidth;
    preview.demandDriven = false;
    bindAddress = address;
    bindPort = port;
    acceptNotifier = new QSocketNotifier(listener, QSocketNotifier::Read, q);
    QObject::connect(acceptNotifier, &QSocketNotifier::activated, q, [this](int) { acceptClients(); });
    encoder.start();
    previewEncoder.start();
    return true;
}

void LatestFrameServer::Impl::acceptClients()
{
    // An accept flood also gets a bounded amount of GUI-thread work per event.
    for (int i = 0; i < MaxClients + 1; ++i) {
        const int fd = ::accept(listener, nullptr, nullptr);
        if (fd < 0) return;
        if (peers.size() >= MaxClients || !nonblocking(fd)) { ::close(fd); continue; }
        int sendBuffer = 64 * 1024;
        ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof(sendBuffer));
        new Peer(*this, fd);
    }
}

void LatestFrameServer::Impl::requestFrame()
{
    QMutexLocker lock(&s.mutex);
    s.demandUntilMs = monotonicMs() + DemandLeaseMs;
    s.wake.wakeAll();
}

bool LatestFrameServer::Impl::frameSnapshot(EncodedFrame &frame, QString &reason, bool isPreview)
{
    SharedState &s = isPreview ? preview : this->s;
    QMutexLocker lock(&s.mutex);
    if (!s.active) { reason = s.reason; return false; }
    if (!s.encodeError.isEmpty()) { reason = "jpeg_encode_failed"; return false; }
    if (s.latest.jpeg.isEmpty() || s.latest.session != s.session) { reason = "frame_not_ready"; return false; }
    if (monotonicMs() - s.latest.receivedMs > StaleMs) { reason = "frame_stale"; return false; }
    frame = s.latest;
    return true;
}

QJsonObject LatestFrameServer::Impl::status()
{
    QMutexLocker lock(&s.mutex);
    const qint64 now = monotonicMs();
    QJsonObject value;
    value["protocol"] = 1;
    value["camera_active"] = s.active;
    value["session"] = s.session;
    value["source"] = s.source;
    value["frame_sequence"] = QString::number(s.latest.sequence);
    value["frame_age_ms"] = s.latest.jpeg.isEmpty() ? -1 : now - s.latest.receivedMs;
    value["frame_ready"] = s.active && s.encodeError.isEmpty() && !s.latest.jpeg.isEmpty()
            && now - s.latest.receivedMs <= StaleMs;
    value["reason"] = s.reason;
    value["encode_error"] = s.encodeError;
    value["jpeg_bytes"] = s.latest.jpeg.size();
    value["width"] = s.latest.width;
    value["height"] = s.latest.height;
    value["quality"] = s.quality;
    value["encode_period_ms"] = s.periodMs;
    value["demand_active"] = now < s.demandUntilMs;
    value["pending_frames"] = s.hasPending ? 1 : 0;
    value["encoding"] = s.encoding;
    value["encode_count"] = QString::number(s.encodeCount);
    value["replaced_count"] = QString::number(s.replacedCount);
    value["offered_count"] = QString::number(s.offeredCount);
    value["last_encode_ms"] = s.lastEncodeMs;
    value["connected_clients"] = peers.size();
    value["bind_address"] = bindAddress;
    value["port"] = bindPort;
    {
        QMutexLocker previewLock(&preview.mutex);
        QJsonObject pv;
        pv["path"] = "/v1/preview.jpg";
        pv["session"] = preview.session;
        pv["frame_sequence"] = QString::number(preview.latest.sequence);
        pv["frame_age_ms"] = preview.latest.jpeg.isEmpty() ? -1 : now - preview.latest.receivedMs;
        pv["frame_ready"] = preview.active && preview.encodeError.isEmpty() && !preview.latest.jpeg.isEmpty() && now - preview.latest.receivedMs <= StaleMs;
        pv["width"] = preview.latest.width; pv["height"] = preview.latest.height;
        pv["max_edge"] = preview.resizeEdge; pv["quality"] = preview.quality;
        pv["encode_period_ms"] = preview.periodMs; pv["jpeg_bytes"] = preview.latest.jpeg.size();
        pv["pending_frames"] = preview.hasPending ? 1 : 0; pv["encoding"] = preview.encoding;
        pv["encode_count"] = QString::number(preview.encodeCount); pv["replaced_count"] = QString::number(preview.replacedCount);
        pv["offered_count"] = QString::number(preview.offeredCount); pv["last_encode_ms"] = preview.lastEncodeMs;
        pv["encode_error"] = preview.encodeError; pv["demand_driven"] = false;
        pv["track_centers"] = preview.trackCenters;
        value["preview"] = pv;
    }
    return value;
}

LatestFrameServer::LatestFrameServer(QObject *parent) : QObject(parent), d(new Impl(this)) {}
LatestFrameServer::~LatestFrameServer() = default;

bool LatestFrameServer::start(const QString &address, quint16 port, int fps, int quality, QString &error, int previewFps, int previewWidth, int previewQuality,
                             bool trackCenters,const QString& trackingOutputDir)
{
    return d->start(address, port, fps, quality, error, previewFps, previewWidth, previewQuality,trackCenters,trackingOutputDir);
}

void LatestFrameServer::beginSession(bool simulation)
{
    const QString session = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (SharedState *state : {&d->s, &d->preview}) {
        QMutexLocker lock(&state->mutex);
        state->active = true; state->session = session;
        state->source = simulation ? "simulation" : "imx415";
        state->reason.clear(); state->encodeError.clear();
        state->pending = RawFrame(); state->latest = EncodedFrame();
        state->hasPending = false; state->nextEncodeMs = 0;
        if(state->trackingPublisher)state->trackingPublisher->invalidate("unavailable","waiting_for_new_session_frame");
        state->wake.wakeAll();
    }
}

void LatestFrameServer::stopSession(const QString &reason)
{
    for (SharedState *state : {&d->s, &d->preview}) {
        QMutexLocker lock(&state->mutex);
        state->active = false; state->reason = reason;
        state->pending = RawFrame(); state->latest = EncodedFrame();
        state->hasPending = false; state->demandUntilMs = 0;
        if(state->trackingPublisher)state->trackingPublisher->invalidate("unavailable",reason);
        state->wake.wakeAll();
    }
}

void LatestFrameServer::offerFrame(const QImage &frame, const QDateTime &utc, quint64 sequence)
{
    const qint64 now = monotonicMs();
    for (SharedState *state : {&d->s, &d->preview}) {
        QMutexLocker lock(&state->mutex);
        ++state->offeredCount;
        if (!state->active || (state->demandDriven && now >= state->demandUntilMs) || frame.isNull()) continue;
        if (state->hasPending) ++state->replacedCount;
        state->pending.image = frame; // Implicit sharing; never resize/encode in capture thread.
        state->pending.utc = utc; state->pending.sequence = sequence; state->pending.receivedMs = now;
        state->pending.session = state->session; state->pending.source = state->source;
        state->hasPending = true; state->wake.wakeAll();
    }
}
