#include "armpage.hpp"
#include "conveyorpage.hpp"
#include "sort_recipe.hpp"
#include "../arm_servo_linux/gpio_backend.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonArray>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPalette>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace {
const int returnMinimumMs=3000;
const qint64 returnSettleMs=300;
void motionTrace(const char *event, const QJsonObject &detail) {
    if(qgetenv("HDMI_UI_MOTION_TRACE")!="1")return;
    QJsonObject record=detail;record["event"]=QString::fromLatin1(event);
    qWarning().noquote()<<"ARM_MOTION"<<QJsonDocument(record).toJson(QJsonDocument::Compact);
}
QJsonArray pulseTrace(const arm::Pulses &values) {
    QJsonArray result;for(double value:values)result.append(value);return result;
}
// The deployed board still uses the original GPIO protocol. Keep this UI
// buildable with that unchanged library and with the newer status interface.
template<class T> auto controllerLocked(const T *c,int)->decltype(c->holdingAfterTimeout()){return c->holdingAfterTimeout();}
template<class T> bool controllerLocked(const T *,long){return false;}
template<class T> auto controllerStatusAvailable(const T *c,int)->decltype(c->hardwareStatusAvailable()){return c->hardwareStatusAvailable();}
template<class T> bool controllerStatusAvailable(const T *,long){return false;}
template<class T> auto controllerOutputActive(const T *c,int)->decltype(c->hardwareOutputActive()){return c->hardwareOutputActive();}
template<class T> bool controllerOutputActive(const T *,long){return false;}
QDoubleSpinBox *number(double lo, double hi, double value, int decimals=2) {
    auto *box = new QDoubleSpinBox; box->setRange(lo,hi); box->setDecimals(decimals);
    box->setValue(value); box->setMaximumWidth(110); return box;
}
QPushButton *button(const QString &text) {
    auto *b = new QPushButton(text); b->setObjectName("secondaryButton"); return b;
}
QWidget *makeScrollable(QWidget *body) {
    auto *area = new QScrollArea; area->setWidgetResizable(true); area->setWidget(body);
    QPalette palette=body->palette();palette.setColor(QPalette::Window,QColor("#101827"));
    palette.setColor(QPalette::Base,QColor("#142237"));palette.setColor(QPalette::Text,QColor("#edf3fc"));
    palette.setColor(QPalette::WindowText,QColor("#edf3fc"));
    body->setPalette(palette);body->setAutoFillBackground(true);area->setPalette(palette);
    return area;
}
QString text(const std::string &s) { return QString::fromStdString(s); }
}
ArmPage::ArmPage(QWidget *parent) : QWidget(parent), config_(arm::ArmConfig::defaults()) {
    setObjectName("armPage"); clock_.start();
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto *title = new QLabel(QStringLiteral("机械臂控制")); title->setObjectName("pageTitle"); layout->addWidget(title);
    auto *connection = new QHBoxLayout;
    mode_ = new QComboBox; mode_->addItems({QStringLiteral("模拟"),QStringLiteral("硬件 GPIO")});
    mode_->setObjectName("armMode");
    lowChip_ = new QLineEdit("/dev/gpiochip2"); highChip_ = new QLineEdit("/dev/gpiochip3");
    lowChip_->setPlaceholderText("GPIO 低32位"); highChip_->setPlaceholderText("GPIO 高32位");
    lowChip_->setMaximumWidth(165); highChip_->setMaximumWidth(165);
    connection->addWidget(mode_); connection->addWidget(lowChip_); connection->addWidget(highChip_);
    auto *connectButton = button(QStringLiteral("连接")); connection->addWidget(connectButton); layout->addLayout(connection);
    auto *commands = new QHBoxLayout;
    auto *enableButton = enableButton_ = button(QStringLiteral("校准姿态启用"));
    enableButton->setObjectName("armEnable");
    debugButton_ = button(QStringLiteral("全范围调试使能"));debugButton_->setObjectName("armDebugEnable");
    auto *stopButton = button(QStringLiteral("停止并保持"));
    stopButton->setObjectName("armStop");
    auto *disableButton = disableButton_ = button(QStringLiteral("回位后关闭 PWM")); disableButton->setObjectName("dangerButton");
    disableButton->setProperty("armOff",true);
    disableButton->setToolTip(QStringLiteral("按复位姿态缓慢回位后关闭输出；回位期间再次点击立即关闭 PWM。停止并保持可取消回位。"));
    commands->addWidget(enableButton);commands->addWidget(debugButton_);commands->addWidget(stopButton);commands->addWidget(disableButton);
    layout->addLayout(commands);
    status_ = new QLabel; status_->setObjectName("armStatus");status_->setWordWrap(true); layout->addWidget(status_);
    position_ = new QLabel; position_->setWordWrap(true); layout->addWidget(position_);
    error_ = new QLabel; error_->setObjectName("armError");error_->setWordWrap(true); error_->setStyleSheet("color:#ffbd9e;"); layout->addWidget(error_);
    auto *tabs = tabs_ = new QTabWidget;tabs->setObjectName("armTabs");layout->addWidget(tabs,1);

    auto *manual = new QWidget; auto *ml = new QVBoxLayout(manual);
    auto *grid = new QGridLayout; grid->addWidget(new QLabel(QStringLiteral("逻辑轴")),0,0);
    grid->addWidget(new QLabel(QStringLiteral("目标角度 °")),0,1); grid->addWidget(new QLabel(QStringLiteral("目标脉宽 μs")),0,2);
    for (int axis=0;axis<6;++axis) {
        const QStringList names={QStringLiteral("夹爪 · Y12"),QStringLiteral("腕旋 · T9"),QStringLiteral("腕俯仰 · T5"),QStringLiteral("肘 · V6"),QStringLiteral("肩 · U7"),QStringLiteral("底座 · U9")};
        grid->addWidget(new QLabel(QStringLiteral("%1 %2").arg(axis+1).arg(names[axis])),axis+1,0);
        angles_[axis]=number(-180,180,0); pulses_[axis]=number(500,2499.2,1500,1);
        angles_[axis]->setObjectName(QString("armAngle%1").arg(axis));
        pulses_[axis]->setObjectName(QString("armPulse%1").arg(axis));pulses_[axis]->setSingleStep(7.84);
        grid->addWidget(angles_[axis],axis+1,1); grid->addWidget(pulses_[axis],axis+1,2);
    }
    ml->addLayout(grid);
    auto *timing = new QHBoxLayout; duration_=new QSpinBox; duration_->setRange(20,60000); duration_->setValue(1000);
    hold_=new QSpinBox; hold_->setRange(0,60000); hold_->setValue(0);
    timing->addWidget(new QLabel(QStringLiteral("运动 ms"))); timing->addWidget(duration_);
    timing->addWidget(new QLabel(QStringLiteral("动作停留 ms"))); timing->addWidget(hold_); ml->addLayout(timing);
    auto *moveButtons=new QHBoxLayout;
    auto *angleMove=button(QStringLiteral("按角度运动")); auto *pulseMove=button(QStringLiteral("按脉宽运动"));
    pulseMoveButton_=pulseMove;pulseMove->setObjectName("armMovePulses");duration_->setObjectName("armDuration");
    auto *home=button(QStringLiteral("复位姿态")); moveButtons->addWidget(angleMove);moveButtons->addWidget(pulseMove);moveButtons->addWidget(home);
    angleMove->setObjectName("armMoveAngles");home->setObjectName("armHome");
    normalMotionControls_={angleMove,home};
    for(auto *field:angles_)normalMotionControls_.push_back(field);
    ml->addLayout(moveButtons);
    auto *special=new QHBoxLayout;claw_=number(-90,90,0);roll_=number(-90,90,0);
    auto *clawMove=button(QStringLiteral("夹爪"));auto *rollMove=button(QStringLiteral("腕部旋转"));
    clawMove->setObjectName("armClawMove");claw_->setObjectName("armClawOpen");
    rollMove->setObjectName("armRollMove");roll_->setObjectName("armRollAngle");
    normalMotionControls_.insert(normalMotionControls_.end(),{claw_,roll_,clawMove,rollMove});
    special->addWidget(claw_);special->addWidget(clawMove);special->addWidget(roll_);special->addWidget(rollMove);ml->addLayout(special);
    auto *manualNote=new QLabel(QStringLiteral("全范围调试：确认六轴初始脉宽后使能，六轴均可输出500～2499.2 μs，角度范围0～180°；角度、复位、坐标和动作组可用。此模式尚未校准机械限位，六路同时输出且无位置反馈，请现场观察。"));
    manualNote->setWordWrap(true);ml->addWidget(manualNote);ml->addStretch();tabs->addTab(makeScrollable(manual),QStringLiteral("关节"));

    auto *coords=new QWidget;auto *cl=new QVBoxLayout(coords);auto *cf=new QFormLayout;
    const QStringList labels={QStringLiteral("X 前方 cm"),QStringLiteral("Y 左方 cm"),QStringLiteral("Z 高度 cm"),QStringLiteral("末端俯仰 °")};
    for(int i=0;i<4;++i){coordinate_[i]=number(i==3?-180:-60,i==3?360:60,0);coordinate_[i]->setObjectName(QString("armCoordinate%1").arg(i));cf->addRow(labels[i],coordinate_[i]);}
    coordinate_[0]->setValue(10);coordinate_[2]->setValue(15);coordinate_[3]->setValue(-45);cl->addLayout(cf);
    auto *coordButtons=new QHBoxLayout;auto *preview=button(QStringLiteral("逆解预览"));auto *coordMove=button(QStringLiteral("移动到坐标"));
    preview->setObjectName("armCoordinatePreview");coordMove->setObjectName("armCoordinateMove");
    normalMotionControls_.push_back(coordMove);
    auto *addCoordinate=button(QStringLiteral("加入动作组"));addCoordinate->setObjectName("armActionAddCoordinate");
    normalMotionControls_.push_back(addCoordinate);
    coordButtons->addWidget(preview);coordButtons->addWidget(coordMove);coordButtons->addWidget(addCoordinate);cl->addLayout(coordButtons);
    coordinatePreview_=new QLabel(QStringLiteral("先预览再运动；逆解会经正解回算检查。"));coordinatePreview_->setWordWrap(true);cl->addWidget(coordinatePreview_);
    auto *originButtons=new QHBoxLayout;
    auto *zeroTip=button(QStringLiteral("以当前尖端设零点"));zeroTip->setObjectName("armCoordinateZero");
    auto *baseFrame=button(QStringLiteral("恢复基坐标"));baseFrame->setObjectName("armCoordinateBase");
    originButtons->addWidget(zeroTip);originButtons->addWidget(baseFrame);cl->addLayout(originButtons);
    coordinateOrigin_=new QLabel;coordinateOrigin_->setObjectName("armCoordinateOrigin");
    coordinateOrigin_->setWordWrap(true);cl->addWidget(coordinateOrigin_);
    auto *coordNote=new QLabel(QStringLiteral("坐标单位 cm；常规硬件运动须确认实测连杆长度，全范围调试使用当前配置的几何尺寸。未实现碰撞检测。"));
    coordNote->setWordWrap(true);cl->addWidget(coordNote);cl->addStretch();tabs->addTab(makeScrollable(coords),QStringLiteral("坐标"));

    auto *actions=new QWidget;auto *al=new QVBoxLayout(actions);
    actionTable_=new QTableWidget(0,3);actionTable_->setHorizontalHeaderLabels({QStringLiteral("角度 / 相对坐标目标"),QStringLiteral("运动 ms"),QStringLiteral("停留 ms")});
    actionTable_->setObjectName("armActionTable");
    actionTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);actionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    actionTable_->setSelectionBehavior(QAbstractItemView::SelectRows);al->addWidget(actionTable_);
    auto *arow=new QHBoxLayout;auto *record=button(QStringLiteral("录制当前命令"));auto *remove=button(QStringLiteral("删除帧"));auto *play=button(QStringLiteral("播放"));
    record->setObjectName("armActionRecord");play->setObjectName("armActionPlay");
    normalMotionControls_.insert(normalMotionControls_.end(),{record,play});
    arow->addWidget(record);arow->addWidget(remove);arow->addWidget(play);al->addLayout(arow);
    auto *files=new QHBoxLayout;auto *loadActions=button(QStringLiteral("载入 JSON"));auto *saveActions=button(QStringLiteral("保存 JSON"));
    files->addWidget(loadActions);files->addWidget(saveActions);al->addLayout(files);
    auto *anote=new QLabel(QStringLiteral("录制当前命令保存六轴角度；坐标页可将目标加入动作组，播放时逐帧逆解。坐标帧保留原点及映射模式，改变后需重新记录。运动/停留时间取关节页；失败立即取消后续帧。"));
    anote->setWordWrap(true);al->addWidget(anote);tabs->addTab(actions,QStringLiteral("动作组"));

    auto *cal=new QWidget;auto *calLayout=new QVBoxLayout(cal);
    configPath_=new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/arm_config.conf");
    configPath_->setObjectName("armConfigPath");
    calLayout->addWidget(configPath_);
    configTable_=new QTableWidget(6,10);
    configTable_->setObjectName("armCalibrationTable");
    configTable_->setHorizontalHeaderLabels({QStringLiteral("名称"),QStringLiteral("通道0–5"),QStringLiteral("方向±1"),QStringLiteral("最小°"),QStringLiteral("最大°"),QStringLiteral("最小μs"),QStringLiteral("最大μs"),QStringLiteral("偏移μs"),QStringLiteral("复位°"),QStringLiteral("已实测")});
    configTable_->setMinimumHeight(250);configTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);calLayout->addWidget(configTable_);
    calibrationConfirmed_=new QCheckBox(QStringLiteral("六轴方向、机械限位和脉宽范围均已逐轴实测确认"));calibrationConfirmed_->setObjectName("armCalibrationConfirmed");calLayout->addWidget(calibrationConfirmed_);
    auto *gl=new QFormLayout;const QStringList geometryLabels={QStringLiteral("底座高度 cm"),QStringLiteral("上臂长度 cm"),QStringLiteral("前臂长度 cm"),QStringLiteral("工具长度 cm")};
    for(int i=0;i<4;++i){geometry_[i]=number(.01,1000,1,3);geometry_[i]->setObjectName(QString("armGeometry%1").arg(i));gl->addRow(geometryLabels[i],geometry_[i]);}calLayout->addLayout(gl);
    geometryConfirmed_=new QCheckBox(QStringLiteral("上述几何尺寸已在本机实测，允许硬件坐标运动"));geometryConfirmed_->setObjectName("armGeometryConfirmed");calLayout->addWidget(geometryConfirmed_);
    auto *saveRow=new QHBoxLayout;auto *loadConfigButton=button(QStringLiteral("载入配置"));auto *saveConfigButton=button(QStringLiteral("保存并应用配置"));saveConfigButton->setObjectName("armSaveConfig");saveRow->addWidget(loadConfigButton);saveRow->addWidget(saveConfigButton);calLayout->addLayout(saveRow);
    auto *calRow=new QHBoxLayout;calibrationAxis_=new QComboBox;for(int i=0;i<6;++i)calibrationAxis_->addItem(QStringLiteral("轴 %1").arg(i+1));
    calibrationPulse_=number(500,2499.2,1500,1);auto *test=button(QStringLiteral("确认并测试本轴脉宽"));calRow->addWidget(calibrationAxis_);calRow->addWidget(calibrationPulse_);calRow->addWidget(test);calLayout->addLayout(calRow);
    calibrationTestButton_=test;
    auto *calNote=new QLabel(QStringLiteral("测试使用已保存配置和关节页运动时间，保持其余五轴最后命令。PL 是六轴全局启用，首次测试前必须输入并确认全部六轴初始脉宽。修改映射/方向后先保存，硬件连接会释放。"));calNote->setWordWrap(true);calLayout->addWidget(calNote);
    tabs->addTab(makeScrollable(cal),QStringLiteral("校准"));

    connect(connectButton,&QPushButton::clicked,this,[this](){resetSession(mode_->currentIndex()==1);});
    connect(lowChip_,&QLineEdit::textEdited,this,[this](){releaseSession();refreshStatus();});
    connect(highChip_,&QLineEdit::textEdited,this,[this](){releaseSession();refreshStatus();});
    connect(mode_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){releaseSession();refreshStatus();});
    connect(enableButton,&QPushButton::clicked,this,[this](){enable();});
    connect(debugButton_,&QPushButton::clicked,this,[this](){enablePulseDebug();});
    connect(stopButton,&QPushButton::clicked,this,[this](){cancelActions();report(QString());refreshStatus();});
    connect(disableButton,&QPushButton::clicked,this,[this](){closePwm();});
    connect(angleMove,&QPushButton::clicked,this,[this](){moveManual(false);});
    connect(pulseMove,&QPushButton::clicked,this,[this](){moveManual(true);});
    connect(home,&QPushButton::clicked,this,[this](){if(!ready())return;cancelActions();if(!robot_->home(duration_->value(),clock_.elapsed()))report(text(robot_->error()));else setHomeTargetControls();refreshStatus();});
    connect(clawMove,&QPushButton::clicked,this,[this](){if(!ready())return;cancelActions();if(!robot_->moveClaw(claw_->value(),duration_->value(),clock_.elapsed()))report(text(robot_->error()));refreshStatus();});
    connect(rollMove,&QPushButton::clicked,this,[this](){if(!ready())return;cancelActions();if(!robot_->moveRoll(roll_->value(),duration_->value(),clock_.elapsed()))report(text(robot_->error()));refreshStatus();});
    const auto previewCoordinate=[this](){
        if(!robot_){report(QStringLiteral("请先连接模拟或硬件后端"));return;}
        auto result=robot_->previewCoordinate(arm::CartesianPose(coordinate_[0]->value(),coordinate_[1]->value(),coordinate_[2]->value(),coordinate_[3]->value()));
        if(!result.ok){coordinatePreview_->setText(text(result.error));return;}
        coordinatePreview_->setText(QStringLiteral("回算：(%1, %2, %3) cm，俯仰 %4°；位置误差 %5 cm。可达解预览不代表路径无碰撞。")
            .arg(result.achieved.xCm,0,'f',3).arg(result.achieved.yCm,0,'f',3).arg(result.achieved.zCm,0,'f',3).arg(result.achieved.pitchDeg,0,'f',2).arg(result.positionErrorCm,0,'g',4));
    };
    connect(preview,&QPushButton::clicked,this,previewCoordinate);
    connect(addCoordinate,&QPushButton::clicked,this,[this](){
        if(!controller_||!robot_){report(QStringLiteral("请先连接后端"));return;}
        if(controller_->moving()||player_.active()){report(QStringLiteral("请在运动和动作组停止后添加坐标帧"));return;}
        const auto frame=robot_->cartesianFrame();
        if(!frame.enabled||!robot_->cartesianFrameCompatible()){report(QStringLiteral("请先设置尖端原点并使用对应映射模式"));return;}
        if(actions_.frames.size()>=1000){report(QStringLiteral("动作最多1000帧"));return;}
        QString mappingError;if(!bindActionMapping(mappingError)){report(mappingError);return;}
        if(!actions_.matchesCoordinateFrame(frame)){report(QStringLiteral("已有坐标帧使用另一原点，请重新载入或删除旧坐标帧"));return;}
        ArmActionFrame action;action.kind=ArmActionFrame::Kind::Coordinate;
        for(int i=0;i<4;++i)action.coordinate[i]=coordinate_[i]->value();
        const arm::CartesianPose target(action.coordinate[0],action.coordinate[1],action.coordinate[2],action.coordinate[3]);
        const auto preview=robot_->previewCoordinate(target);
        if(!preview.ok){report(text(preview.error));return;}
        action.angles=controller_->currentAngles();action.durationMs=duration_->value();action.holdMs=hold_->value();
        actions_.coordinateFrame=frame;actions_.frames.append(action);refreshActions();report(QString());
    });
    connect(zeroTip,&QPushButton::clicked,this,[this](){QString error;if(!setCoordinateOrigin(true,error))report(error);else report(QString());refreshStatus();});
    connect(baseFrame,&QPushButton::clicked,this,[this](){QString error;if(!setCoordinateOrigin(false,error))report(error);else report(QString());refreshStatus();});
    connect(coordMove,&QPushButton::clicked,this,[this,previewCoordinate](){if(!ready())return;cancelActions();previewCoordinate();if(!robot_->moveCoordinate(arm::CartesianPose(coordinate_[0]->value(),coordinate_[1]->value(),coordinate_[2]->value(),coordinate_[3]->value()),duration_->value(),clock_.elapsed()))report(text(robot_->error()));refreshStatus();});
    connect(record,&QPushButton::clicked,this,[this](){
        if(!controller_){report(QStringLiteral("请先连接"));return;}if(controller_->moving()||player_.active()){report(QStringLiteral("请在运动完成或停止后录制"));return;}
        if(actions_.frames.size()>=1000){report(QStringLiteral("动作最多1000帧"));return;}
        QString mappingError;if(!bindActionMapping(mappingError)){report(mappingError);return;}
        ArmActionFrame frame;frame.angles=controller_->currentAngles();frame.durationMs=duration_->value();frame.holdMs=hold_->value();actions_.frames.append(frame);refreshActions();
    });
    connect(remove,&QPushButton::clicked,this,[this](){cancelActions();int row=actionTable_->currentRow();if(row>=0&&row<actions_.frames.size()){actions_.frames.remove(row);refreshActions();}});
    connect(play,&QPushButton::clicked,this,[this](){
        if(!ready())return;
        QString error;ArmActions validated;
        if(!validateActions(validated,error)){report(error);return;}
        startActions(validated);report(QString());
    });
    connect(loadActions,&QPushButton::clicked,this,[this](){
        cancelActions();if(controller_)controller_->stop();
        QString path=QFileDialog::getOpenFileName(this,QStringLiteral("载入动作组"),QString(),"JSON (*.json)");if(path.isEmpty())return;
        QString error;if(!loadActionsFromPath(path,error))report(error);else report(QString());
    });
    connect(saveActions,&QPushButton::clicked,this,[this](){
        cancelActions();if(controller_)controller_->stop();
        QString error;ArmActions validated;if(!validated.load(actions_.json(),minimumAngles(),maximumAngles(),error)){report(error);return;}
        const QString directory=actionFile_.isEmpty()?QDir::currentPath()+QStringLiteral("/actions"):QFileInfo(actionFile_).absolutePath();
        QFileDialog dialog(this,QStringLiteral("保存动作组"),directory,QStringLiteral("JSON (*.json)"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setDefaultSuffix(QStringLiteral("json"));
        if(dialog.exec()!=QDialog::Accepted||dialog.selectedFiles().isEmpty())return;
        const QString path=dialog.selectedFiles().first();
        if(!validated.save(path,error))report(error);
        else{actionFile_=QFileInfo(path).absoluteFilePath();report(QString());}
    });
    connect(loadConfigButton,&QPushButton::clicked,this,[this](){loadConfig();});
    connect(saveConfigButton,&QPushButton::clicked,this,[this](){saveConfig();});
    connect(configTable_,&QTableWidget::cellChanged,this,[this](int row,int column){
        if(column==0)return;
        calibrationConfirmed_->setChecked(false);
        if(column!=9){const QSignalBlocker block(configTable_);configTable_->item(row,9)->setCheckState(Qt::Unchecked);}
    });
    for(auto *field:geometry_)connect(field,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this](double){geometryConfirmed_->setChecked(false);});
    connect(test,&QPushButton::clicked,this,[this](){
        if(!controller_){report(QStringLiteral("请先连接"));return;}
        cancelActions();controller_->stop();
        if(!confirmBaseline())return;
        const int axis=calibrationAxis_->currentIndex();
        if(QMessageBox::question(this,QStringLiteral("本轴校准测试"),QStringLiteral("确认轴 %1 的 %2 μs 在当前可运行范围内？其余轴保持最后命令。启用后六轴都会收到PWM。")
           .arg(axis+1).arg(calibrationPulse_->value()),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
        cancelActions();if(!controller_->calibrateAxis(unsigned(axis),calibrationPulse_->value(),duration_->value(),clock_.elapsed(),true))report(text(controller_->error()));else report(QString());refreshStatus();
    });
    for(int axis=0;axis<6;++axis) {
        connect(angles_[axis],QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,axis](double){editTarget(axis,false);});
        connect(pulses_[axis],QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,axis](double){editTarget(axis,true);});
    }
    QString initialLoadError;
    if(QFile::exists(configPath_->text())){
        arm::ArmConfig saved;std::string failure;
        if(arm::ArmConfig::load(configPath_->text().toStdString(),saved,failure))config_=saved;
        else initialLoadError=QStringLiteral("保存配置载入失败，保持未确认默认配置：")+text(failure);
    }
    showConfig();setControlsFromConfig();resetSession(false);if(!initialLoadError.isEmpty())report(initialLoadError);
    conveyor_=new ConveyorPage([this](const QJsonObject &command){return handleConsoleCommand(command);});
    tabs_->addTab(conveyor_,QStringLiteral("传送带联动"));
    timer_=new QTimer(this);connect(timer_,&QTimer::timeout,this,[this](){tick();});timer_->start(20);
}
ArmPage::~ArmPage(){delete conveyor_;conveyor_=nullptr;releaseSession();}
void ArmPage::showConveyorTab(){tabs_->setCurrentIndex(4);}
void ArmPage::showJointTab(){tabs_->setCurrentIndex(0);}
void ArmPage::hideEvent(QHideEvent *event){cancelActions();refreshStatus();QWidget::hideEvent(event);}
void ArmPage::releaseSession(){cancelActions();robot_.reset();controller_.reset();backend_.reset();targetControlsSynced_=false;}
void ArmPage::resetSession(bool hardware){
    // A repeated connect is a status refresh, never a teardown or PWM command.
    if(controller_&&backend_&&backend_->hardware()==hardware){refreshStatus();return;}
    releaseSession();report(QString());
    if(hardware)backend_.reset(new arm::GPIOBackend(lowChip_->text().toStdString(),highChip_->text().toStdString()));
    else backend_.reset(new arm::MockBackend);
    controller_.reset(new arm::Controller(*backend_,config_));
    if(!controller_->initialize()){report(text(controller_->error()));controller_.reset();backend_.reset();refreshStatus();return;}
    robot_.reset(new arm::RobotArm(*controller_,!hardware));
    if(!hardware)controller_->acknowledgeInitialPulses(config_.homePulses(),true);
    setCoordinateControlsFromFrame();
    refreshStatus();
}
void ArmPage::report(const QString &error){error_->setText(error);}
void ArmPage::cancelActions(){
    returningBeforeOff_=false;returnSettledAt_=-1;
    if(sortJobState_=="running")sortJobState_="interrupted";
    if(actionJobState_=="running")actionJobState_="interrupted";
    player_.stop();if(controller_)controller_->stop();
}
void ArmPage::startActions(const ArmActions &actions){
    cancelActions();player_.play(actions);++actionJobId_;actionJobState_="running";
}
bool ArmPage::confirmBaseline(){
    if(!controller_)return false;
    if(controller_->initialPoseAcknowledged())return true;
    arm::Pulses baseline;QStringList values;
    for(int i=0;i<6;++i){baseline[i]=pulses_[i]->value();values<<QStringLiteral("轴%1：%2 μs").arg(i+1).arg(baseline[i],0,'f',1);}
    if(backend_->hardware()&&QMessageBox::question(this,QStringLiteral("确认全部六轴初始命令"),QStringLiteral("当前无位置反馈，启用可能立即移动六个关节。已核对接线、姿态与机械限位，确认下列初始脉宽？\n")+values.join("\n"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return false;
    if(!controller_->acknowledgeInitialPulses(baseline,true)){report(text(controller_->error()));return false;}return true;
}
void ArmPage::enable(){
    if(!controller_){report(QStringLiteral("请先连接后端"));return;}if(!confirmBaseline())return;
    if(!controller_->enable())report(text(controller_->error()));else report(QString());refreshStatus();
}
void ArmPage::enablePulseDebug(){
    if(!controller_){report(QStringLiteral("请先连接后端"));return;}
    if(controller_->enabled()){report(QStringLiteral("请先关闭 PWM，再确认调试初始姿态"));return;}
    const auto baseline=debugHomePulses();QStringList values;
    for(int i=0;i<6;++i)values<<QStringLiteral("%1：%2 μs").arg(text(config_.axes[i].name)).arg(baseline[i],0,'f',1);
    if(QMessageBox::question(this,QStringLiteral("确认六轴全范围调试"),
       QStringLiteral("将直接使用“复位姿态”的六路命令使能并保持 PWM 开启；输入框中其他目标不会用于本次使能。确认下面六路脉宽？使能会同时驱动六个关节，可能立即移动；当前无位置反馈，请由现场人员观察并准备关闭电源。\n\n")+values.join("\n")+
       QStringLiteral("\n\n本次开放六轴500～2499.2 μs、0～180°，允许角度、复位、坐标及动作组。机械限位尚未校准，配置确认标志保持不变。"),
       QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    cancelActions();
    if(!controller_->enablePulseDebug(baseline,true))report(text(controller_->error()));
    else{
        refreshStatus();
        setHomeTargetControls();
        report(QString());
    }
    refreshStatus();
}
void ArmPage::closePwm(){
    if(!controller_){report(QStringLiteral("请先连接后端"));return;}
    const bool immediate=returningBeforeOff_||!controller_->enabled()||controllerLocked(controller_.get(),0);
    cancelActions();report(QString());
    if(immediate){
        if(!controller_->disable())report(text(controller_->error()));
    }else if(!robot_->home(unsigned(std::max(returnMinimumMs,duration_->value())),uint64_t(clock_.elapsed()))){
        report(text(robot_->error()));
    }else{
        setHomeTargetControls();
        returningBeforeOff_=true;
    }
    refreshStatus();
}
void ArmPage::setHomeTargetControls(){
    syncTargetControls(controller_->homePulses());
}
void ArmPage::syncTargetControls(const arm::Pulses &target){
    const auto mapping=controller_?controller_->motionConfig():config_;
    const auto angles=mapping.pulsesToAngles(target);
    for(int i=0;i<6;++i){
        const QSignalBlocker pulseBlock(pulses_[i]),angleBlock(angles_[i]);
        pulses_[i]->setValue(target[i]);angles_[i]->setValue(angles[i]);
    }
}
void ArmPage::editTarget(int axis,bool pulses){
    const auto mapping=controller_?controller_->motionConfig():config_;
    auto target=mapping.homePulses();
    if(pulses) {
        target[axis]=pulses_[axis]->value();
        const QSignalBlocker block(angles_[axis]);
        angles_[axis]->setValue(mapping.pulsesToAngles(target)[axis]);
    }else{
        auto angles=mapping.pulsesToAngles(target);angles[axis]=angles_[axis]->value();
        std::string error;
        if(!mapping.anglesToPulses(angles,target,error))return;
        const QSignalBlocker block(pulses_[axis]);pulses_[axis]->setValue(target[axis]);
    }
}
bool ArmPage::ready(){
    if(!controller_||!controller_->enabled()){report(QStringLiteral("请先连接后端并确认初始姿态后启用"));return false;}report(QString());return true;
}
void ArmPage::moveManual(bool pulses){
    QJsonArray angles, pulseValues, editorText;
    for(int i=0;i<6;++i){angles.append(angles_[i]->value());pulseValues.append(pulses_[i]->value());editorText.append(angles_[i]->text());}
    motionTrace("manual-click",{{"ms",clock_.elapsed()},{"input",pulses?"pulses":"angles"},{"angles",angles},{"pulses",pulseValues},{"editorText",editorText},
        {"enabled",controller_&&controller_->enabled()},{"locked",controller_&&controllerLocked(controller_.get(),0)}});
    if(!ready()){motionTrace("manual-rejected",{{"ms",clock_.elapsed()},{"error",error_->text()}});return;}
    cancelActions();bool ok;
    if(pulses){arm::Pulses pose;for(int i=0;i<6;++i)pose[i]=pulses_[i]->value();ok=controller_->pulseDebugEnabled()?controller_->moveDebugPulses(pose,duration_->value(),clock_.elapsed()):controller_->movePulses(pose,duration_->value(),clock_.elapsed());}
    else{arm::Pose pose;for(int i=0;i<6;++i)pose[i]=angles_[i]->value();ok=controller_->moveAngles(pose,duration_->value(),clock_.elapsed());}
    if(!ok)report(text(controller_->error()));
    motionTrace("manual-scheduled",{{"ms",clock_.elapsed()},{"ok",ok},{"moving",controller_->moving()},
        {"current",pulseTrace(controller_->currentPulses())},{"endpoint",pulseTrace(controller_->targetPulses())},{"error",text(controller_->error())}});
    refreshStatus();
}
void ArmPage::tick(){
    if(!controller_)return;
    const qint64 now=clock_.elapsed();
    if(controller_->enabled()&&lastUiTickMs_>=0&&now-lastUiTickMs_>150)
        motionTrace("tick-gap",{{"ms",now},{"gapMs",now-lastUiTickMs_}});
    lastUiTickMs_=now;
    const bool wasMoving=controller_->moving();
    if(!controller_->tick(now)){const QString failure=text(controller_->error());motionTrace("tick-failed",{{"ms",clock_.elapsed()},{"error",failure},{"current",pulseTrace(controller_->currentPulses())}});cancelActions();report(failure);refreshStatus();return;}
    if(wasMoving&&!controller_->moving())motionTrace("motion-completed",{{"ms",clock_.elapsed()},{"current",pulseTrace(controller_->currentPulses())}});
    if(returningBeforeOff_){
        if(!controller_->enabled()||controllerLocked(controller_.get(),0)){
            const QString failure=text(controller_->error());cancelActions();report(failure);
        }else if(!controller_->moving()){
            // The final PWM command has been submitted, not measured at the shaft.
            // Keep the event loop and heartbeats running during a short settling hold.
            const qint64 now=clock_.elapsed();
            if(returnSettledAt_<0)returnSettledAt_=now;
            if(now-returnSettledAt_>=returnSettleMs){
                cancelActions();
                if(!controller_->disable())report(text(controller_->error()));
            }
        }
        refreshStatus();return;
    }
    player_.tick(clock_.elapsed(),controller_->moving(),text(controller_->error()),[this](const ArmActionFrame &frame,QString &error){
        if(frame.kind==ArmActionFrame::Kind::Coordinate) {
            if(!robot_->cartesianFrameCompatible()||!actions_.matchesCoordinateFrame(robot_->cartesianFrame())) {
                error=QStringLiteral("坐标动作原点或映射已变化");return false;
            }
            const arm::CartesianPose target(frame.coordinate[0],frame.coordinate[1],frame.coordinate[2],frame.coordinate[3]);
            const bool ok=robot_->moveCoordinateWithTools(target,frame.angles[0],frame.angles[1],unsigned(frame.durationMs),uint64_t(clock_.elapsed()));
            if(!ok)error=text(robot_->error());
            return ok;
        }
        const bool ok=controller_->moveAngles(frame.angles,uint64_t(frame.durationMs),uint64_t(clock_.elapsed()));
        if(!ok)error=text(controller_->error());
        return ok;
    });
    if(!player_.error().isEmpty())report(player_.error());
    if(sortJobState_=="running"&&!player_.active())sortJobState_=player_.error().isEmpty()?"completed":"failed";
    if(actionJobState_=="running"&&!player_.active())actionJobState_=player_.error().isEmpty()?"completed":"failed";
    refreshStatus();
}
void ArmPage::refreshStatus(){
    const bool debug=controller_&&controller_->pulseDebugEnabled();
    bool calibrated=config_.calibrationConfirmed;for(const auto &axis:config_.axes)calibrated=calibrated&&axis.calibrated;
    const bool normalAllowed=debug||!backend_||!backend_->hardware()||calibrated;
    const bool locked=controller_&&controllerLocked(controller_.get(),0);
    tabs_->setEnabled(!returningBeforeOff_);
    disableButton_->setText(returningBeforeOff_?QStringLiteral("立即关闭 PWM"):
                           locked?QStringLiteral("关闭 PWM 并解锁"):QStringLiteral("回位后关闭 PWM"));
    enableButton_->setEnabled(controller_&&normalAllowed&&!debug&&!locked&&!returningBeforeOff_);
    debugButton_->setEnabled(controller_&&!controller_->enabled()&&!locked);
    pulseMoveButton_->setEnabled(controller_&&controller_->enabled()&&(debug||normalAllowed));
    calibrationTestButton_->setEnabled(bool(controller_));
    for(auto *control:normalMotionControls_)control->setEnabled(normalAllowed);
    if(duration_->minimum()!=20)duration_->setMinimum(20);
    for(int i=0;i<6;++i){
        const double minimum=debug?controller_->debugMinPulses()[i]:(!calibrated?arm::PlMinUs:config_.axes[i].minPulseUs);
        const double maximum=debug?controller_->debugMaxPulses()[i]:(!calibrated?arm::PlMaxUs:config_.axes[i].maxPulseUs);
        const QSignalBlocker pulseBlock(pulses_[i]),angleBlock(angles_[i]);
        if(pulses_[i]->minimum()!=minimum||pulses_[i]->maximum()!=maximum)pulses_[i]->setRange(minimum,maximum);
        const double minAngle=debug?0:config_.axes[i].minAngleDeg,maxAngle=debug?180:config_.axes[i].maxAngleDeg;
        if(angles_[i]->minimum()!=minAngle||angles_[i]->maximum()!=maxAngle)angles_[i]->setRange(minAngle,maxAngle);
    }
    // Refresh editors only for a new command, never on each interpolation tick.
    // This also updates targets after action/coordinate/tool commands without
    // overwriting a pending manual edit during the 20 ms status refresh.
    if(controller_&&controller_->enabled()&&controller_->initialPoseAcknowledged()){
        const auto target=controller_->targetPulses();
        if(!targetControlsSynced_||target!=lastCommandTarget_||debug!=lastTargetDebug_){
            syncTargetControls(target);lastCommandTarget_=target;lastTargetDebug_=debug;targetControlsSynced_=true;
        }
    }else targetControlsSynced_=false;
    const QString mode=backend_?(backend_->hardware()?QStringLiteral("硬件 GPIO"):QStringLiteral("模拟（不输出GPIO）")):QStringLiteral("未连接");
    status_->setText(QStringLiteral("%1 · PWM %2 · %3 · 动作 %4/%5 · %6 · 位置无反馈")
        .arg(mode).arg(controller_&&controllerLocked(controller_.get(),0)?
                      (controllerStatusAvailable(controller_.get(),0)?
                       (controllerOutputActive(controller_.get(),0)?QStringLiteral("保持中，需手动关闭后使能"):QStringLiteral("已锁定，需手动关闭后使能")):
                       QStringLiteral("状态未知，控制已锁定")):
                      (controller_&&controller_->enabled()?QStringLiteral("启用"):QStringLiteral("关闭")))
        .arg(returningBeforeOff_?(controller_&&controller_->moving()?QStringLiteral("缓慢回位中，完成后关闭 PWM"):QStringLiteral("复位停留，准备关闭 PWM")):
             (controller_&&controller_->moving()?QStringLiteral("运动中"):QStringLiteral("停止/保持")))
        .arg(player_.active()?player_.index()+1:0).arg(actions_.frames.size())
        .arg(debug?QStringLiteral("全范围调试500～2499.2 μs · 未校准"):QStringLiteral("常规控制")));
    if(!controller_){position_->clear();coordinateOrigin_->setText(config_.cartesianFrame.enabled?
        QStringLiteral("尖端原点已保存，请连接后端并使用记录时的映射模式。"):
        QStringLiteral("使用基坐标；连接并保持姿态后可设置尖端原点。"));return;}
    QStringList values;const auto pulses=controller_->currentPulses();
    for(int i=0;i<6;++i)values<<QString::number(pulses[i],'f',1);
    position_->setText(QStringLiteral("最后命令 μs：")+values.join(" / "));
    const auto &frame=config_.cartesianFrame;
    if(frame.enabled) {
        QString message=QStringLiteral("尖端原点已保存（%1）：基坐标 (%2, %3, %4) cm。X向前、Y向左、Z向上。")
            .arg(frame.debugMapping?QStringLiteral("全范围调试"):QStringLiteral("常规映射"))
            .arg(frame.originCm[0],0,'f',3).arg(frame.originCm[1],0,'f',3).arg(frame.originCm[2],0,'f',3);
        if(robot_&&robot_->cartesianFrameCompatible()&&controller_->initialPoseAcknowledged()) {
            const auto p=robot_->currentCartesian();
            message+=QStringLiteral("\n当前尖端命令坐标：(%1, %2, %3) cm，俯仰 %4°。")
                .arg(p.xCm,0,'f',3).arg(p.yCm,0,'f',3).arg(p.zCm,0,'f',3).arg(p.pitchDeg,0,'f',2);
        } else message+=QStringLiteral("\n请使用记录原点时的映射模式并确认初始姿态。关联位置由命令推算，无实际位置反馈。");
        coordinateOrigin_->setText(message);
    } else coordinateOrigin_->setText(QStringLiteral("使用基坐标；可在使能且停止后将当前尖端设为原点。设零不驱动舵机。"));
}
void ArmPage::setCoordinateControlsFromFrame(){
    if(!config_.cartesianFrame.enabled)return;
    for(int i=0;i<3;++i)coordinate_[i]->setValue(0);
    coordinate_[3]->setValue(config_.cartesianFrame.pitchDeg);
}
bool ArmPage::setCoordinateOrigin(bool enabled,QString &error){
    if(!controller_||!robot_){error=QStringLiteral("请先连接后端");return false;}
    if(controller_->moving()||player_.active()){error=QStringLiteral("请在运动和动作组停止后设置坐标原点");return false;}
    const auto previous=robot_->cartesianFrame();
    if(enabled?!robot_->zeroAtCurrentPose():!robot_->setCartesianFrame(arm::CartesianFrame())) {
        error=text(robot_->error());return false;
    }
    arm::ArmConfig candidate=config_;candidate.cartesianFrame=robot_->cartesianFrame();
    std::string failure;
    if(!QDir().mkpath(QFileInfo(configPath_->text()).absolutePath())||
       !candidate.save(configPath_->text().toStdString(),failure)) {
        robot_->setCartesianFrame(previous);
        error=QStringLiteral("坐标原点保存失败：")+text(failure);return false;
    }
    config_=candidate;
    if(enabled)setCoordinateControlsFromFrame();
    else {
        const auto pose=robot_->currentBaseCartesian();
        coordinate_[0]->setValue(pose.xCm);coordinate_[1]->setValue(pose.yCm);
        coordinate_[2]->setValue(pose.zCm);coordinate_[3]->setValue(pose.pitchDeg);
    }
    coordinatePreview_->setText(enabled?QStringLiteral("当前尖端已设为 (0,0,0)，俯仰保持原值。位置由命令和几何模型推算。"):
                               QStringLiteral("已恢复基坐标。"));
    error.clear();return true;
}
void ArmPage::setControlsFromConfig(){
    bool calibrated=config_.calibrationConfirmed;for(const auto &axis:config_.axes)calibrated=calibrated&&axis.calibrated;
    // Use the same home angles and mapping as the reset-pose button. Before
    // calibration the expected enable mode is full-range debugging.
    // These are input presets only; loading them never enables hardware.
    const auto initialPulses=calibrated?config_.homePulses():debugHomePulses();
    for(int i=0;i<6;++i){
        const QSignalBlocker pulseBlock(pulses_[i]),angleBlock(angles_[i]);
        angles_[i]->setRange(config_.axes[i].minAngleDeg,config_.axes[i].maxAngleDeg);
        angles_[i]->setValue(config_.axes[i].homeAngleDeg);
        pulses_[i]->setRange(calibrated?config_.axes[i].minPulseUs:arm::PlMinUs,
                             calibrated?config_.axes[i].maxPulseUs:arm::PlMaxUs);
        pulses_[i]->setValue(initialPulses[i]);
    }
    claw_->setRange(0,90);roll_->setRange(-90,90);
}
arm::Pulses ArmPage::debugHomePulses() const{
    arm::ArmConfig debugConfig=config_;
    for(auto &axis:debugConfig.axes){
        axis.minPulseUs=arm::PlMinUs;axis.maxPulseUs=arm::PlMaxUs;
        axis.minAngleDeg=0;axis.maxAngleDeg=180;axis.offsetUs=0;
        axis.homeAngleDeg=std::max(0.0,std::min(180.0,axis.homeAngleDeg));
    }
    return debugConfig.homePulses();
}
void ArmPage::showConfig(){
    const QSignalBlocker tableBlock(configTable_);
    for(int i=0;i<6;++i){const auto &a=config_.axes[i];QStringList vals={text(a.name),QString::number(a.channel),QString::number(a.direction),QString::number(a.minAngleDeg),QString::number(a.maxAngleDeg),QString::number(a.minPulseUs),QString::number(a.maxPulseUs),QString::number(a.offsetUs),QString::number(a.homeAngleDeg)};
        for(int col=0;col<9;++col)configTable_->setItem(i,col,new QTableWidgetItem(vals[col]));
        auto *confirmed=new QTableWidgetItem;confirmed->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable);confirmed->setCheckState(a.calibrated?Qt::Checked:Qt::Unchecked);configTable_->setItem(i,9,confirmed);
    }
    calibrationConfirmed_->setChecked(config_.calibrationConfirmed);geometryConfirmed_->setChecked(config_.geometryConfirmed);
    for(int i=0;i<4;++i){const QSignalBlocker block(geometry_[i]);geometry_[i]->setValue(config_.linkLengthsCm[i]);}
}
bool ArmPage::readConfig(arm::ArmConfig &candidate,QString &error)const{
    candidate=config_;
    for(int i=0;i<6;++i){auto &a=candidate.axes[i];a.name=configTable_->item(i,0)->text().toStdString();
        bool ok;int channel=configTable_->item(i,1)->text().toInt(&ok);if(!ok||channel<0||channel>5){error=QStringLiteral("通道须为0–5整数");return false;}a.channel=unsigned(channel);
        int direction=configTable_->item(i,2)->text().toInt(&ok);if(!ok||(direction!=1&&direction!=-1)){error=QStringLiteral("方向须为1或-1");return false;}a.direction=direction;
        double *fields[]={&a.minAngleDeg,&a.maxAngleDeg,&a.minPulseUs,&a.maxPulseUs,&a.offsetUs,&a.homeAngleDeg};
        for(int col=3;col<9;++col){double value=configTable_->item(i,col)->text().toDouble(&ok);if(!ok||!std::isfinite(value)){error=QStringLiteral("校准值须为有限数字");return false;}*fields[col-3]=value;}
        a.calibrated=configTable_->item(i,9)->checkState()==Qt::Checked;
    }
    candidate.calibrationConfirmed=calibrationConfirmed_->isChecked();candidate.geometryConfirmed=geometryConfirmed_->isChecked();
    for(int i=0;i<4;++i)candidate.linkLengthsCm[i]=geometry_[i]->value();
    // A recorded origin belongs to the geometry and angle mapping that made it.
    bool mappingChanged=candidate.linkLengthsCm!=config_.linkLengthsCm;
    for(int i=0;i<6;++i){const auto &a=candidate.axes[i];const auto &b=config_.axes[i];
        mappingChanged=mappingChanged||a.channel!=b.channel||a.direction!=b.direction||
            a.minAngleDeg!=b.minAngleDeg||a.maxAngleDeg!=b.maxAngleDeg||
            a.minPulseUs!=b.minPulseUs||a.maxPulseUs!=b.maxPulseUs||a.offsetUs!=b.offsetUs;
    }
    if(mappingChanged)candidate.cartesianFrame=arm::CartesianFrame();
    std::string failure;if(!candidate.validate(failure)){error=text(failure);return false;}return true;
}
void ArmPage::saveConfig(){
    arm::ArmConfig candidate;QString error;if(!readConfig(candidate,error)){report(error);return;}
    QDir().mkpath(QFileInfo(configPath_->text()).absolutePath());std::string failure;
    if(!candidate.save(configPath_->text().toStdString(),failure)){report(text(failure));return;}
    const bool hardware=mode_->currentIndex()==1;releaseSession();config_=candidate;showConfig();setControlsFromConfig();
    if(!hardware)resetSession(false);else{report(QStringLiteral("配置已保存，硬件连接已释放，请重新连接并确认初始姿态"));refreshStatus();}
}
void ArmPage::loadConfig(){
    arm::ArmConfig candidate;std::string failure;if(!arm::ArmConfig::load(configPath_->text().toStdString(),candidate,failure)){report(text(failure));return;}
    const bool hardware=mode_->currentIndex()==1;releaseSession();config_=candidate;showConfig();setControlsFromConfig();
    if(!hardware)resetSession(false);else{report(QStringLiteral("配置已载入，请重新连接硬件"));refreshStatus();}
}
void ArmPage::refreshActions(){
    actionTable_->setRowCount(actions_.frames.size());
    for(int row=0;row<actions_.frames.size();++row){const auto &frame=actions_.frames[row];QString target;
        if(frame.kind==ArmActionFrame::Kind::Coordinate) {
            target=QStringLiteral("坐标 (%1, %2, %3) cm，俯仰 %4°\n夹爪角 %5°，腕旋角 %6°")
                .arg(frame.coordinate[0],0,'f',2).arg(frame.coordinate[1],0,'f',2).arg(frame.coordinate[2],0,'f',2)
                .arg(frame.coordinate[3],0,'f',2).arg(frame.angles[0],0,'f',2).arg(frame.angles[1],0,'f',2);
        } else {QStringList values;for(double a:frame.angles)values<<QString::number(a,'f',2);target=QStringLiteral("角度 °：")+values.join(" / ");}
        actionTable_->setItem(row,0,new QTableWidgetItem(target));
        actionTable_->setItem(row,1,new QTableWidgetItem(QString::number(frame.durationMs)));
        actionTable_->setItem(row,2,new QTableWidgetItem(QString::number(frame.holdMs)));
    }
    actionTable_->resizeRowsToContents();
}
bool ArmPage::bindActionMapping(QString &error){
    if(!controller_){error=QStringLiteral("请先连接后端");return false;}
    const auto mapping=controller_->motionConfig();
    if(actions_.frames.isEmpty()||actions_.angleMappingId.isEmpty())actions_.bindAngleMapping(mapping);
    if(!actions_.matchesAngleMapping(mapping)){
        error=QStringLiteral("动作角度映射已变化，请恢复录制/首次播放时的模式和校准配置；原动作未修改");return false;
    }
    return true;
}
bool ArmPage::validateActions(ArmActions &validated,QString &error){
    // Legacy files have no mapping metadata. Bind them on first playback in
    // memory; loading never rewrites the existing action file.
    if(!bindActionMapping(error))return false;
    if(!validated.load(actions_.json(),minimumAngles(),maximumAngles(),error))return false;
    if(validated.usesCoordinates()&&(!robot_||!robot_->cartesianFrameCompatible()||
                                  !validated.matchesCoordinateFrame(robot_->cartesianFrame()))) {
        error=QStringLiteral("坐标动作原点或映射模式不匹配，请使用记录时的配置或重新记录");return false;
    }
    return true;
}
std::array<double,6> ArmPage::minimumAngles()const{std::array<double,6> v;for(int i=0;i<6;++i)v[i]=controller_&&controller_->pulseDebugEnabled()?0:config_.axes[i].minAngleDeg;return v;}
std::array<double,6> ArmPage::maximumAngles()const{std::array<double,6> v;for(int i=0;i<6;++i)v[i]=controller_&&controller_->pulseDebugEnabled()?180:config_.axes[i].maxAngleDeg;return v;}

bool ArmPage::loadActionsFromPath(const QString &path,QString &error){
    cancelActions();
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){error=file.errorString();return false;}
    if(file.size()>1024*1024){error=QStringLiteral("动作文件超过1 MiB");return false;}
    ArmActions candidate;
    if(!candidate.load(file.readAll(),minimumAngles(),maximumAngles(),error))return false;
    actions_=candidate;actionFile_=path;refreshActions();error.clear();return true;
}
QJsonObject ArmPage::handleConsoleCommand(const QJsonObject &command){
    const QString action=command.value("action").toString();
    if(action.startsWith("conveyor-"))return conveyor_->handleConsoleCommand(command);
    const auto reply=[this](bool ok,const QString &failure)->QJsonObject{
        refreshStatus();
        QJsonObject result;result["ok"]=ok;result["error"]=failure;
        result["connected"]=bool(controller_);
        result["mode"]=backend_?(backend_->hardware()?"hardware":"simulation"):"disconnected";
        result["enabled"]=controller_&&controller_->enabled();
        result["returningBeforeOff"]=returningBeforeOff_;
        result["holdingAfterTimeout"]=controller_&&controllerLocked(controller_.get(),0);
        result["hardwareStatusAvailable"]=controller_&&controllerStatusAvailable(controller_.get(),0);
        result["hardwareOutputActive"]=controller_&&controllerOutputActive(controller_.get(),0);
        result["moving"]=controller_&&controller_->moving();
        const bool debug=controller_&&controller_->pulseDebugEnabled();result["debug"]=debug;
        result["initialPoseAcknowledged"]=controller_&&controller_->initialPoseAcknowledged();
        result["calibrationConfirmed"]=config_.calibrationConfirmed;
        result["geometryConfirmed"]=config_.geometryConfirmed;
        const auto &frame=config_.cartesianFrame;
        result["coordinateOriginEnabled"]=frame.enabled;
        result["coordinateOriginDebugMapping"]=frame.debugMapping;
        result["coordinateOriginBaseCm"]=QJsonArray{frame.originCm[0],frame.originCm[1],frame.originCm[2]};
        result["coordinateOriginPitchDeg"]=frame.pitchDeg;
        result["coordinateFrameCompatible"]=robot_&&robot_->cartesianFrameCompatible();
        if(robot_&&robot_->cartesianFrameCompatible()&&controller_->initialPoseAcknowledged()) {
            const auto pose=robot_->currentCartesian();
            result["cartesianCm"]=QJsonArray{pose.xCm,pose.yCm,pose.zCm};result["cartesianPitchDeg"]=pose.pitchDeg;
        }
        result["actionFile"]=actionFile_;result["actionFrameCount"]=actions_.frames.size();
        result["actionPlaying"]=player_.active();result["actionFrameIndex"]=player_.active()?player_.index()+1:0;
        result["actionUsesCoordinates"]=actions_.usesCoordinates();
        result["sortJobState"]=sortJobState_;result["sortJobId"]=int(sortJobId_);
        result["actionJobState"]=actionJobState_;result["actionJobId"]=int(actionJobId_);
        QJsonArray calibrated,pulses,minimum,maximum,targets;
        for(int i=0;i<6;++i){
            calibrated.append(config_.axes[i].calibrated);
            if(controller_)pulses.append(controller_->currentPulses()[i]);
            if(debug){minimum.append(controller_->debugMinPulses()[i]);maximum.append(controller_->debugMaxPulses()[i]);}
            targets.append(pulses_[i]->value());
        }
        result["axesCalibrated"]=calibrated;result["commandedPulses"]=pulses;result["targetPulses"]=targets;
        // A fixed camera mapping is valid only at this observation pose/model.
        QJsonArray links,axisModels;for(double length:config_.linkLengthsCm)links.append(length);
        for(const auto &a:config_.axes)axisModels.append(QJsonArray{int(a.channel),a.direction,a.minAngleDeg,
            a.maxAngleDeg,a.minPulseUs,a.maxPulseUs,a.offsetUs});
        result["visionReference"]=QJsonObject{{"pulses",pulses},{"debug",debug},{"linksCm",links},{"axes",axisModels},
            {"originEnabled",frame.enabled},{"originDebug",frame.debugMapping},
            {"originCm",QJsonArray{frame.originCm[0],frame.originCm[1],frame.originCm[2]}},{"originPitchDeg",frame.pitchDeg}};
        result["debugMinPulses"]=minimum;result["debugMaxPulses"]=maximum;
        result["positionFeedback"]=false;result["status"]=status_->text();result["uiError"]=error_->text();
        return result;
    };
    const auto reject=[this,&reply](const QString &failure)->QJsonObject{report(failure);return reply(false,failure);};
    const auto parsePulses=[&command](const QString &key,arm::Pulses &out)->bool{
        const QJsonValue field=command.value(key);if(!field.isArray())return false;
        const QJsonArray values=field.toArray();if(values.size()!=6)return false;
        for(int i=0;i<6;++i){
            if(!values[i].isDouble()||!std::isfinite(values[i].toDouble()))return false;
            const double value=values[i].toDouble();const double rounded=std::round(value*10.0)/10.0;
            if(!std::isfinite(rounded)||std::abs(value-rounded)>1e-7)return false;
            out[i]=rounded;
        }
        return true;
    };
    if(action=="show"||action=="status")return reply(true,QString());
    // Internal conveyor calls reuse this controller; the console allowlist does
    // not expose these commands. Validate every joint target before any motion.
    if(action=="sort-preview"||action=="sort-run"||action=="fixed-sort-preview"||action=="fixed-sort-run"){
        try {
            if(action.startsWith("fixed-sort-")) {
                if(controller_&&controller_->hardware()&&!command.value("cameraMeasured").toBool(false))
                    throw std::runtime_error("Fixed camera and work plane must be measured before hardware sorting");
                if(config_.cartesianFrame.enabled)
                    throw std::runtime_error("Fixed camera sorting uses arm base coordinates; restore base coordinates first");
            }
            if(!robot_||!controller_||!controller_->initialPoseAcknowledged()||!robot_->cartesianFrameCompatible()||
               controller_->moving()||player_.active())throw std::runtime_error("Prepare a stationary arm in the calibrated coordinate frame");
            if(controller_->hardware()&&(controller_->pulseDebugEnabled()||!config_.calibrationConfirmed||!config_.geometryConfirmed))
                throw std::runtime_error("Automatic sorting requires confirmed joint and geometry calibration in normal mode");
            const auto xy=command.value("xy").toArray();
            if(xy.size()!=2||!xy[0].isDouble()||!xy[1].isDouble())throw std::runtime_error("Expected stopped XY pair");
            const auto recipe=SortRecipe::build(command.value("config").toObject(),command.value("className").toString(),xy[0].toDouble(),xy[1].toDouble());
            if(!SortRecipe::referenceMatches(recipe.reference,reply(true,QString()).value("visionReference").toObject()))
                throw std::runtime_error("Arm observation pose/model differs from sorting calibration");
            ArmActions plan;arm::Pose seed=controller_->currentAngles();
            arm::ArmConfig mapping=config_;
            if(controller_->pulseDebugEnabled())for(auto &a:mapping.axes){a.minAngleDeg=0;a.maxAngleDeg=180;a.minPulseUs=arm::PlMinUs;a.maxPulseUs=arm::PlMaxUs;a.offsetUs=0;}
            QJsonArray steps;
            for(const auto &w:recipe.waypoints){
                arm::IKOptions options;options.minPitchDeg=options.maxPitchDeg=w.coordinate[3];
                options.seed=arm::servoAnglesToJoints(seed);options.useSeed=true;
                const auto solved=robot_->previewCoordinate(arm::CartesianPose(w.coordinate[0],w.coordinate[1],w.coordinate[2],w.coordinate[3]),options);
                if(!solved.ok)throw std::runtime_error((w.name+": "+text(solved.error)).toStdString());
                ArmActionFrame frame;frame.angles=arm::jointsToServoAngles(solved.joints,seed);
                frame.angles[0]=w.claw;frame.angles[1]=w.roll;frame.durationMs=w.durationMs;frame.holdMs=w.holdMs;
                arm::Pulses pulses;std::string failure;
                if(!mapping.anglesToPulses(frame.angles,pulses,failure))throw std::runtime_error(failure);
                plan.frames.append(frame);seed=frame.angles;
                steps.append(QJsonObject{{"step",w.name},{"coordinate",QJsonArray{w.coordinate[0],w.coordinate[1],w.coordinate[2],w.coordinate[3]}}});
            }
            ArmActionFrame back;back.angles=mapping.pulsesToAngles(recipe.observationPulses);back.durationMs=recipe.returnMs;back.holdMs=300;
            arm::Pulses validated;std::string failure;
            if(!mapping.anglesToPulses(back.angles,validated,failure))throw std::runtime_error(failure);
            plan.frames.append(back);steps.append(QJsonObject{{"step","return-observation"}});
            if(action=="sort-run"||action=="fixed-sort-run"){
                if(!command.value("confirm").toBool()||!controller_->enabled())throw std::runtime_error("Sorting requires explicit run request and enabled arm");
                startActions(plan);++sortJobId_;sortJobState_="running";
            }
            report(QString());auto response=reply(true,QString());response["zone"]=recipe.zone;response["steps"]=steps;return response;
        }catch(const std::exception &e){return reject(QString::fromUtf8(e.what()));}
    }
    if(action=="coordinate-zero"||action=="coordinate-base") {
        QString failure;if(!setCoordinateOrigin(action=="coordinate-zero",failure))return reject(failure);
        report(QString());return reply(true,QString());
    }
    if(action=="coordinate-preview"||action=="coordinate-move"||action=="xy-preview"||action=="xy-move") {
        if(!robot_)return reject(QStringLiteral("请先连接后端"));
        const bool xy=action.startsWith("xy-");
        const bool moving=action=="coordinate-move"||action=="xy-move";
        QJsonArray values=command.value(xy?"xy":"coordinate").toArray();
        if(xy){
            if(!command.value("xy").isArray()||values.size()!=2)
                return reject(QStringLiteral("xy 须为 [X_cm,Y_cm]，像素须先标定转换"));
            values.append(command.value("zCm"));values.append(command.value("pitchDeg"));
            if(command.contains("allowPitchSearch")&&!command.value("allowPitchSearch").isBool())
                return reject(QStringLiteral("allowPitchSearch 须为布尔值"));
        }
        if(values.size()!=4)
            return reject(QStringLiteral("coordinate 须为 [X,Y,Z,俯仰角]，单位 cm、度"));
        for(const auto &v:values)if(!v.isDouble()||!std::isfinite(v.toDouble()))
            return reject(QStringLiteral("坐标和俯仰须为有限数值"));
        const arm::CartesianPose target(values[0].toDouble(),values[1].toDouble(),values[2].toDouble(),values[3].toDouble());
        const arm::XYPlane plane(target.zCm,target.pitchDeg,command.value("allowPitchSearch").toBool(false));
        if(xy&&command.contains("visionReference")){
            if(!command.value("visionReference").isObject()||controller_->moving()||player_.active()||
               !controller_->initialPoseAcknowledged()||!robot_->cartesianFrameCompatible()||
               command.value("visionReference").toObject()!=reply(true,QString()).value("visionReference").toObject())
                return reject(QStringLiteral("视觉标定姿态或坐标配置已变化，请返回拍摄姿态并重新识别"));
        }
        const auto preview=xy?robot_->previewXY(target.xCm,target.yCm,plane):robot_->previewCoordinate(target);
        if(!preview.ok)return reject(text(preview.error));
        if(moving) {
            if(!command.value("confirm").isBool()||!command.value("confirm").toBool())return reject(QStringLiteral("运动须明确设置 confirm=true"));
            const QJsonValue duration=command.value("durationMs");const double ms=duration.toDouble(-1);
            if(!duration.isDouble()||!std::isfinite(ms)||std::floor(ms)!=ms||ms<20||ms>60000)
                return reject(QStringLiteral("durationMs 须为20～60000的整数"));
            if(!ready())return reply(false,error_->text());
            cancelActions();
            const bool ok=xy?robot_->moveXY(target.xCm,target.yCm,plane,unsigned(ms),uint64_t(clock_.elapsed())):
                robot_->moveCoordinate(target,unsigned(ms),uint64_t(clock_.elapsed()));
            if(!ok)return reject(text(robot_->error()));
        }
        report(QString());auto response=reply(true,QString());
        response["achievedCm"]=QJsonArray{preview.achieved.xCm,preview.achieved.yCm,preview.achieved.zCm};
        response["achievedPitchDeg"]=preview.achieved.pitchDeg;response["positionErrorCm"]=preview.positionErrorCm;
        response["jointAnglesDeg"]=QJsonArray{preview.joints.yawDeg,preview.joints.shoulderDeg,preview.joints.elbowDeg,preview.joints.wristPitchDeg};
        return response;
    }
    if(action=="load-actions"){
        const QJsonValue path=command.value("path");
        if(!path.isString()||path.toString().isEmpty())return reject(QStringLiteral("path 须为动作 JSON 文件路径"));
        QString failure;if(!loadActionsFromPath(path.toString(),failure))return reject(failure);
        report(QString());return reply(true,QString());
    }
    if(action=="play-actions"){
        if(!command.value("confirm").isBool()||!command.value("confirm").toBool())return reject(QStringLiteral("播放须明确设置 confirm=true"));
        if(!ready())return reply(false,error_->text());
        ArmActions validated;QString failure;
        if(!validateActions(validated,failure))return reject(failure);
        startActions(validated);report(QString());return reply(true,QString());
    }
    if(action=="connect"){
        const QJsonValue requested=command.value("mode");
        const QString mode=requested.isUndefined()?QStringLiteral("hardware"):requested.toString();
        if(mode!="hardware"&&mode!="simulation")return reject(QStringLiteral("mode 必须为 hardware 或 simulation"));
        mode_->setCurrentIndex(mode=="hardware"?1:0);resetSession(mode=="hardware");
        return reply(bool(controller_),controller_?QString():error_->text());
    }
    if(action=="stop"||action=="off"){
        cancelActions();
        if(!controller_)return reject(QStringLiteral("请先连接后端"));
        if(action=="off"&&!controller_->disable())return reject(text(controller_->error()));
        report(QString());return reply(true,QString());
    }
    if(action=="debug-enable"){
        cancelActions();
        if(!controller_)return reject(QStringLiteral("请先连接后端"));
        if(controller_->enabled())return reject(QStringLiteral("请先关闭 PWM，再确认调试初始姿态"));
        if(!command.value("confirm").isBool()||!command.value("confirm").toBool())return reject(QStringLiteral("必须明确设置 confirm=true 并确认六路初始脉宽"));
        arm::Pulses baseline;if(!parsePulses("baseline",baseline))return reject(QStringLiteral("baseline 必须包含六个有限数值（μs），最多一位小数"));
        if(!controller_->enablePulseDebug(baseline,true))return reject(text(controller_->error()));
        refreshStatus();for(int i=0;i<6;++i)pulses_[i]->setValue(baseline[i]);
        report(QString());return reply(true,QString());
    }
    if(action=="debug-pulses"){
        cancelActions();
        if(!controller_)return reject(QStringLiteral("请先连接后端"));
        if(!controller_->pulseDebugEnabled())return reject(QStringLiteral("请先明确确认并调试使能"));
        arm::Pulses target;if(!parsePulses("pulses",target))return reject(QStringLiteral("pulses 必须包含六个有限数值（μs），最多一位小数"));
        const QJsonValue duration=command.value("durationMs");const double ms=duration.toDouble(-1);
        if(!duration.isDouble()||!std::isfinite(ms)||std::floor(ms)!=ms||ms<20||ms>60000)return reject(QStringLiteral("durationMs 必须为20～60000的整数"));
        if(!controller_->moveDebugPulses(target,uint64_t(ms),uint64_t(clock_.elapsed())))return reject(text(controller_->error()));
        for(int i=0;i<6;++i)pulses_[i]->setValue(target[i]);
        duration_->setValue(int(ms));
        report(QString());return reply(true,QString());
    }
    return reject(QStringLiteral("不支持的控制命令"));
}
