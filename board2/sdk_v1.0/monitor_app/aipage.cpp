#include "aipage.h"
#include "evidence_store.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QTimer>
#include <QSet>

namespace {

QString colorText(const AlarmEvent &event)
{
    return event.color == MaterialColor::None
        ? QStringLiteral("—")
        : materialColorName(event.color);
}

void appendList(QString *html, const char *tag, const QStringList &items)
{
    for (const QString &item : items)
        *html += QStringLiteral("<%1>%2</%1>").arg(QString::fromLatin1(tag), item);
}

} // namespace

AiPage::AiPage(QWidget *parent)
    : QWidget(parent),
      modeBadge_(new QLabel(QStringLiteral("电脑桥接：等待连接"))),
      alarmList_(new QListWidget),
      generateButton_(new QPushButton(QStringLiteral("生成分析（离线模拟）"))),
      result_(new QTextBrowser)
{
    setObjectName(QStringLiteral("aiPage"));
    result_->document()->setDefaultStyleSheet(
        QStringLiteral("h3{color:#7dd6c3;} h4{color:#a6b9d3;}"));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    QHBoxLayout *bar = new QHBoxLayout;
    bar->setSpacing(12);
    modeBadge_->setObjectName(QStringLiteral("aiModeBadge"));
    modeBadge_->setStyleSheet(
        QStringLiteral("background:#4a3a1d; color:#e8a33d; border:1px solid #8a6a2d;"
                       "border-radius:8px; padding:6px 12px; font-weight:700;"));
    bar->addWidget(modeBadge_);
    bar->addStretch();
    QLabel *note = new QLabel(QStringLiteral("仅展示告警关联图片"));
    note->setObjectName(QStringLiteral("muted"));
    bar->addWidget(note);
    root->addLayout(bar);

    QHBoxLayout *body = new QHBoxLayout;
    body->setSpacing(12);
    root->addLayout(body, 1);

    QFrame *listCard = new QFrame;
    listCard->setObjectName(QStringLiteral("card"));
    QVBoxLayout *listBody = new QVBoxLayout(listCard);
    listBody->setContentsMargins(16, 12, 16, 12);
    listBody->setSpacing(8);
    QLabel *listTitle = new QLabel(QStringLiteral("告警事件"));
    listTitle->setObjectName(QStringLiteral("cardTitle"));
    listBody->addWidget(listTitle);
    alarmList_->setObjectName(QStringLiteral("aiAlarmList"));
    listBody->addWidget(alarmList_, 1);
    generateButton_->setObjectName(QStringLiteral("aiGenerate"));
    listBody->addWidget(generateButton_);
    externalButton_=new QPushButton(QStringLiteral("发送图片 + 文字分析"));
    externalButton_->setObjectName(QStringLiteral("aiExternal"));
    listBody->addWidget(externalButton_);
    body->addWidget(listCard, 2);

    QFrame *resultCard = new QFrame;
    resultCard->setObjectName(QStringLiteral("card"));
    QVBoxLayout *resultBody = new QVBoxLayout(resultCard);
    resultBody->setContentsMargins(16, 12, 16, 12);
    resultBody->setSpacing(8);
    QLabel *resultTitle = new QLabel(QStringLiteral("分析结果"));
    resultTitle->setObjectName(QStringLiteral("cardTitle"));
    resultBody->addWidget(resultTitle);
    image_=new QLabel(QStringLiteral("当前告警尚未收到图片"));
    image_->setObjectName(QStringLiteral("aiEvidenceImage"));
    image_->setAlignment(Qt::AlignCenter);
    image_->setFixedHeight(166);
    image_->setMinimumWidth(260);
    image_->setStyleSheet(QStringLiteral("background:#091321;border:1px solid #34475d;border-radius:6px;"));
    resultBody->addWidget(image_);
    imageNote_=new QLabel;
    imageNote_->setObjectName(QStringLiteral("aiEvidenceNote"));
    imageNote_->setWordWrap(true);
    resultBody->addWidget(imageNote_);
    result_->setObjectName(QStringLiteral("aiResult"));
    result_->setOpenExternalLinks(false);
    resultBody->addWidget(result_, 1);
    body->addWidget(resultCard, 3);

    connect(generateButton_, &QPushButton::clicked, this,
            [this]() { generateAnalysis(); });
    result_->setHtml(QStringLiteral("<p>选择左侧告警事件后点击\"生成分析\"。</p>"));
    generateButton_->setEnabled(false);
    connect(externalButton_,&QPushButton::clicked,this,[this](){requestAnalysis();});
    connect(alarmList_,&QListWidget::currentRowChanged,this,[this](int){
        shownImage_.clear(); shownResult_.clear(); result_->setPlainText(QStringLiteral("可使用离线规则，或将此事件的图片和文字发送到电脑分析。")); refreshEvidence();
    });
    QTimer *timer=new QTimer(this);
    connect(timer,&QTimer::timeout,this,[this](){refreshEvidence();}); timer->start(1000);
    publishEvents(); refreshEvidence();
}

