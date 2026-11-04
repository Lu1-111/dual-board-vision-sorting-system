#include "dashboardwindow.h"
#include "systempage.h"
#include "taskpage.h"
#include "armpage.hpp"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

DashboardWindow::DashboardWindow(QWidget *parent)
    : QMainWindow(parent), pages_(new QStackedWidget),
      tasksButton_(new QPushButton(QStringLiteral("待办任务"))),
      systemButton_(new QPushButton(QStringLiteral("系统监控"))),
      armButton_(new QPushButton(QStringLiteral("机械臂控制"))),
      conveyorButton_(new QPushButton(QStringLiteral("传送带联动"))),
      clockLabel_(new QLabel)
{
    setWindowTitle(QStringLiteral("任务管理器"));
    setMinimumSize(1024, 640);
    setStyleSheet(QString::fromLatin1(R"QSS(
        QMainWindow, QDialog, QWidget#root, QWidget#content, QWidget#taskPage,
        QWidget#systemPage, QWidget#armPage, QWidget#conveyorPage { background: #101827; color: #edf3fc; }
        QFrame#sidebar { background: #172235; border-right: 1px solid #29384d; }
        QFrame#card { background: #1b2a3f; border: 1px solid #31415a;
                      border-radius: 14px; }
        QLabel { color: #edf3fc; background: transparent; }
        QCheckBox { color: #edf3fc; }
        QTabWidget::pane { background: #101827; border: 1px solid #405473; }
        QTabBar::tab { background: #23364e; color: #d7e6f8; padding: 8px 15px; }
        QTabBar::tab:selected { background: #245d68; color: white; }
        QLabel#brand { font-size: 24px; font-weight: 700; }
        QLabel#pageTitle { font-size: 31px; font-weight: 700; }
        QLabel#sectionTitle { font-size: 19px; font-weight: 600; }
        QLabel#statValue { font-size: 28px; font-weight: 700; color: #7dd6c3; }
        QLabel#muted, QLabel#clock { color: #a6b9d3; font-size: 14px; }
        QLabel#detailText { color: #d8e5f5; font-size: 15px; }
        QLabel#mascot { background: white; border-radius: 12px; }
        QPushButton { min-height: 34px; background: #2b4059; color: #e3edfb;
            border: 1px solid #46607b; border-radius: 8px;
            padding: 8px 16px; font-size: 15px; }
        QPushButton:hover { background: #395674; }
        QPushButton#navButton { background: transparent; color: #b4c3d8;
            border: 0; border-radius: 9px; padding: 12px 16px;
            text-align: left; font-size: 17px; }
        QPushButton#navButton:hover { background: #263a54; color: white; }
        QPushButton#navButton[selected="true"] { background: #245d68;
            color: #e6fffa; font-weight: 700; }
        QPushButton#primaryButton { background: #267a75; color: white;
            border: 0; border-radius: 8px; padding: 8px 17px;
            font-size: 15px; font-weight: 600; }
        QPushButton#primaryButton:hover { background: #33958e; }
        QPushButton#secondaryButton, QPushButton#exitButton {
            background: #2b4059; color: #e3edfb; border: 1px solid #46607b;
            border-radius: 8px; padding: 8px 16px; font-size: 15px; }
        QPushButton#secondaryButton:hover, QPushButton#exitButton:hover {
            background: #395674; }
        QPushButton#dangerButton { background: #563741; color: #ffdce0;
            border: 1px solid #865260; border-radius: 8px;
            padding: 8px 16px; font-size: 15px; }
        QPushButton#dangerButton:hover { background: #74444f; }
        QPushButton:disabled { color: #75869c; background: #253248;
            border-color: #344459; }
        QLineEdit, QTextEdit, QComboBox, QDateEdit, QSpinBox, QDoubleSpinBox { background: #142237;
            color: #edf3fc; border: 1px solid #405473; border-radius: 7px;
            padding: 8px; selection-background-color: #267a75; }
        QTableWidget { background: #142237; alternate-background-color: #192a40;
            color: #edf3fc; border: 1px solid #31415a;
            gridline-color: #2b4059; selection-background-color: #245d68; }
        QHeaderView::section { background: #23364e; color: #d7e6f8;
            border: 0; border-bottom: 1px solid #405473; padding: 10px; }
        QProgressBar { background: #142237; color: #edf3fc;
            border: 1px solid #405473; border-radius: 7px; text-align: center; }
        QProgressBar::chunk { background: #267a75; border-radius: 6px; }
    )QSS"));

    QWidget *root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    QHBoxLayout *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(root);

    QFrame *sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(246);
    QVBoxLayout *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(20, 30, 20, 24);
    side->setSpacing(10);
    QLabel *brand = new QLabel(QStringLiteral("任务管理器"));
    brand->setObjectName(QStringLiteral("brand"));
    side->addWidget(brand);
    QLabel *subtitle = new QLabel(QStringLiteral("机械臂 · 视觉 · 传送带"));
    subtitle->setObjectName(QStringLiteral("muted"));
    side->addWidget(subtitle);
    side->addSpacing(28);
    for (QPushButton *button : {tasksButton_, systemButton_, armButton_, conveyorButton_}) {
        button->setObjectName(QStringLiteral("navButton"));
        button->setCursor(Qt::PointingHandCursor);
        side->addWidget(button);
    }
    side->addStretch();

    QLabel *mascot = new QLabel;
    mascot->setObjectName(QStringLiteral("mascot"));
    mascot->setFixedSize(176, 176);
    mascot->setAlignment(Qt::AlignCenter);
    mascot->setPixmap(QPixmap(QStringLiteral(":/assets/mascot.jpeg")).scaled(
        mascot->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    side->addWidget(mascot, 0, Qt::AlignHCenter);
    side->addSpacing(10);
    QLabel *platform = new QLabel(QStringLiteral("Linux · Qt Widgets"));
    platform->setObjectName(QStringLiteral("muted"));
    side->addWidget(platform);
    rootLayout->addWidget(sidebar);

    QWidget *content = new QWidget;
    content->setObjectName(QStringLiteral("content"));
    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(28, 22, 28, 24);
    contentLayout->setSpacing(18);
    QHBoxLayout *top = new QHBoxLayout;
    QLabel *heading = new QLabel(QStringLiteral("本机工作台"));
    heading->setObjectName(QStringLiteral("muted"));
    top->addWidget(heading);
    top->addStretch();
    clockLabel_->setObjectName(QStringLiteral("clock"));
    top->addWidget(clockLabel_);
    QPushButton *exitButton = new QPushButton(QStringLiteral("退出"));
    exitButton->setObjectName(QStringLiteral("exitButton"));
    top->addSpacing(16);
    top->addWidget(exitButton);
    contentLayout->addLayout(top);

    pages_->addWidget(new TaskPage);
    pages_->addWidget(new SystemPage);
    pages_->addWidget(new ArmPage);
    contentLayout->addWidget(pages_, 1);
    rootLayout->addWidget(content, 1);

    connect(tasksButton_, &QPushButton::clicked, this, [this]() { selectPage(0); });
    connect(systemButton_, &QPushButton::clicked, this, [this]() { selectPage(1); });
    connect(armButton_, &QPushButton::clicked, this, [this]() { selectPage(2); });
    connect(conveyorButton_, &QPushButton::clicked, this, [this]() { selectPage(3); });
    connect(exitButton, &QPushButton::clicked, this, &QWidget::close);
    selectPage(0);

    QTimer *timer = new QTimer(this);
    const auto updateClock = [this]() {
        clockLabel_->setText(QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
    };
    connect(timer, &QTimer::timeout, this, updateClock);
    timer->start(1000);
    updateClock();
}

void DashboardWindow::selectPage(int index)
{
    pages_->setCurrentIndex(index==3?2:index);
    if(index==3)static_cast<ArmPage*>(pages_->widget(2))->showConveyorTab();
    else if(index==2)static_cast<ArmPage*>(pages_->widget(2))->showJointTab();
    tasksButton_->setProperty("selected", index == 0);
    systemButton_->setProperty("selected", index == 1);
    armButton_->setProperty("selected", index == 2);
    conveyorButton_->setProperty("selected", index == 3);
    for (QPushButton *button : {tasksButton_, systemButton_, armButton_, conveyorButton_}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

void DashboardWindow::showArmPage()
{
    selectPage(2);
}
void DashboardWindow::showConveyorPage(){selectPage(3);}

QJsonObject DashboardWindow::handleArmConsoleCommand(const QJsonObject &command)
{
    const auto action=command.value("action").toString();
    if(action.startsWith("conveyor-")){
        if(action!="conveyor-status"&&action!="conveyor-screenshot")showConveyorPage();
    }else if(action!="status"&&action!="xy-preview"&&action!="coordinate-preview")showArmPage();
    return static_cast<ArmPage *>(pages_->widget(2))->handleConsoleCommand(command);
}

void DashboardWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

