#include "overviewpage.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QFrame *makeCard(const QString &title, QVBoxLayout **outBody)
{
    QFrame *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(6);
    QLabel *label = new QLabel(title);
    label->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(label);
    if (outBody) *outBody = layout;
    return card;
}

QLabel *makeChip(const QString &hex, int size)
{
    QLabel *chip = new QLabel;
    chip->setFixedSize(size, size);
    chip->setStyleSheet(QStringLiteral("background:%1; border-radius:%2px;")
                            .arg(hex)
                            .arg(qMax(2, size / 5)));
    return chip;
}

} // namespace

OverviewPage::OverviewPage(const QString &sourceName, QWidget *parent)
    : QWidget(parent),
      stateValue_(new QLabel(QStringLiteral("待机"))),
      commValue_(new QLabel(QStringLiteral("等待数据…"))),
      sourceValue_(new QLabel(sourceName)),
      uptimeValue_(new QLabel(QStringLiteral("00:00:00"))),
      currentChip_(makeChip(materialColorHex(MaterialColor::None), 44)),
      currentName_(new QLabel(QStringLiteral("—"))),
      confidenceValue_(new QLabel(QStringLiteral("置信度 —"))),
      visionValue_(new QLabel(QStringLiteral("视觉：关闭"))),
      noAlarmLabel_(new QLabel(QStringLiteral("无活跃告警"))),
      alarmList_(new QListWidget),
      timer_(new QTimer(this))
{
    setObjectName(QStringLiteral("overviewPage"));
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // 顶部状态条：状态 / 通信 / 数据来源 / 运行时长。
    QHBoxLayout *top = new QHBoxLayout;
    top->setSpacing(12);
    root->addLayout(top);

    QVBoxLayout *stateBody = nullptr;
    top->addWidget(makeCard(QStringLiteral("生产线状态"), &stateBody), 1);
    stateValue_->setObjectName(QStringLiteral("bigState"));
    stateBody->addWidget(stateValue_);

    QVBoxLayout *commBody = nullptr;
    top->addWidget(makeCard(QStringLiteral("模拟心跳"), &commBody), 1);
    commTitle_ = qobject_cast<QLabel *>(commBody->itemAt(0)->widget());
    commValue_->setObjectName(QStringLiteral("commValue"));
    commValue_->setStyleSheet(QStringLiteral("color:#a6b9d3;"));
    commBody->addWidget(commValue_);

    QVBoxLayout *sourceBody = nullptr;
    top->addWidget(makeCard(QStringLiteral("数据来源"), &sourceBody), 1);
    sourceValue_->setObjectName(QStringLiteral("sourceValue"));
    sourceBody->addWidget(sourceValue_);

    QVBoxLayout *uptimeBody = nullptr;
    top->addWidget(makeCard(QStringLiteral("板一开机时长"), &uptimeBody), 1);
    uptimeValue_->setObjectName(QStringLiteral("uptimeValue"));
    uptimeBody->addWidget(uptimeValue_);

    // 中部：分类计数 + 当前物料。
    QHBoxLayout *middle = new QHBoxLayout;
    middle->setSpacing(12);
    root->addLayout(middle, 1);

    QVBoxLayout *countsBody = nullptr;
    middle->addWidget(makeCard(QStringLiteral("已完成分拣计数 · 本次开机"), &countsBody), 5);
    const MaterialColor quality[2] = {MaterialColor::GreenGood, MaterialColor::GreenDefect};
    for (int i = 0; i < 2; ++i) {
        const MaterialColor color = quality[i];
        QWidget *row = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(10);
        layout->addWidget(makeChip(materialQualityHex(color), 16));
        QLabel *name = new QLabel(materialQualityName(color));
        layout->addWidget(name);
        layout->addStretch();
        countValues_[i] = new QLabel(QStringLiteral("0"));
        countValues_[i]->setObjectName(i == 0 ? QStringLiteral("goodCount") : QStringLiteral("defectCount"));
        layout->addWidget(countValues_[i]);
        countsBody->addWidget(row);
    }

    QVBoxLayout *currentBody = nullptr;
    middle->addWidget(makeCard(QStringLiteral("当前物料"), &currentBody), 4);
    QWidget *chipRow = new QWidget;
    QHBoxLayout *chipLayout = new QHBoxLayout(chipRow);
    chipLayout->setContentsMargins(0, 4, 0, 4);
    chipLayout->setSpacing(14);
    chipLayout->addWidget(currentChip_);
    currentName_->setObjectName(QStringLiteral("currentMaterial"));
    currentName_->setStyleSheet(QStringLiteral("font-size:26px; font-weight:700;"));
    currentName_->setMinimumWidth(110); // Keep the three-character unknown quality visible during rapid updates.
    chipLayout->addWidget(currentName_);
    chipLayout->addStretch();
    currentBody->addWidget(chipRow);
    confidenceValue_->setObjectName(QStringLiteral("confidenceValue"));
    currentBody->addWidget(confidenceValue_);
    visionValue_->setObjectName(QStringLiteral("muted"));
    currentBody->addWidget(visionValue_);

    // 底部：活跃告警。
    QVBoxLayout *alarmBody = nullptr;
    root->addWidget(makeCard(QStringLiteral("活跃告警"), &alarmBody), 2);
    alarmList_->setObjectName(QStringLiteral("alarmList"));
    noAlarmLabel_->setObjectName(QStringLiteral("alarmMuted"));
    alarmBody->addWidget(noAlarmLabel_);
    alarmBody->addWidget(alarmList_);

    connect(timer_, &QTimer::timeout, this, &OverviewPage::refreshTick);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->start(2000);
}