void AiPage::alarmRaised(const AlarmEvent &event)
{
    events_.prepend(event);
    while (events_.size() > 100) events_.removeLast();
    publishEvents();
    refreshAlarmList();
}

void AiPage::alarmCleared(const AlarmEvent &event)
{
    for (AlarmEvent &stored : events_) {
        if (stored.eventId == event.eventId) {
            stored.durationS = event.durationS;
            stored.active = false;
            stored.recoveredAt = event.recoveredAt;
            stored.recoveryUnknown = event.recoveryUnknown;
            break;
        }
    }
    publishEvents();
    refreshAlarmList();
}

void AiPage::refreshAlarmList()
{
    const QVariant selected = alarmList_->currentItem()
        ? alarmList_->currentItem()->data(Qt::UserRole) : QVariant();
    alarmList_->clear();
    for (const AlarmEvent &event : events_) {
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("#%1  %2\n%3 · %4")
                .arg(event.eventId)
                .arg(alarmTypeName(event.type))
                .arg(colorText(event))
                .arg(event.active ? QStringLiteral("持续中") : event.recoveryUnknown ? QStringLiteral("结束时间未知") : QStringLiteral("已恢复")));
        if (event.active)
            item->setForeground(QColor(0xe0, 0x52, 0x52));
        alarmList_->addItem(item);
        item->setData(Qt::UserRole, event.eventId);
        if (selected.isValid() && selected.toUInt() == event.eventId)
            alarmList_->setCurrentItem(item);
    }
    if (!alarmList_->currentItem() && alarmList_->count()) alarmList_->setCurrentRow(0);
    generateButton_->setEnabled(!events_.isEmpty());
}

void AiPage::resetSession()
{
    events_.clear(); refreshAlarmList();
    requests_.clear(); publishEvents(); refreshEvidence();
    result_->setHtml(QStringLiteral("<p>发送端会话已更新，等待新的告警事件。</p>"));
}

QJsonObject AiPage::selectedEvent() const {
    int row=alarmList_->currentRow(); if(row<0 || row>=events_.size()) return {};
    const AlarmEvent &e=events_[row];
    return {{"session_id",double(e.sessionId)},{"event_id",double(e.eventId)},
        {"alarm_type",int(e.type)},{"alarm_name",alarmTypeName(e.type)},
        {"color",materialColorName(e.color)},{"duration_s",int(e.durationS)},
        {"active",e.active},{"simulated",e.simulated},{"detected_at",e.detectedAt.toString(Qt::ISODate)}};
}

