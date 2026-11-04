#ifndef DASHBOARDWINDOW_H
#define DASHBOARDWINDOW_H

#include <QMainWindow>
#include <QJsonObject>

class QLabel;
class QPushButton;
class QStackedWidget;

class DashboardWindow : public QMainWindow
{
public:
    explicit DashboardWindow(QWidget *parent = nullptr);
    void showArmPage();
    void showConveyorPage();
    QJsonObject handleArmConsoleCommand(const QJsonObject &command);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void selectPage(int index);

    QStackedWidget *pages_;
    QPushButton *tasksButton_;
    QPushButton *systemButton_;
    QPushButton *armButton_;
    QPushButton *conveyorButton_;
    QLabel *clockLabel_;
};

#endif
