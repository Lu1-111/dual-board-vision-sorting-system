#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include <QJsonObject>
#include <memory>
#include <array>
#include <vector>
#include "arm_actions.hpp"
#include "../arm_servo_linux/arm_control.hpp"
#include "../arm_servo_linux/robot_arm.hpp"
class QComboBox; class QLineEdit; class QLabel; class QDoubleSpinBox;
class QSpinBox; class QTableWidget; class QCheckBox; class QTimer; class QPushButton;
class ConveyorPage;class QTabWidget;

class ArmPage : public QWidget {
public:
    explicit ArmPage(QWidget *parent = nullptr);
    ~ArmPage();
    QJsonObject handleConsoleCommand(const QJsonObject &command);
    void showConveyorTab();
    void showJointTab();
protected:
    void hideEvent(QHideEvent *event) override;
private:
    void resetSession(bool hardware);
    void releaseSession();
    void tick();
    void refreshStatus();
    void report(const QString &error);
    void cancelActions();
    void startActions(const ArmActions &actions);
    bool confirmBaseline();
    bool ready();
    void enable();
    void enablePulseDebug();
    void closePwm();
    void setHomeTargetControls();
    void syncTargetControls(const arm::Pulses &target);
    void editTarget(int axis,bool pulses);
    bool bindActionMapping(QString &error);
    arm::Pulses debugHomePulses() const;
    void moveManual(bool pulses);
    void setControlsFromConfig();
    void showConfig();
    bool readConfig(arm::ArmConfig &candidate, QString &error) const;
    void saveConfig();
    void loadConfig();
    void refreshActions();
    bool loadActionsFromPath(const QString &path, QString &error);
    bool setCoordinateOrigin(bool enabled, QString &error);
    void setCoordinateControlsFromFrame();
    bool validateActions(ArmActions &validated, QString &error);
    std::array<double,6> minimumAngles() const;
    std::array<double,6> maximumAngles() const;
    arm::ArmConfig config_;
    std::unique_ptr<arm::Backend> backend_;
    std::unique_ptr<arm::Controller> controller_;
    std::unique_ptr<arm::RobotArm> robot_;
    ArmActions actions_;
    QString actionFile_;
    ArmActionPlayer player_;
    QString sortJobState_="idle";
    unsigned sortJobId_=0;
    QString actionJobState_="idle";
    unsigned actionJobId_=0;
    bool returningBeforeOff_=false;
    bool targetControlsSynced_=false,lastTargetDebug_=false;
    arm::Pulses lastCommandTarget_{{0,0,0,0,0,0}};
    qint64 returnSettledAt_=-1;
    qint64 lastUiTickMs_=-1;
    QElapsedTimer clock_;
    QTimer *timer_;
    QComboBox *mode_;
    QLineEdit *lowChip_, *highChip_, *configPath_;
    QLabel *status_, *error_, *position_, *coordinatePreview_, *coordinateOrigin_;
    std::array<QDoubleSpinBox*,6> angles_, pulses_;
    QSpinBox *duration_, *hold_;
    std::array<QDoubleSpinBox*,4> coordinate_, geometry_;
    QDoubleSpinBox *claw_, *roll_, *calibrationPulse_;
    QComboBox *calibrationAxis_;
    QTableWidget *configTable_, *actionTable_;
    QCheckBox *calibrationConfirmed_, *geometryConfirmed_;
    QPushButton *enableButton_, *debugButton_, *pulseMoveButton_, *calibrationTestButton_, *disableButton_;
    std::vector<QWidget*> normalMotionControls_;
    ConveyorPage *conveyor_;
    QTabWidget *tabs_;
};