void AiPage::publishEvents() {
    QJsonArray list;
    for(const AlarmEvent &e:events_) list.append(QJsonObject{{"session_id",double(e.sessionId)},{"event_id",double(e.eventId)},
        {"alarm_type",int(e.type)},{"alarm_name",alarmTypeName(e.type)},{"color",materialColorName(e.color)},
        {"duration_s",int(e.durationS)},{"active",e.active},{"simulated",e.simulated},{"detected_at",e.detectedAt.toString(Qt::ISODate)}});
    Evidence::write("index.json",{{"events",list}});
    // Bound disk use while retaining evidence still referenced by pending requests.
    QSet<QString> keep;
    for(auto v:list) keep.insert(Evidence::key(v.toObject()));
    for(QString f:QDir(Evidence::root()).entryList({"*.job.json"},QDir::Files)) {
        auto j=Evidence::read(f); if(!Evidence::expired(j)) keep.insert(Evidence::key(j));
    }
    for(QString f:QDir(Evidence::root()).entryList({"*.image.json"},QDir::Files)) {
        QString k=f.left(f.size()-11);
        if(!keep.contains(k)) { QFile::remove(Evidence::path(f)); QFile::remove(Evidence::path(k+".jpg")); }
    }
}

void AiPage::refreshEvidence() {
    QJsonObject status=Evidence::read("pc-status.json");
    qint64 age=QDateTime::currentSecsSinceEpoch()-qint64(status["last_seen"].toDouble());
    bool online=!status.isEmpty() && age>=0 && age<10;
    modeBadge_->setText(online?QStringLiteral("电脑已连接 · %1").arg(status["provider"].toString()):QStringLiteral("电脑桥接：未连接"));
    QJsonObject ev=selectedEvent(); QString k=Evidence::key(ev);
    QJsonObject meta=Evidence::read(k+".image.json"); QString digest=meta["sha256"].toString();
    bool have=!k.isEmpty() && !digest.isEmpty();
    if(!have) { image_->setPixmap(QPixmap()); image_->setText(QStringLiteral("当前告警尚未收到图片")); shownImage_.clear(); }
    else if(shownImage_!=k+digest) {
        QFile f(Evidence::path(k+".jpg")); QByteArray b; if(f.open(QIODevice::ReadOnly)) b=f.readAll();
        QPixmap pix; have=Evidence::sha(b)==digest && pix.loadFromData(b,"JPEG");
        if(have) { image_->setPixmap(pix.scaled(370,166,Qt::KeepAspectRatio,Qt::SmoothTransformation)); shownImage_=k+digest; }
        else { image_->setPixmap(QPixmap()); image_->setText(QStringLiteral("图片校验或解码失败")); }
    }
    imageNote_->setText(have?QStringLiteral("事件 #%1 · %2 · %3 KB").arg(ev["event_id"].toInt()).arg(meta["source"].toString()=="test"?QStringLiteral("联调测试图片，非真实异常判定"):QStringLiteral("异常抓拍")).arg(meta["bytes"].toInt()/1024):QStringLiteral("正常画面不在此显示；等待对应告警图片。"));
    QString id=requests_.value(k); QJsonObject j=Evidence::job(id), r=Evidence::read(id+".result.json");
    bool pending=!j.isEmpty() && r.isEmpty() && !Evidence::expired(j);
    externalButton_->setEnabled(have && !pending);
    externalButton_->setText(pending?QStringLiteral("等待电脑分析…"):QStringLiteral("发送图片 + 文字分析"));
    if(!r.isEmpty() && r["image_sha256"]==digest && Evidence::key(r)==k && shownResult_!=id) {
        QString title=r["status"].toString()=="mock"?QStringLiteral("【离线演示 · 未调用外部 AI】"):r["status"].toString()=="error"?QStringLiteral("【分析未完成】"):QStringLiteral("【外部 AI 返回 · 请人工核实】");
        result_->setPlainText(title+"\n"+r["analysis"].toString()); shownResult_=id;
    } else if(!j.isEmpty() && r.isEmpty() && Evidence::expired(j) && shownResult_!=id+"expired") {
        result_->setPlainText(QStringLiteral("等待超过 5 分钟。请检查电脑桥接程序，然后重新发送。")); shownResult_=id+"expired";
    }
}