void OverviewPage::applyHeartbeat(const Heartbeat &beat)
{
    pendingBeat_=beat; haveBeat_=true; lastHeartbeatAt_.restart();
}

void OverviewPage::renderHeartbeat(const Heartbeat &beat)
{
    lastSequence_ = beat.sequence;
    uptimeValue_->setText(formatHms(beat.uptimeS));
    switch (beat.runState) {
    case RunState::Running:
        stateValue_->setText(runStateName(beat.runState));
        stateValue_->setStyleSheet(QStringLiteral("color:#7dd6c3;"));
        break;
    case RunState::Fault:
        stateValue_->setText(runStateName(beat.runState));
        stateValue_->setStyleSheet(QStringLiteral("color:#e05252;"));
        break;
    case RunState::Paused:
        stateValue_->setText(runStateName(beat.runState));
        stateValue_->setStyleSheet(QStringLiteral("color:#e0c341;"));
        break;
    case RunState::Idle:
        stateValue_->setText(runStateName(beat.runState));
        stateValue_->setStyleSheet(QStringLiteral("color:#a6b9d3;"));
        break;
    }
} 

void OverviewPage::applyStatus(const LineStatus &frame)
{
    pendingStatus_=frame; haveStatus_=true; statusAge_.restart();
}

void OverviewPage::renderStatus(const LineStatus &frame)
{
    const bool hasQuality = frame.currentColor == MaterialColor::GreenGood || frame.currentColor == MaterialColor::GreenDefect;
    currentChip_->setStyleSheet(
        QStringLiteral("background:%1; border-radius:8px;")
            .arg(materialQualityHex(frame.currentColor)));
    currentName_->setText(materialQualityName(frame.currentColor));
    confidenceValue_->setText(hasQuality
            ? QStringLiteral("置信度 %1%").arg(frame.confidence100)
            : QStringLiteral("置信度 —"));
    switch (frame.visionState) {
    case 1: visionValue_->setText(QStringLiteral("视觉：正常")); break;
    case 2: visionValue_->setText(QStringLiteral("视觉：降级")); break;
    case 3: visionValue_->setText(QStringLiteral("视觉：故障")); break;
    case 0: visionValue_->setText(QStringLiteral("视觉：关闭")); break;
    }
}

void OverviewPage::applyCounters(quint32 red, quint32 yellow, quint32 blue,
                                 quint32 greenGood, quint32 greenDefect)
{
    Q_UNUSED(red); Q_UNUSED(yellow); Q_UNUSED(blue);
    pendingCounts_[0]=greenGood; pendingCounts_[1]=greenDefect; haveCounts_=true; countsAge_.restart();
}

QString OverviewPage::alarmSummary(const AlarmEvent &event)
{
    QString text = QStringLiteral("[事件 %1] %2")
                       .arg(event.eventId)
                       .arg(alarmTypeName(event.type));
    if (event.color != MaterialColor::None)
        text += QStringLiteral(" · 涉及 %1").arg(materialColorName(event.color));
    if(event.detectedAt.date().year()<2025 && QDateTime::currentDateTime().date().year()>=2025)
        text += QStringLiteral(" · 触发时尚未校时");
    else text += QStringLiteral(" · 已持续 %1 秒")
                .arg(qMax<qint64>(0,event.detectedAt.secsTo(QDateTime::currentDateTime())));
    return text;
}

