#include "alarmpage.h"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
const int kMaxRows = 500;
}

AlarmPage::AlarmPage(QWidget *parent)
    : QWidget(parent),
      filter_(new QComboBox),
      stats_(new QLabel),
      table_(new QTableWidget)
{
    setObjectName(QStringLiteral("alarmPage"));
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    QHBoxLayout *bar = new QHBoxLayout;
    bar->setSpacing(10);
    QLabel *title = new QLabel(QStringLiteral("事件历史"));
    title->setObjectName(QStringLiteral("sectionTitle"));
    bar->addWidget(title);
    bar->addStretch();
    bar->addWidget(new QLabel(QStringLiteral("筛选:")));
    filter_->setObjectName(QStringLiteral("alarmFilter"));
    filter_->addItems(QStringList()
                      << QStringLiteral("全部")
                      << QStringLiteral("颜色不符")
                      << QStringLiteral("物料滞留")
                      << QStringLiteral("缺料"));
    bar->addWidget(filter_);
    QPushButton *clearButton = new QPushButton(QStringLiteral("清空显示"));
    clearButton->setObjectName(QStringLiteral("secondaryButton"));
    bar->addWidget(clearButton);
    stats_->setObjectName(QStringLiteral("muted"));
    bar->addWidget(stats_);
    root->addLayout(bar);

    table_->setObjectName(QStringLiteral("alarmTable"));
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(QStringList()
                                      << QStringLiteral("时间")
                                      << QStringLiteral("事件号")
                                      << QStringLiteral("类型")
                                      << QStringLiteral("涉及颜色")
                                      << QStringLiteral("持续(秒)")
                                      << QStringLiteral("状态")
                                      << QStringLiteral("恢复时间"));
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    root->addWidget(table_, 1);

    connect(filter_,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) { rebuild(); });
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        events_.clear();
        rebuild();
    });
    rebuild();
}

void AlarmPage::alarmRaised(const AlarmEvent &event)
{
    events_.prepend(event);
    while (events_.size() > kMaxRows) events_.removeLast();
    rebuild();
}

void AlarmPage::alarmCleared(const AlarmEvent &event)
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
    rebuild();
}

void AlarmPage::rebuild()
{
    const int filterType = filter_->currentIndex();  // 0 全部, 1..3 对应枚举
    int activeCount = 0;
    for (const AlarmEvent &event : events_) {
        if (event.active) ++activeCount;
    }

    int row = 0;
    table_->setRowCount(0);
    for (const AlarmEvent &event : events_) {
        if (filterType != 0 && static_cast<int>(event.type) != filterType) continue;
        table_->insertRow(row);

        const QString colorText = event.color == MaterialColor::None
            ? QStringLiteral("—")
            : materialColorName(event.color);
        const QString timeText = event.detectedAt.isValid()
            ? event.detectedAt.toString(QStringLiteral("HH:mm:ss"))
            : QStringLiteral("—");
        const QString recoveredText = event.recoveredAt.isValid()
            ? event.recoveredAt.toString(QStringLiteral("HH:mm:ss"))
            : QStringLiteral("—");

        table_->setItem(row, 0, new QTableWidgetItem(timeText));
        table_->setItem(row, 1,
            new QTableWidgetItem(QString::number(event.eventId)));
        table_->setItem(row, 2, new QTableWidgetItem(alarmTypeName(event.type)));
        table_->setItem(row, 3, new QTableWidgetItem(colorText));
        table_->setItem(row, 4,
            new QTableWidgetItem(QString::number(static_cast<int>(event.durationS))));
        QTableWidgetItem *stateItem = new QTableWidgetItem(
            event.active ? QStringLiteral("持续中") : event.recoveryUnknown ? QStringLiteral("结束时间未知") : QStringLiteral("已恢复"));
        stateItem->setForeground(event.active
            ? QBrush(QColor(0xe0, 0x52, 0x52))
            : QBrush(QColor(0x7d, 0xd6, 0xc3)));
        table_->setItem(row, 5, stateItem);
        table_->setItem(row, 6, new QTableWidgetItem(recoveredText));
        ++row;
    }

    stats_->setText(QStringLiteral("共 %1 条 · 活跃 %2")
                        .arg(events_.size())
                        .arg(activeCount));
}

void AlarmPage::resetSession() { events_.clear(); rebuild(); }
