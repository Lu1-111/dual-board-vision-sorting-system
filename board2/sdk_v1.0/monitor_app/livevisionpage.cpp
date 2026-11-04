#include "livevisionpage.h"
#include "camerapreviewclient.h"
#include <QThread>
#include <QMetaObject>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QProgressBar>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <ctime>

namespace {
const char kModelId[] = "effnetb0v3";
const char kValidationStatus[] = "board_regression_21_ok";
const char kModelSha256[] = "e6dbbc45c5a38117a8d9754e3b002032c937bbe312f0c50d0bc37ad7aaa4f016";
qint64 monotonicMs() {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return -1;
    return qint64(value.tv_sec) * 1000 + value.tv_nsec / 1000000;
}
QString displayClass(int index) {
    static const char *names[] = {"输送带空载", "物料在位", "手部侵入"};
    return index >= 0 && index < 3 ? QString::fromUtf8(names[index]) : QStringLiteral("待识别");
}
bool number(const QJsonValue &value, double *out, double lower = 0, double upper = 60000) {
    if (!value.isDouble()) return false;
    *out = value.toDouble();
    return std::isfinite(*out) && *out >= lower && *out <= upper;
}
QFrame *card(QVBoxLayout **layout) {
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    *layout = new QVBoxLayout(frame);
    (*layout)->setContentsMargins(14, 12, 14, 12);
    (*layout)->setSpacing(10);
    return frame;
}
}

