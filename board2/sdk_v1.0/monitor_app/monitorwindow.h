#ifndef MONITOR_MONITORWINDOW_H
#define MONITOR_MONITORWINDOW_H

#include <QMainWindow>

class AlarmPage;
class IDataSource;
class QLabel;
class OverviewPage;
class LiveVisionPage;
class QPushButton;
class QStackedWidget;

// 板2 监控终端主窗口：总览 / 告警 / 实时视觉，侧栏导航。
// 布局按 1024×600 设计（最小尺寸即 1024×600，全屏运行于板2 HDMI 屏）。
class MonitorWindow : public QMainWindow {
public:
    explicit MonitorWindow(IDataSource *source = nullptr, QWidget *parent = nullptr);
    void showLivePage() { selectPage(2); }

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void selectPage(int index);

    IDataSource *source_;
    QStackedWidget *pages_;
    QPushButton *overviewButton_;
    QPushButton *alarmButton_;
    QPushButton *liveButton_;
    OverviewPage *overviewPage_;
    AlarmPage *alarmPage_;
    LiveVisionPage *livePage_;
};

#endif
