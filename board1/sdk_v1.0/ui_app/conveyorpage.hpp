#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include <QJsonObject>
#include <functional>
#include <memory>
#include "../conveyor_linux/conveyor.hpp"
class QLabel;class QComboBox;class QLineEdit;class QCheckBox;class QDoubleSpinBox;
class QPushButton;class QPlainTextEdit;class BeltView;class SortSettings;

class ConveyorPage : public QWidget {
public:
    using ArmHandler=std::function<QJsonObject(const QJsonObject &)>;
    explicit ConveyorPage(ArmHandler arm,QWidget *parent=nullptr);
    ~ConveyorPage();
    QJsonObject handleConsoleCommand(const QJsonObject &command);
protected:
    void hideEvent(QHideEvent *event) override;
private:
    void connectBackend();
    void start(bool demo,bool sorting=false);
    void beginSort();
    void tick();
    void stop();
    void release();
    void preview();
    void playArmActions();
    void readVision();
    void refresh();
    void event(const QString &message);
    void fail(const QString &message);
    uint64_t now() const;
    QJsonObject status() const;
    ArmHandler arm_;
    QElapsedTimer clock_;
    conveyor::Config config_;
    std::unique_ptr<conveyor::Registers> registers_;
    std::unique_ptr<conveyor::MotorDriver> motor_;
    std::unique_ptr<conveyor::Controller> controller_;
    QComboBox *mode_;
    QLineEdit *uio_,*configPath_,*visionPath_,*calibrationPath_,*className_;
    QDoubleSpinBox *target_,*y_,*z_,*pitch_,*speed_;
    QCheckBox *watch_,*autoRelease_;
    QLabel *banner_,*state_,*position_,*speedText_,*armText_,*error_;
    QPlainTextEdit *events_;
    BeltView *view_;
    SortSettings *sortSettings_;
    QJsonObject activeSortConfig_;
    bool autoSort_=false,sortJob_=false;
    unsigned sortJobId_=0,sorted_=0;
    unsigned armActionJobId_=0;
    QString sortedClass_,sortedZone_;
    double plantX_=5,plantV_=0,encoderPosition_=0,lastX_=0,lastY_=0;
    int64_t encoderCount_=0;
    uint64_t sequence_=0,objectId_=1,lastTick_=0,lastFrame_=0,pickStart_=0;
    uint64_t sourceSequence_=0,sourceCapture_=0,startWallMs_=0;
    uint64_t sourceObjectId_=0,releasedObjectId_=0;
    bool sourceSeen_=false,awaitEntry_=false,demo_=false,armJob_=false,operationOk_=true;
    unsigned completed_=0;
    QString sourceClass_,message_;
    QJsonObject observationReference_;
    conveyor::State previous_=conveyor::State::Idle;
};