LiveVisionPage::LiveVisionPage(QWidget *parent, const QString &directory)
    : QWidget(parent),
      directory_(directory.isEmpty() ? qEnvironmentVariable("MONITOR_LIVE_DIR", "/tmp/monitor-live") : directory),
      status_(new QLabel), message_(new QLabel), preview_(new QLabel),
      prediction_(new QLabel), confidence_(new QLabel), state_(new QLabel),
      resultInfo_(new QLabel), performance_(new QLabel), scope_(new QLabel), timer_(new QTimer(this)) {
    directory_ = QDir(directory_).absolutePath();
    setObjectName(QStringLiteral("liveVisionPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);
    auto *heading = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("实时视频 · 独立预览 / 最近识别"));
    title->setObjectName(QStringLiteral("sectionTitle"));
    heading->addWidget(title);
    heading->addStretch();
    status_->setObjectName(QStringLiteral("liveStatus"));
    heading->addWidget(status_);
    root->addLayout(heading);
    auto *warning = new QLabel(QStringLiteral("21 张实板回归通过；现场精度待验收"));
    warning->setObjectName(QStringLiteral("liveValidationWarning"));
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("background:#493720;color:#ffda92;border:1px solid #80613b;border-radius:7px;padding:9px;font-size:14px;"));
    root->addWidget(warning);
    auto *body = new QHBoxLayout;
    body->setSpacing(12);
    QVBoxLayout *imageLayout = nullptr;
    auto *imageCard = card(&imageLayout);
    auto *imageTitle = new QLabel(QStringLiteral("实时视频 · 板 1 相机"));
    imageTitle->setObjectName(QStringLiteral("cardTitle"));
    imageLayout->addWidget(imageTitle);
    preview_->setObjectName(QStringLiteral("livePreview"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(300, 190);
    preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    preview_->setStyleSheet(QStringLiteral("background:#0c1421;color:#90a5be;border-radius:8px;font-size:16px;"));
    imageLayout->addWidget(preview_, 1);
    body->addWidget(imageCard, 5);
    QVBoxLayout *resultLayout = nullptr;
    auto *resultCard = card(&resultLayout);
    resultCard->setMinimumWidth(245);
    scope_->setObjectName(QStringLiteral("livePredictionScope"));
    scope_->setStyleSheet(QStringLiteral("color:#a6b9d3;font-size:13px;"));
    resultLayout->addWidget(scope_);
    prediction_->setObjectName(QStringLiteral("livePrediction"));
    prediction_->setWordWrap(true);
    prediction_->setStyleSheet(QStringLiteral("font-size:25px;font-weight:700;"));
    resultLayout->addWidget(prediction_);
    confidence_->setObjectName(QStringLiteral("liveConfidence"));
    confidence_->setStyleSheet(QStringLiteral("font-size:18px;"));
    resultLayout->addWidget(confidence_);
    state_->setObjectName(QStringLiteral("liveInferenceState"));
    resultLayout->addWidget(state_);
    resultInfo_->setObjectName(QStringLiteral("liveResultInfo"));
    resultInfo_->setWordWrap(true);
    resultInfo_->setStyleSheet(QStringLiteral("color:#a6b9d3;font-size:12px;"));
    resultLayout->addWidget(resultInfo_);
    resultLayout->addSpacing(4);
    for (int i = 0; i < 3; ++i) {
        auto *label = new QLabel(QStringLiteral("模型概率 · %1").arg(displayClass(i)));
        label->setStyleSheet(QStringLiteral("font-size:13px;color:#c2d1e2;"));
        resultLayout->addWidget(label);
        probabilities_[i] = new QProgressBar;
        probabilities_[i]->setObjectName(QStringLiteral("liveProbability%1").arg(i));
        probabilities_[i]->setRange(0, 1000);
        probabilities_[i]->setMinimumHeight(21);
        probabilities_[i]->setAlignment(Qt::AlignCenter);
        probabilities_[i]->setStyleSheet(QStringLiteral("QProgressBar{background:#111f32;border:1px solid #3a506d;border-radius:5px;color:#edf3fc;font-size:12px;}QProgressBar::chunk{background:#267a75;border-radius:4px;}"));
        resultLayout->addWidget(probabilities_[i]);
    }
    resultLayout->addStretch();
    performance_->setObjectName(QStringLiteral("livePerformance"));
    performance_->setWordWrap(true);
    performance_->setStyleSheet(QStringLiteral("color:#a6b9d3;font-size:12px;"));
    resultLayout->addWidget(performance_);
    body->addWidget(resultCard, 3);
    root->addLayout(body, 1);
    message_->setObjectName(QStringLiteral("liveMessage"));
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    message_->setStyleSheet(QStringLiteral("color:#a6b9d3;font-size:12px;"));
    root->addWidget(message_);
    connect(timer_, &QTimer::timeout, this, &LiveVisionPage::refresh);
    timer_->start(200);
    clearPreview(QStringLiteral("等待板 1 实时视频"));
    if (qEnvironmentVariable("MONITOR_PREVIEW_DISABLED") != QStringLiteral("1")) {
        bool ok = false;
        int fps = qEnvironmentVariable("MONITOR_PREVIEW_FPS", "15").toInt(&ok);
        if (!ok || fps < 1 || fps > 30) fps = 15;
        previewThread_ = new QThread(this);
        previewClient_ = new CameraPreviewClient(qEnvironmentVariable("MONITOR_PREVIEW_URL", "http://172.22.203.1:47003/v1/preview.jpg"), fps);
        previewClient_->moveToThread(previewThread_);
        connect(previewThread_, &QThread::started, previewClient_, &CameraPreviewClient::start);
        connect(previewThread_, &QThread::finished, previewClient_, &QObject::deleteLater);
        connect(previewClient_, &CameraPreviewClient::frameReady, this, &LiveVisionPage::receivePreview, Qt::QueuedConnection);
        connect(previewClient_, &CameraPreviewClient::stateChanged, this, &LiveVisionPage::receivePreviewState, Qt::QueuedConnection);
        previewThread_->start();
    }
    refresh();
}

LiveVisionPage::~LiveVisionPage() {
    if (previewThread_ && previewThread_->isRunning()) {
        QMetaObject::invokeMethod(previewClient_, "stop", Qt::BlockingQueuedConnection);
        previewThread_->quit();
        previewThread_->wait();
    }
}

void LiveVisionPage::clearPreview(const QString &message) {
    frame_ = QPixmap();
    preview_->clear(); preview_->setText(message);
}

void LiveVisionPage::expirePreview() {
    const qint64 now = monotonicMs();
    if (!frame_.isNull() && (now < 0 || previewSourceAgeMs_ + qMax(0.0, double(now)-previewPublishedMs_) > 2000 || now-previewFirstSeenMs_ > 2000))
        clearPreview(QStringLiteral("实时视频已过期，等待新帧"));
}

void LiveVisionPage::receivePreviewState(const QString &status, const QString &message) {
    if (status != QStringLiteral("ok")) clearPreview(message.left(160));
}

void LiveVisionPage::receivePreview(const QImage &image, const QJsonObject &meta) {
    // ACK on every return path. The producer permits only one queued image.
    struct Ack { CameraPreviewClient *client; ~Ack() { if(client) QMetaObject::invokeMethod(client,"acknowledgeFrame",Qt::QueuedConnection); } } ack{previewClient_};
    const QString session = meta.value("source_session").toString();
    const QString sequence = meta.value("frame_sequence").toString();
    const QString key = session + QChar('|') + sequence;
    double published=0, age=0, width=0, height=0, fps=0;
    const qint64 now = monotonicMs();
    if (now < 0 || image.isNull() || image.width()>960 || image.height()>540 ||
        !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,128}$")).match(session).hasMatch() ||
        !QRegularExpression(QStringLiteral("^[0-9]{1,64}$")).match(sequence).hasMatch() ||
        !number(meta.value("published_monotonic_ms"),&published,0,9e15) || published>double(now)+250 ||
        !number(meta.value("source_frame_age_ms"),&age,0,2000) || age+qMax(0.0,double(now)-published)>2000 ||
        !number(meta.value("width"),&width,1,4096) || !number(meta.value("height"),&height,1,4096) ||
        width!=std::floor(width) || height!=std::floor(height) || width*height>8000000 ||
        !number(meta.value("fps"),&fps,0,120)) {
        clearPreview(QStringLiteral("实时视频数据无效")); return;
    }
    if (key == previewKey_) { expirePreview(); return; }
    const bool changedSession = !previewSession_.isEmpty() && previewSession_ != session;
    previewSession_=session;
    if (changedSession)
        unavailable(QStringLiteral("等待识别"),QStringLiteral("上次相机会话结果，等待新识别。"));
    previewKey_=key; previewFirstSeenMs_=now;
    previewPublishedMs_=published; previewSourceAgeMs_=age;
    frame_=QPixmap::fromImage(image); updatePreview();
}

void LiveVisionPage::unavailable(const QString &status, const QString &message) {
    if (!lastResult_.isEmpty()) { renderLastResult(status, message, true); return; }
    status_->setText(QStringLiteral("等待首次识别"));
    status_->setStyleSheet(QStringLiteral("color:#f3c479;font-size:14px;"));
    message_->setText(message.left(160));
    prediction_->setText(QStringLiteral("等待首次识别"));
    scope_->setText(QStringLiteral("最近识别 · 全图传送带状态"));
    confidence_->setText(QStringLiteral("模型最高概率：待识别"));
    state_->setText(QStringLiteral("状态：等待首次识别"));
    state_->setStyleSheet(QStringLiteral("color:#a6b9d3;"));
    performance_->setText(QStringLiteral("等待首次识别耗时"));
    resultInfo_->setText(QStringLiteral("等待首次识别"));
    for (auto *bar : probabilities_) { bar->setValue(0); bar->setFormat(QStringLiteral("待识别")); }
}

void LiveVisionPage::renderLastResult(const QString &updateStatus, const QString &message, bool retained) {
    const auto values = lastResult_.value("probabilities").toArray();
    const int maximum = lastResult_.value("class_index").toInt();
    const double confidence = lastResult_.value("confidence").toDouble();
    const bool uncertain = lastResult_.value("state").toString() == QStringLiteral("uncertain");
    const QString label = uncertain ? QStringLiteral("不确定") : displayClass(maximum);
    const qint64 now = monotonicMs();
    const double age = lastResult_.value("source_frame_age_ms").toDouble() +
        qMax(0.0, double(now)-lastResult_.value("published_monotonic_ms").toDouble());
    const bool previousSession = !previewSession_.isEmpty() && lastResult_.value("source_session").toString()!=previewSession_;
    const bool historical = retained || age>5000 || previousSession;
    status_->setText(historical ? QStringLiteral("保留上次结果 · %1").arg(updateStatus) : QStringLiteral("最近识别有效"));
    status_->setStyleSheet(historical ? QStringLiteral("color:#f3c479;font-size:14px;") : QStringLiteral("color:#7dd6c3;font-size:14px;"));
    prediction_->setText(QStringLiteral("预测：%1").arg(label));
    scope_->setText(QStringLiteral("最近识别 · 全图传送带状态"));
    confidence_->setText(QStringLiteral("模型最高概率：%1%").arg(confidence*100,0,'f',1));
    state_->setText(QStringLiteral("状态：%1").arg(label));
    state_->setStyleSheet(historical || uncertain ? QStringLiteral("color:#f3c479;font-size:15px;") : QStringLiteral("color:#7dd6c3;font-size:15px;"));
    for (int i=0;i<3;++i) {
        const double probability=values.at(i).toDouble();
        probabilities_[i]->setValue(qRound(probability*1000));
        probabilities_[i]->setFormat(QStringLiteral("%1%").arg(probability*100,0,'f',1));
    }
    const QString context = previousSession ? QStringLiteral("上次相机会话结果，等待新识别")
        : historical ? QStringLiteral("历史识别，保留上次结果，等待更新")
        : frame_.isNull() ? QStringLiteral("历史识别（视频不可用）") : QStringLiteral("最近识别");
    resultInfo_->setText(context);
    const auto times=lastResult_.value("timing_ms").toObject();
    performance_->setText(QStringLiteral("总耗时 %1 ms · CPU 前段 %2 ms · 尾段 %3 ms（NPU + CpuRef）\n预处理 %4 ms · 识别 %5 fps")
        .arg(times.value("total").toDouble(),0,'f',1).arg(times.value("cpu").toDouble(),0,'f',1)
        .arg(times.value("npu").toDouble(),0,'f',1).arg(times.value("preprocess").toDouble(),0,'f',1)
        .arg(lastResult_.value("fps").toDouble(),0,'f',2));
    message_->setText(historical ? message.left(100) : QString());
}

void LiveVisionPage::refresh() {
    expirePreview();
    QFile file(directory_ + QStringLiteral("/latest.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        unavailable(QStringLiteral("等待连接"), QStringLiteral("等待后台接收板 1 相机画面与模型结果。"));
        return;
    }
    if (file.size() <= 0 || file.size() > 65536) {
        unavailable(QStringLiteral("数据不可用"), QStringLiteral("实时结果格式不完整。")); return;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        unavailable(QStringLiteral("数据不可用"), QStringLiteral("等待完整的实时结果。")); return;
    }
    const QJsonObject obj = doc.object();
    if (obj.value("schema").toDouble(-1) != 1) {
        unavailable(QStringLiteral("数据不可用"), QStringLiteral("实时结果版本不匹配。")); return;
    }
    const QString status = obj.value("status").toString();
    if (status != QStringLiteral("ok")) {
        const QString text = status == "connecting" ? QStringLiteral("等待连接")
            : status == "source_stale" ? QStringLiteral("识别已过期")
            : status == "inference_error" ? QStringLiteral("推理不可用") : QStringLiteral("数据不可用");
        unavailable(text, obj.value("message").toString(QStringLiteral("暂无有效实时数据。"))); return;
    }
    double published = 0;
    const qint64 now = monotonicMs();
    if (now < 0 || !number(obj.value("published_monotonic_ms"), &published, 0, 9e15) ||
        published > double(now) + 250) {
        unavailable(QStringLiteral("识别已过期"), QStringLiteral("识别结果时间无效，保留上次结果，等待更新。")); return;
    }
    double sourceAge = 0;
    // Worker age includes capture, HTTP and inference. Add time since publication;
    // a fresh JSON timestamp must never make an old camera exposure look current.
    // A completed slow inference is useful history, not a current-video prediction.
    // Rendering still marks every capture older than 5 seconds as historical.
    if (!number(obj.value("source_frame_age_ms"), &sourceAge, 0, 15000) ||
        sourceAge + qMax(0.0, double(now) - published) > 15000) {
        unavailable(QStringLiteral("识别已过期"), QStringLiteral("识别结果已过期，等待更新。")); return;
    }
    for (const char *field : {"roi", "model_roi", "color_assist", "fusion"}) {
        if (obj.contains(field)) {
            unavailable(QStringLiteral("数据不可用"), QStringLiteral("全图 EfficientNet 不接受旧检测范围或颜色融合结果。")); return;
        }
    }
    const QString session = obj.value("source_session").toString();
    const QString sequence = obj.value("frame_sequence").toString();
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,128}$")).match(session).hasMatch() ||
        !QRegularExpression(QStringLiteral("^[0-9]{1,64}$")).match(sequence).hasMatch()) {
        unavailable(QStringLiteral("数据不可用"), QStringLiteral("画面编号缺失。")); return;
    }
    if (!previewSession_.isEmpty() && session != previewSession_) {
        unavailable(QStringLiteral("等待识别"),QStringLiteral("识别结果来自旧相机会话，等待当前会话结果。")); return;
    }
    const QStringList expected{QStringLiteral("empty"), QStringLiteral("object_present"), QStringLiteral("hand_present")};
    const auto names = obj.value("class_names").toArray();
    const auto values = obj.value("probabilities").toArray();
    bool valid = names.size() == 3 && values.size() == 3;
    double probability[3]{}, sum = 0;
    int maximum = 0;
    for (int i = 0; valid && i < 3; ++i) {
        valid = names.at(i).toString() == expected.at(i) && number(values.at(i), &probability[i], 0, 1);
        sum += probability[i];
        if (probability[i] > probability[maximum]) maximum = i;
    }
    double confidence = 0, threshold = 0, fps = 0;
    const auto times = obj.value("timing_ms").toObject();
    double total = 0, preprocess = 0, cpu = 0, npu = 0, head = 0;
    valid = valid && obj.value("valid").isBool() && obj.value("valid").toBool() && std::abs(sum - 1.) < .001 &&
        obj.value("class_index").toDouble(-1) == maximum &&
        number(obj.value("confidence"), &confidence, 0, 1) && std::abs(confidence - probability[maximum]) < .00001 &&
        number(obj.value("threshold"), &threshold, 0, 1) && std::abs(threshold - .8) < .000001 &&
        number(obj.value("fps"), &fps, 0, 120) && number(times.value("total"), &total) &&
        number(times.value("preprocess"), &preprocess) && number(times.value("cpu"), &cpu) && number(times.value("npu"), &npu) && number(times.value("head"), &head) &&
        obj.value("model_id").toString() == QLatin1String(kModelId) &&
        obj.value("model_sha256").toString() == QLatin1String(kModelSha256) &&
        obj.value("validation_status").toString() == QLatin1String(kValidationStatus);
    const bool uncertain = confidence < threshold;
    valid = valid && obj.value("state").toString() == (uncertain ? QStringLiteral("uncertain") : expected.at(maximum));
    if (!valid) {
        unavailable(QStringLiteral("数据不可用"), QStringLiteral("模型结果不完整或与该帧状态不一致。")); return;
    }
    const QString path = obj.value("frame_path").toString();
    const QFileInfo info(path);
    const QString canonicalDir = QFileInfo(directory_).canonicalFilePath();
    if (!info.isAbsolute() || !info.isFile() || info.isSymLink() || canonicalDir.isEmpty() ||
        info.canonicalPath() != canonicalDir ||
        info.fileName() != QStringLiteral("frame_%1_%2.jpg").arg(session, sequence) ||
        info.size() <= 0 || info.size() > 8*1024*1024) {
        unavailable(QStringLiteral("识别图不可用"), QStringLiteral("未找到与结果配对的完整相机画面。")); return;
    }
    QFile matchedImage(path);
    if (!matchedImage.open(QIODevice::ReadOnly) || !matchedImage.seek(info.size()-2) || matchedImage.read(2) != QByteArray::fromHex("ffd9")) {
        unavailable(QStringLiteral("数据不可用"),QStringLiteral("识别对应图像未完整接收。")); return;
    }
    // Keep validating the inference image association, but never use it as video.
    QImageReader reader(path, "jpg");
    const QSize size = reader.size();
    if (!size.isValid() || size.width()>4096 || size.height()>4096 || qint64(size.width())*size.height()>8000000) {
        unavailable(QStringLiteral("数据不可用"),QStringLiteral("识别对应图像无效。")); return;
    }
    // Commit a complete validated snapshot before rendering. Invalid/new partial
    // payloads never mutate the displayed class, confidence or probabilities.
    lastResult_ = obj;
    renderLastResult(QStringLiteral("更新完成"), QString(), false);
}

void LiveVisionPage::updatePreview() {
    if (frame_.isNull()) return;
    QPixmap shown = frame_.scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    preview_->setPixmap(shown);
}

void LiveVisionPage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updatePreview();
}