void AiPage::requestAnalysis() {
    if(QDateTime::currentDateTime().date().year()<2025) {
        result_->setPlainText(QStringLiteral("系统时钟未校准，请先启动电脑桥接程序自动校时，再发送图文分析请求。")); return;
    }
    QJsonObject ev=selectedEvent(); QString id=Evidence::enqueue(ev);
    if(id.isEmpty()) { result_->setPlainText(QStringLiteral("提交失败：图片未就绪、队列已满或存储不可写。")); return; }
    requests_[Evidence::key(ev)]=id; shownResult_.clear();
    result_->setPlainText(QStringLiteral("已排队：将当前告警文字和对应图片交给电脑。请启动电脑桥接程序；尚未调用外部服务。")); refreshEvidence();
}

void AiPage::generateAnalysis()
{
    if (events_.isEmpty()) {
        result_->setHtml(QStringLiteral("<p>暂无告警事件，等待数据…</p>"));
        return;
    }
    const int row = alarmList_->currentRow();
    if (row < 0 || row >= events_.size()) return;
    const AlarmEvent event = events_.at(row);

    QStringList causes;
    QStringList steps;
    switch (event.type) {
    case AlarmType::ColorMismatch:
        causes << QStringLiteral("来料中混入不符合预设颜色的物料")
               << QStringLiteral("检测区光照变化或反光导致颜色分类偏差")
               << QStringLiteral("传送带表面残留物遮挡被误识别");
        steps << QStringLiteral("核对该批次物料的颜色要求与预设配方")
              << QStringLiteral("检查检测区光照、遮光板与背景反光")
              << QStringLiteral("清理传送带检测区残留物料")
              << QStringLiteral("复位告警后观察后续 5 件物料分类结果");
        break;
    case AlarmType::MaterialStuck:
        causes << QStringLiteral("物料卡在检测区或挡板处")
               << QStringLiteral("传送带打滑或停转")
               << QStringLiteral("机械臂取料动作未完成，阻塞后续物料");
        steps << QStringLiteral("暂停上料并确认设备状态后进入检测区")
              << QStringLiteral("清理滞留物料，检查是否有变形件")
              << QStringLiteral("检查皮带张紧与传送带运转速度")
              << QStringLiteral("空载运行一圈确认无卡滞后再恢复供料");
        break;
    case AlarmType::MaterialMissing:
        causes << QStringLiteral("上料口/料斗缺料")
               << QStringLiteral("上料机构卡滞或供料节拍异常")
               << QStringLiteral("相机画面过暗导致视觉漏检");
        steps << QStringLiteral("检查上料口是否仍有物料")
              << QStringLiteral("确认上料机构动作与供料节拍")
              << QStringLiteral("查看相机实时画面与曝光设置")
              << QStringLiteral("恢复供料后确认计数是否正常累加");
        break;
    }

    QString html = QStringLiteral(
        "<html><body>"
        "<h3>【离线模拟分析】事件 #%1 · %2</h3>"
        "<p>%3 · 涉及颜色 %4 · 持续 %5 秒 · 状态：%6</p>"
        "<p style='color:#e8a33d'>本结果由本地规则模板生成，属于模拟输出，"
        "未调用外部大模型服务。</p>"
        "<h4>可能原因</h4><ul>")
        .arg(static_cast<int>(event.eventId))
        .arg(alarmTypeName(event.type))
        .arg(event.detectedAt.isValid()
                 ? event.detectedAt.toString(QStringLiteral("HH:mm:ss"))
                 : QStringLiteral("--:--:--"))
        .arg(colorText(event))
        .arg(static_cast<int>(event.durationS))
        .arg(event.active ? QStringLiteral("持续中") : QStringLiteral("已恢复"));
    appendList(&html, "li", causes);
    html += QStringLiteral("</ul><h4>排查建议</h4><ol>");
    appendList(&html, "li", steps);
    html += QStringLiteral("</ol></body></html>");
    result_->setHtml(html);
}