void OverviewPage::alarmRaised(const AlarmEvent &event)
{
    activeAlarms_.insert(event.eventId, event);
    rebuildActiveAlarms();
}

void OverviewPage::alarmCleared(const AlarmEvent &event)
{
    activeAlarms_.remove(event.eventId);
    rebuildActiveAlarms();
}

void OverviewPage::rebuildActiveAlarms()
{
    alarmList_->clear();
    for (const AlarmEvent &event : activeAlarms_) {
        QListWidgetItem *item = new QListWidgetItem(alarmSummary(event));
        QColor foreground(0xe0, 0x52, 0x52);
        if (event.type == AlarmType::ColorMismatch)
            foreground = QColor(0xe0, 0xa3, 0x3d);
        else if (event.type == AlarmType::MaterialMissing)
            foreground = QColor(0xd8, 0xc3, 0x41);
        item->setForeground(foreground);
        item->setData(Qt::UserRole, event.eventId);
        alarmList_->addItem(item);
    }
    const bool empty = activeAlarms_.isEmpty();
    noAlarmLabel_->setVisible(empty);
    alarmList_->setVisible(!empty);
}

void OverviewPage::refreshTick()
{
    if ((!canMode_ || online_) && haveBeat_) {
        renderHeartbeat(pendingBeat_);
        if(canMode_) {
            commValue_->setText(linkMessage_ + QStringLiteral("\n每 2 秒刷新"));
            commValue_->setStyleSheet(QStringLiteral("color:#7dd6c3;font-size:17px;"));
        }
        if(haveStatus_ && statusAge_.elapsed()<=6000) renderStatus(pendingStatus_);
        else { currentName_->setText(QStringLiteral("—")); confidenceValue_->setText(QStringLiteral("置信度 —")); visionValue_->setText(QStringLiteral("视觉：等待新数据")); currentChip_->setStyleSheet(QStringLiteral("background:#5a6b80;border-radius:8px;")); }
        for(int i=0;i<2;++i) countValues_[i]->setText(haveCounts_ && countsAge_.elapsed()<=6000 ? QString::number(pendingCounts_[i]) : QStringLiteral("—"));
    }
    if (!canMode_ && lastHeartbeatAt_.isValid()) {
        const qint64 ageS = lastHeartbeatAt_.elapsed() / 1000;
        if (ageS > 3) {
            commValue_->setText(QStringLiteral("心跳超时 %1 秒").arg(ageS));
            commValue_->setStyleSheet(QStringLiteral("color:#e05252;"));
        } else {
            commValue_->setText(QStringLiteral("正常 · 序号 %1")
                                    .arg(static_cast<int>(lastSequence_)));
            commValue_->setStyleSheet(QStringLiteral("color:#7dd6c3;"));
        }
    }
    for (int i = 0; i < alarmList_->count(); ++i) {
        QListWidgetItem *item = alarmList_->item(i);
        const auto it = activeAlarms_.constFind(
            item->data(Qt::UserRole).toUInt());
        if (it != activeAlarms_.constEnd()) item->setText(alarmSummary(it.value()));
    }
}

void OverviewPage::setCanMode(bool enabled)
{
    canMode_=enabled;
    commTitle_->setText(enabled?QStringLiteral("CAN 心跳"):QStringLiteral("模拟心跳"));
    if(enabled) resetSession();
}
void OverviewPage::setLinkState(bool online,const QString &message)
{
    online_=online; linkMessage_=message;
    if(online) return;
    haveBeat_=haveStatus_=haveCounts_=false;
    commValue_->setText(message);
    commValue_->setWordWrap(true);
    commValue_->setStyleSheet(online?QStringLiteral("color:#7dd6c3;font-size:17px;"):QStringLiteral("color:#e05252;font-size:17px;"));
    if(!online) {
        stateValue_->setText(QStringLiteral("未知")); stateValue_->setStyleSheet(QStringLiteral("color:#a6b9d3;"));
        currentName_->setText(QStringLiteral("—")); confidenceValue_->setText(QStringLiteral("置信度 —"));
        visionValue_->setText(QStringLiteral("视觉：等待新数据"));
        for(auto *value:countValues_) value->setText(QStringLiteral("—"));
        currentChip_->setStyleSheet(QStringLiteral("background:#5a6b80;border-radius:8px;"));
        uptimeValue_->setText(QStringLiteral("—"));
    }
}
void OverviewPage::resetSession()
{
    activeAlarms_.clear(); rebuildActiveAlarms(); lastHeartbeatAt_.invalidate();
    setLinkState(false,QStringLiteral("等待有效心跳"));
}
