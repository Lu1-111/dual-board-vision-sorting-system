#include "monitorwindow.h"

#include "alarmpage.h"
#include "datasource.h"
#include "overviewpage.h"
#include "livevisionpage.h"
#include "simdatasource.h"
#include "candatasource.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

MonitorWindow::MonitorWindow(IDataSource *source, QWidget *parent)
    : QMainWindow(parent),
      source_(source ? source : new SimDataSource(1000, this)),
      pages_(new QStackedWidget),
      overviewButton_(new QPushButton(QStringLiteral("监控总览"))),
      alarmButton_(new QPushButton(QStringLiteral("告警记录"))),
      liveButton_(new QPushButton(QStringLiteral("实时视觉"))),
      overviewPage_(new OverviewPage(source_->sourceName())),
      alarmPage_(new AlarmPage),
      livePage_(new LiveVisionPage)
{
    setWindowTitle(QStringLiteral("流水线监控终端"));
    setMinimumSize(1024, 600);
    setStyleSheet(QString::fromLatin1(R"QSS(
        QMainWindow, QDialog, QWidget#root, QWidget#content, QWidget#overviewPage,
        QWidget#alarmPage, QWidget#liveVisionPage { background: #101827; color: #edf3fc; }
        QFrame#sidebar { background: #172235; border-right: 1px solid #29384d; }
        QFrame#card { background: #1b2a3f; border: 1px solid #31415a;
                      border-radius: 12px; }
        QLabel { color: #edf3fc; background: transparent; }
        QLabel#brand { font-size: 22px; font-weight: 700; }
        QLabel#sectionTitle { font-size: 17px; font-weight: 600; }
        QLabel#cardTitle { color: #a6b9d3; font-size: 13px; }
        QLabel#commValue, QLabel#sourceValue, QLabel#uptimeValue,
        QLabel#confidenceValue { font-size: 21px; font-weight: 700; }
        QLabel#bigState { font-size: 28px; font-weight: 700; }
        QLabel#countValue, QLabel#goodCount, QLabel#defectCount { font-size: 24px; font-weight: 700; }
        QLabel#alarmMuted { color: #5f8f6f; font-size: 15px; }
        QLabel#muted, QLabel#clock { color: #a6b9d3; font-size: 13px; }
        QPushButton { min-height: 30px; background: #2b4059; color: #e3edfb;
            border: 1px solid #46607b; border-radius: 8px;
            padding: 6px 14px; font-size: 14px; }
        QPushButton:hover { background: #395674; }
        QPushButton#navButton { background: transparent; color: #b4c3d8;
            border: 0; border-radius: 9px; padding: 11px 14px;
            text-align: left; font-size: 16px; }
        QPushButton#navButton:hover { background: #263a54; color: white; }
        QPushButton#navButton[selected="true"] { background: #245d68;
            color: #e6fffa; font-weight: 700; }
        QPushButton#exitButton {
            background: #2b4059; color: #e3edfb; border: 1px solid #46607b;
            border-radius: 8px; padding: 6px 14px; font-size: 14px; }
        QPushButton#exitButton:hover {
            background: #395674; }
        QPushButton:disabled { color: #75869c; background: #253248;
            border-color: #344459; }
        QLineEdit, QComboBox, QSpinBox { background: #142237;
            color: #edf3fc; border: 1px solid #405473; border-radius: 7px;
            padding: 6px; selection-background-color: #267a75; }
        QTableWidget { background: #142237; alternate-background-color: #192a40;
            color: #edf3fc; border: 1px solid #31415a;
            gridline-color: #2b4059; selection-background-color: #245d68; }
        QHeaderView::section { background: #23364e; color: #d7e6f8;
            border: 0; border-bottom: 1px solid #405473; padding: 8px; }
        QListWidget { background: #142237; color: #edf3fc;
            border: 1px solid #31415a; border-radius: 8px; padding: 4px; }
        QListWidget::item { padding: 8px; border-radius: 6px; margin: 1px 2px; }
        QListWidget::item:selected { background: #245d68; color: white; }
        QTextBrowser { background: #142237; color: #d8e5f5;
            border: 1px solid #31415a; border-radius: 8px; padding: 10px; }
    )QSS"));

    QWidget *root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    QHBoxLayout *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(root);

    QFrame *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(190);
    QVBoxLayout *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(16, 22, 16, 16);
    side->setSpacing(10);
    QLabel *brand = new QLabel(QStringLiteral("流水线监控"));
    brand->setObjectName(QStringLiteral("brand"));
    side->addWidget(brand);
    QLabel *subtitle = new QLabel(QStringLiteral("分拣 · 告警 · 视觉"));
    subtitle->setObjectName(QStringLiteral("muted"));
    side->addWidget(subtitle);
    side->addSpacing(20);
    for (QPushButton *button : {overviewButton_, alarmButton_, liveButton_}) {
        button->setObjectName(QStringLiteral("navButton"));
        button->setCursor(Qt::PointingHandCursor);
        side->addWidget(button);
    }
    side->addStretch();
    if (auto *sim = qobject_cast<SimDataSource *>(source_)) {
        QLabel *demoLabel = new QLabel(QStringLiteral("演示场景"));
        demoLabel->setObjectName(QStringLiteral("muted"));
        side->addWidget(demoLabel);
        auto *scenario = new QComboBox;
        scenario->setObjectName(QStringLiteral("demoScenario"));
        scenario->addItems({QStringLiteral("自动循环"), QStringLiteral("正常分拣"),
            QStringLiteral("颜色不符"), QStringLiteral("物料滞留"), QStringLiteral("缺料")});
        side->addWidget(scenario);
        connect(scenario, QOverload<int>::of(&QComboBox::currentIndexChanged),
                sim, [sim](int index) { sim->setScenario(index - 1); });
    }
    QLabel *platform = new QLabel(source_->isCan()?QStringLiteral("CAN：等待数据"):QStringLiteral("板 2 · 离线演示\nCAN：未接入"));
    platform->setObjectName(QStringLiteral("muted"));
    platform->setWordWrap(true);
    side->addWidget(platform);
    rootLayout->addWidget(sidebar);

    QWidget *content = new QWidget;
    content->setObjectName(QStringLiteral("content"));
    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(22, 14, 22, 16);
    contentLayout->setSpacing(12);
    QHBoxLayout *top = new QHBoxLayout;
    QLabel *heading = new QLabel(source_->isCan()?QStringLiteral("CAN 接收 · 等待发送端声明数据来源"):QStringLiteral("模拟演示 · 所有生产数据均为模拟值"));
    heading->setObjectName(QStringLiteral("muted"));
    top->addWidget(heading);
    top->addStretch();
    top->addSpacing(12);
    QPushButton *exitButton = new QPushButton(QStringLiteral("退出"));
    exitButton->setObjectName(QStringLiteral("exitButton"));
    top->addWidget(exitButton);
    contentLayout->addLayout(top);
    overviewPage_->setCanMode(source_->isCan());
    if(auto *can=qobject_cast<CanDataSource *>(source_)) {
        connect(can,&CanDataSource::diagnostics,this,[platform](const QString &text){platform->setText(text);});
        connect(can,&CanDataSource::peerMode,this,[heading,can](bool simulated){
            heading->setText(can->isSimulation()?QStringLiteral("CAN 模拟自检 · 非板间实线通信"):simulated?QStringLiteral("CAN 接收 · 发送端使用模拟数据"):QStringLiteral("CAN 接收 · 第一块板上报数据"));
        });
        if(can->isSimulation()) {
            auto *pause=new QPushButton(QStringLiteral("模拟断线")); pause->setObjectName(QStringLiteral("canPause")); pause->setCheckable(true);
            side->insertWidget(side->count()-1,pause);
            connect(pause,&QPushButton::toggled,this,[can,pause](bool paused){can->setPaused(paused); pause->setText(paused?QStringLiteral("恢复接收"):QStringLiteral("模拟断线"));});
        }
    }

    pages_->addWidget(overviewPage_);
    pages_->addWidget(alarmPage_);
    pages_->addWidget(livePage_);
    contentLayout->addWidget(pages_, 1);
    rootLayout->addWidget(content, 1);

    connect(overviewButton_, &QPushButton::clicked, this,
            [this]() { selectPage(0); });
    connect(alarmButton_, &QPushButton::clicked, this,
            [this]() { selectPage(1); });
    connect(liveButton_, &QPushButton::clicked, this,
            [this]() { selectPage(2); });
    connect(exitButton, &QPushButton::clicked, this, &QWidget::close);
    selectPage(0);

    connect(source_, &IDataSource::heartbeat, overviewPage_,
            &OverviewPage::applyHeartbeat);
    connect(source_, &IDataSource::linkState, overviewPage_, &OverviewPage::setLinkState);
    connect(source_, &IDataSource::sessionReset, overviewPage_, &OverviewPage::resetSession);
    connect(source_, &IDataSource::sessionReset, alarmPage_, &AlarmPage::resetSession);
    connect(source_, &IDataSource::status, overviewPage_,
            &OverviewPage::applyStatus);
    connect(source_, &IDataSource::counters, overviewPage_,
            &OverviewPage::applyCounters);
    connect(source_, &IDataSource::alarmRaised, overviewPage_,
            &OverviewPage::alarmRaised);
    connect(source_, &IDataSource::alarmCleared, overviewPage_,
            &OverviewPage::alarmCleared);
    connect(source_, &IDataSource::alarmRaised, alarmPage_,
            &AlarmPage::alarmRaised);
    connect(source_, &IDataSource::alarmCleared, alarmPage_,
            &AlarmPage::alarmCleared);

}

void MonitorWindow::selectPage(int index)
{
    pages_->setCurrentIndex(index);
    overviewButton_->setProperty("selected", index == 0);
    alarmButton_->setProperty("selected", index == 1);
    liveButton_->setProperty("selected", index == 2);
    for (QPushButton *button : {overviewButton_, alarmButton_, liveButton_}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

void MonitorWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
