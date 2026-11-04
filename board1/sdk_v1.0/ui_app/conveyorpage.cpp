#include "conveyorpage.hpp"
#include "sort_recipe.hpp"
#include "sort_settings.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
QString stateText(conveyor::State s) {
    using conveyor::State;
    switch(s){case State::Idle:return QStringLiteral("待机");case State::Waiting:return QStringLiteral("等待目标");
    case State::Feeding:return QStringLiteral("进料中");case State::Tracking:return QStringLiteral("跟踪 / 减速");
    case State::Settling:return QStringLiteral("停带确认");case State::Holding:return QStringLiteral("到位 · 等待抓取");
    case State::Picking:return QStringLiteral("抓取中 · 传送带禁能");case State::Fault:return QStringLiteral("故障停机");}
    return QString();
}
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
double numeric(const QJsonValue &v){require(v.isDouble()&&std::isfinite(v.toDouble()),"Expected a finite JSON number");return v.toDouble();}
uint64_t integer(const QJsonValue &v){const double n=numeric(v);require(n>=0&&n<=9007199254740991.0&&n==std::floor(n),"Expected nonnegative integer");return uint64_t(n);}
QJsonObject readJson(const QString &path){
    QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot open JSON file");
    require(f.size()>0&&f.size()<=262144,"JSON file size outside limits");
    QJsonParseError error;auto d=QJsonDocument::fromJson(f.readAll(),&error);
    require(error.error==QJsonParseError::NoError&&d.isObject(),"Malformed JSON object");return d.object();
}
QPointF project(const QJsonObject &cal,const QPointF &pixel,const QJsonObject &frame){
    require(cal.value("version").toInt()==1,"Unsupported calibration version");
    const auto size=cal.value("imageSize").toArray();
    require(size.size()==2&&numeric(size[0])==numeric(frame.value("image_width"))&&
        numeric(size[1])==numeric(frame.value("image_height")),"Image size changed; recalibrate");
    require(cal.value("inputFraction").toDouble(1)==frame.value("input_fraction").toDouble(1),"Image crop changed");
    auto m=cal.value("projection").toObject();auto hull=m.value("pixelHull").toArray();
    require(hull.size()>=3,"Calibration region missing");
    for(int i=0;i<hull.size();++i){
        const auto a=hull[i].toArray(),b=hull[(i+1)%hull.size()].toArray();
        require(a.size()==2&&b.size()==2,"Invalid calibration hull");
        const double ax=numeric(a[0]),ay=numeric(a[1]),bx=numeric(b[0]),by=numeric(b[1]);
        require((bx-ax)*(pixel.y()-ay)-(by-ay)*(pixel.x()-ax)>=-1e-7,"Target outside calibrated region");
    }
    const auto pc=m.value("pixelCenter").toArray(),xc=m.value("xyCenter").toArray(),h=m.value("h").toArray();
    require(pc.size()==2&&xc.size()==2&&h.size()==8,"Invalid homography shape");
    const double ps=numeric(m.value("pixelScale")),xs=numeric(m.value("xyScale"));
    require(ps>0&&xs>0,"Invalid calibration scale");
    const double u=(pixel.x()-numeric(pc[0]))/ps,v=(pixel.y()-numeric(pc[1]))/ps;
    const double w=numeric(h[6])*u+numeric(h[7])*v+1;
    require(std::abs(w)>1e-8,"Projection horizon");
    const QPointF xy((numeric(h[0])*u+numeric(h[1])*v+numeric(h[2]))/w*xs+numeric(xc[0]),
                     (numeric(h[3])*u+numeric(h[4])*v+numeric(h[5]))/w*xs+numeric(xc[1]));
    require(std::isfinite(xy.x())&&std::isfinite(xy.y()),"Invalid projected coordinate");return xy;
}
QDoubleSpinBox *box(double lo,double hi,double value){auto *b=new QDoubleSpinBox;b->setRange(lo,hi);b->setDecimals(2);b->setValue(value);return b;}
QPushButton *button(const QString &text,const char *name=nullptr){auto *b=new QPushButton(text);if(name)b->setObjectName(name);return b;}
}

class BeltView : public QWidget {
public:
    double x=5,y=0,target=20;bool visibleObject=true,simulation=true,ready=false;
    BeltView(){setMinimumHeight(145);setObjectName("conveyorView");}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const QRectF belt(25,38,width()-50,70);p.setPen(QPen(QColor("#3b5875"),2));p.setBrush(QColor("#192c41"));p.drawRoundedRect(belt,18,18);
        p.setPen(QPen(QColor("#2c455e"),1));for(int i=1;i<14;++i){const double x=belt.left()+belt.width()*i/14;p.drawLine(QPointF(x,belt.top()+5),QPointF(x,belt.bottom()-5));}
        const double stopX=belt.left()+belt.width()*.8;
        p.setPen(QPen(QColor("#ffcb6b"),3,Qt::DashLine));p.drawLine(QPointF(stopX,27),QPointF(stopX,119));
        p.setPen(QColor("#ffcb6b"));p.drawText(QRectF(stopX-65,5,130,22),Qt::AlignCenter,QStringLiteral("抓取线 X=%1 cm").arg(target,0,'f',1));
        if(visibleObject){const double t=std::max(0.03,std::min(.96,(x-(target-18))/22));const double px=belt.left()+t*belt.width();
            const double py=belt.center().y()-std::max(-20.0,std::min(20.0,y*8));p.setPen(Qt::NoPen);p.setBrush(QColor(ready?"#7dd6a1":"#56b8d7"));p.drawRoundedRect(QRectF(px-16,py-16,32,32),5,5);}
        p.setPen(QColor("#a6b9d3"));p.drawText(QRectF(25,116,width()-50,25),Qt::AlignLeft,
            simulation?QStringLiteral("模拟带面与编码器 · 不输出电机信号   → +X"):QStringLiteral("真实视觉坐标 / 编码器反馈   → +X"));
    }
};

ConveyorPage::ConveyorPage(ArmHandler arm,QWidget *parent):QWidget(parent),arm_(std::move(arm)) {
    setObjectName("conveyorPage");clock_.start();
    setStyleSheet("QWidget#conveyorPage, QWidget#conveyorRunBody, QWidget#conveyorSettingsBody {background:#111e30;color:#dce7f7;}"
                  "QScrollArea {background:#111e30;border:1px solid #30455e;}"
                  "QLabel,QCheckBox {color:#dce7f7;}"
                  "QPlainTextEdit {background:#0b1727;color:#adc4dc;border:1px solid #30455e;border-radius:6px;}"
                  "QLineEdit,QDoubleSpinBox,QComboBox {background:#17283d;color:#e6edf6;}");
    auto *root=new QVBoxLayout(this);root->setContentsMargins(8,8,8,8);root->setSpacing(8);
    banner_=new QLabel(QStringLiteral("模拟联调 · BTS7960 / JGB37-520 · 实物传送带未连接"));banner_->setWordWrap(true);
    banner_->setStyleSheet("background:#203c4b;color:#a7ecdb;padding:10px;border-radius:8px;");root->addWidget(banner_);
    auto *tabs=new QTabWidget;tabs->setObjectName("conveyorTabs");root->addWidget(tabs,1);
    auto *runBody=new QWidget;runBody->setObjectName("conveyorRunBody");auto *run=new QVBoxLayout(runBody);run->setContentsMargins(8,8,8,8);
    view_=new BeltView;run->addWidget(view_);
    auto *stats=new QGridLayout;state_=new QLabel;position_=new QLabel;speedText_=new QLabel;
    state_->setObjectName("conveyorState");position_->setObjectName("conveyorPosition");speedText_->setObjectName("conveyorSpeed");
    state_->setStyleSheet("font-size:21px;font-weight:600;color:#7dd6c3;");
    stats->addWidget(state_,0,0);stats->addWidget(position_,0,1);stats->addWidget(speedText_,1,0,1,2);run->addLayout(stats);
    auto *controls=new QHBoxLayout;
    auto *startButton=button(QStringLiteral("开始进料"),"conveyorStart"),*demoButton=button(QStringLiteral("模拟演示"),"conveyorDemo");
    auto *stopButton=button(QStringLiteral("停止"),"conveyorStop"),*resetButton=button(QStringLiteral("故障复位")),*releaseButton=button(QStringLiteral("放行继续"),"conveyorRelease");
    for(auto *b:{startButton,demoButton,stopButton,resetButton,releaseButton})controls->addWidget(b);
    run->addLayout(controls);
    auto *sortButton=button(QStringLiteral("开始视觉自动分拣"),"conveyorAutoStart");run->addWidget(sortButton);
    connect(sortButton,&QPushButton::clicked,this,[this](){
        const auto arm=arm_({{"action","status"}});
        if(arm.value("mode").toString()=="hardware"&&QMessageBox::question(this,QStringLiteral("自动分拣"),QStringLiteral("确认已标定五类放置区、抓取高度和观察姿态，并核对完整抓放路径？启动后会在到位时自动驱动已使能的机械臂。"))!=QMessageBox::Yes)return;
        start(false,true);
    });
    armText_=new QLabel(QStringLiteral("机械臂联动：默认只预览；模拟演示不会使机械臂运动。"));armText_->setWordWrap(true);run->addWidget(armText_);
    auto *armControls=new QHBoxLayout;auto *previewButton=button(QStringLiteral("预览抓取点"));
    auto *pickButton=button(QStringLiteral("执行已载入抓放动作组"),"conveyorPick");
    autoRelease_=new QCheckBox(QStringLiteral("动作完成并回到拍摄姿态后自动放行"));autoRelease_->setObjectName("conveyorAutoRelease");armControls->addWidget(previewButton);armControls->addWidget(pickButton);armControls->addWidget(autoRelease_);run->addLayout(armControls);
    error_=new QLabel;error_->setWordWrap(true);error_->setStyleSheet("color:#ffbd9e;");run->addWidget(error_);
    events_=new QPlainTextEdit;events_->setReadOnly(true);events_->setMaximumBlockCount(80);events_->setMaximumHeight(110);events_->setObjectName("conveyorEvents");run->addWidget(events_);
    auto *area=new QScrollArea;area->setWidgetResizable(true);area->setWidget(runBody);tabs->addTab(area,QStringLiteral("运行与联动"));
    auto *settingsBody=new QWidget;settingsBody->setObjectName("conveyorSettingsBody");auto *settings=new QFormLayout(settingsBody);
    mode_=new QComboBox;mode_->addItems({QStringLiteral("模拟传送带"),QStringLiteral("硬件 UIO（需校准）")});mode_->setObjectName("conveyorMode");
    uio_=new QLineEdit("/dev/uio0");configPath_=new QLineEdit;
    visionPath_=new QLineEdit("/tmp/cubes_latest.json");calibrationPath_=new QLineEdit;
    className_=new QLineEdit;className_->setPlaceholderText(QStringLiteral("可选，例如 green_good"));
    target_=box(-100,100,20);y_=box(-30,30,0);z_=box(-30,50,2);pitch_=box(-90,10,-45);speed_=box(.1,4,1.5);
    target_->setObjectName("conveyorTarget");y_->setObjectName("conveyorY");z_->setObjectName("conveyorZ");
    watch_=new QCheckBox(QStringLiteral("持续读取视觉 JSON（模拟模式也可联调真实视觉）"));
    settings->addRow(QStringLiteral("运行模式"),mode_);settings->addRow(QStringLiteral("UIO 设备"),uio_);settings->addRow(QStringLiteral("控制配置 INI"),configPath_);
    settings->addRow(QStringLiteral("抓取线 X / cm"),target_);settings->addRow(QStringLiteral("模拟物体 Y / cm"),y_);
    settings->addRow(QStringLiteral("抓取高度 Z / cm"),z_);settings->addRow(QStringLiteral("末端俯仰 / °"),pitch_);settings->addRow(QStringLiteral("目标最大带速 / cm/s"),speed_);
    settings->addRow(QStringLiteral("视觉结果 JSON"),visionPath_);settings->addRow(QStringLiteral("平面标定 JSON"),calibrationPath_);settings->addRow(QStringLiteral("筛选类别"),className_);settings->addRow(watch_);
    auto *apply=button(QStringLiteral("连接 / 应用参数"));settings->addRow(apply);
    auto *note=new QLabel(QStringLiteral("硬件：12 V / 60 rpm，20→31 齿，滚筒约 Ø20 mm；空载带速估算 4.05 cm/s。实际编码器比例需实测。X/Y/Z 使用机械臂当前坐标系；相机移动后须恢复标定拍摄姿态。"));note->setWordWrap(true);settings->addRow(note);
    auto *settingsArea=new QScrollArea;settingsArea->setWidgetResizable(true);settingsArea->setWidget(settingsBody);tabs->addTab(settingsArea,QStringLiteral("参数与视觉"));
    sortSettings_=new SortSettings([this](){return arm_({{"action","status"}});},[this](){stop();});
    auto *sortArea=new QScrollArea;sortArea->setWidgetResizable(true);sortArea->setWidget(sortSettings_);tabs->addTab(sortArea,QStringLiteral("分类放置区"));
    connect(startButton,&QPushButton::clicked,this,[this](){start(false);});connect(demoButton,&QPushButton::clicked,this,[this](){start(true);});
    connect(stopButton,&QPushButton::clicked,this,[this](){stop();});connect(resetButton,&QPushButton::clicked,this,[this](){if(controller_){controller_->resetFault(now());refresh();}});
    connect(releaseButton,&QPushButton::clicked,this,[this](){release();});connect(previewButton,&QPushButton::clicked,this,[this](){preview();});
    connect(pickButton,&QPushButton::clicked,this,[this](){playArmActions();});connect(apply,&QPushButton::clicked,this,[this](){connectBackend();});
    connect(mode_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){stop();controller_.reset();motor_.reset();registers_.reset();message_=QStringLiteral("模式已更改，请点击连接 / 应用参数");refresh();});
    connect(watch_,&QCheckBox::toggled,this,[this](bool){stop();sourceSeen_=false;sourceClass_.clear();});
    connectBackend();auto *timer=new QTimer(this);connect(timer,&QTimer::timeout,this,[this](){tick();});timer->start(20);
}
ConveyorPage::~ConveyorPage(){stop();controller_.reset();motor_.reset();registers_.reset();}
uint64_t ConveyorPage::now() const{return uint64_t(clock_.elapsed());}
void ConveyorPage::event(const QString &s){events_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss")+"  "+s);}
void ConveyorPage::fail(const QString &s){operationOk_=false;message_=s;error_->setText(s);event(s);}
void ConveyorPage::connectBackend(){
    stop();controller_.reset();motor_.reset();registers_.reset();
    try {
        if(!configPath_->text().trimmed().isEmpty())config_=conveyor::loadConfig(configPath_->text().toStdString());
        else {config_=conveyor::Config();config_.target=target_->value();config_.tolerance=.2;config_.slowDistance=4;
            config_.lateralMin=-2;config_.lateralMax=2;config_.encoderEnabled=1;config_.cmPerCount=.001;
            config_.stableSpeed=.08;config_.leadMs=100;config_.maxRunMs=60000;config_.maxSpeed=speed_->value();config_.minSpeed=.2;}
        config_.target=target_->value();config_.maxSpeed=speed_->value();config_.validate();
        require(std::abs(config_.axisX-1)<1e-9&&std::abs(config_.axisY)<1e-9&&std::abs(config_.originX)<1e-9&&std::abs(config_.originY)<1e-9,
            "This UI requires arm-frame +X belt direction and zero origin; transform calibration before use");
        if(mode_->currentIndex()==1){require(!configPath_->text().trimmed().isEmpty()&&config_.encoderEnabled&&config_.cmPerCount>0,"Hardware requires measured encoder configuration");registers_=conveyor::openUio(uio_->text().toStdString());}
        else registers_.reset(new conveyor::MockRegisters);
        motor_.reset(new conveyor::MotorDriver(*registers_));motor_->initialize(now());
        require(mode_->currentIndex()!=1||!(motor_->features()&1),"PL is BENCH_ONLY: use conveyorctl --self-test; motor outputs are locked");
        controller_.reset(new conveyor::Controller(*motor_,config_));
        previous_=conveyor::State::Idle;message_.clear();event(QStringLiteral("控制器已连接，输出禁能"));
    } catch(const std::exception &e){controller_.reset();motor_.reset();registers_.reset();fail(QString::fromUtf8(e.what()));}
    refresh();
}
void ConveyorPage::start(bool demo,bool sorting){
    try{
        require(bool(controller_),"Connect controller first");require(!demo||dynamic_cast<conveyor::MockRegisters*>(registers_.get()),"Demo requires mock conveyor");
        require(controller_->snapshot().state==conveyor::State::Idle,"Stop/reset before starting");
        const auto arm=arm_({{"action","status"}});
        require(mode_->currentIndex()!=1||watch_->isChecked(),"Hardware feeding requires live vision; enable vision before starting");
        if(sorting){
            require(watch_->isChecked(),"Automatic sorting requires live vision");
            require(arm.value("enabled").toBool()&&!arm.value("moving").toBool()&&!arm.value("actionPlaying").toBool(),"Prepare and enable a stationary arm first");
            require(arm.value("mode").toString()!="hardware"||!dynamic_cast<conveyor::MockRegisters*>(registers_.get()),"Physical arm sorting requires a physical conveyor; use simulation for integration tests");
            require(arm.value("mode").toString()!="hardware"||
                (!arm.value("debug").toBool()&&arm.value("calibrationConfirmed").toBool()&&arm.value("geometryConfirmed").toBool()&&arm.value("coordinateFrameCompatible").toBool()),
                "Confirm normal-mode joint, geometry and coordinate calibration before automatic feeding");
            activeSortConfig_=sortSettings_->config();
            for(const auto &name:{"red","yellow","blue","green_good","green_defect"})SortRecipe::build(activeSortConfig_,QString::fromLatin1(name),target_->value(),0);
            require(SortRecipe::referenceMatches(activeSortConfig_.value("vision_reference").toObject(),arm.value("visionReference").toObject()),"Return arm to the configured observation pose/model first");
        }
        autoSort_=sorting;sortJob_=false;
        demo_=demo;plantX_=config_.target-15;plantV_=0;encoderPosition_=0;encoderCount_=0;
        sequence_=0;sourceSeen_=false;awaitEntry_=false;sourceClass_.clear();lastFrame_=now();lastTick_=now();startWallMs_=uint64_t(QDateTime::currentMSecsSinceEpoch());
        observationReference_=arm_({{"action","status"}}).value("visionReference").toObject();
        controller_->arm(now());controller_->feed(now());message_.clear();event(demo?QStringLiteral("开始模拟到位 / 虚拟抓取 / 放行演示，机械臂不运动"):QStringLiteral("开始进料，等待有效目标"));
    }catch(const std::exception &e){fail(QString::fromUtf8(e.what()));}refresh();
}
void ConveyorPage::stop(){
    demo_=false;autoSort_=false;sortJob_=false;
    if(armJob_){arm_({{"action","stop"}});armJob_=false;}
    if(controller_)controller_->stop(now());
    refresh();
}
void ConveyorPage::beginSort(){
    // Freeze the stopped XY/class in the request. No visual updates are consumed
    // during Picking, and a failed plan leaves the belt stopped.
    const auto plan=arm_({{"action","sort-preview"},{"config",activeSortConfig_},{"className",sourceClass_},{"xy",QJsonArray{lastX_,lastY_}}});
    require(plan.value("ok").toBool(),plan.value("error").toString().toUtf8().constData());
    controller_->beginPick(now());
    const auto run=arm_({{"action","sort-run"},{"config",activeSortConfig_},{"className",sourceClass_},{"xy",QJsonArray{lastX_,lastY_}},{"confirm",true}});
    require(run.value("ok").toBool(),run.value("error").toString().toUtf8().constData());
    armJob_=true;sortJob_=true;sortJobId_=unsigned(run.value("sortJobId").toInt());
    sortedClass_=sourceClass_;sortedZone_=run.value("zone").toString();
    event(QStringLiteral("自动分拣 %1 → 放置区 %2；坐标已锁定，带面禁能").arg(sortedClass_,sortedZone_));
}
void ConveyorPage::hideEvent(QHideEvent *e){stop();QWidget::hideEvent(e);}
void ConveyorPage::preview(){
    const double x=controller_&&controller_->snapshot().ready?lastX_:target_->value();
    const double y=controller_&&controller_->snapshot().ready?lastY_:y_->value();
    const auto result=arm_({{"action","xy-preview"},{"xy",QJsonArray{x,y}},{"zCm",z_->value()},{"pitchDeg",pitch_->value()},{"allowPitchSearch",true}});
    if(result.value("ok").toBool()){armText_->setText(QStringLiteral("逆解预览通过：(%1, %2, %3) cm；未发送运动命令。抓放使用动作组页载入的完整动作。").arg(x,0,'f',2).arg(y,0,'f',2).arg(z_->value(),0,'f',2));event(QStringLiteral("机械臂 XY 预览通过"));}
    else {armText_->setText(QStringLiteral("机械臂预览未通过：")+result.value("error").toString());fail(armText_->text());}
}
void ConveyorPage::playArmActions(){
    bool locked=false,queued=false;
    try {
        require(controller_&&controller_->snapshot().state==conveyor::State::Holding,"Belt must be stably holding first");
        const auto state=arm_({{"action","status"}});
        require(state.value("enabled").toBool()&&!state.value("moving").toBool()&&!state.value("actionPlaying").toBool(),"Prepare and enable arm before picking");
        require(state.value("actionFrameCount").toInt()>0,"Load a complete pick/place/return action group first");
        if(state.value("mode").toString()=="hardware"&&QMessageBox::question(this,QStringLiteral("执行抓放动作组"),QStringLiteral("传送带已停稳。确认已核对动作组、物体位置和机械臂路径，并允许执行？"))!=QMessageBox::Yes)return;
        // The modal dialog runs a nested event loop: vision/control timers may
        // have stopped or faulted the belt while the operator was confirming.
        require(controller_&&controller_->snapshot().state==conveyor::State::Holding,"Belt holding was lost during confirmation");
        const auto latest=arm_({{"action","status"}});
        require(latest.value("enabled").toBool()&&!latest.value("moving").toBool()&&!latest.value("actionPlaying").toBool(),"Arm state changed during confirmation");
        require(SortRecipe::referenceMatches(latest.value("visionReference").toObject(),state.value("visionReference").toObject()),"Arm pose/model changed during confirmation");
        observationReference_=state.value("visionReference").toObject();
        controller_->beginPick(now());
        locked=true;
        require(controller_->snapshot().state==conveyor::State::Picking,"Cannot lock conveyor for picking");
        queued=true;
        const auto result=arm_({{"action","play-actions"},{"confirm",true}});
        require(result.value("ok").toBool(),result.value("error").toString().toUtf8().constData());
        armActionJobId_=unsigned(result.value("actionJobId").toInt());
        require(armActionJobId_>0&&result.value("actionJobState").toString()=="running","Arm did not acknowledge an action task");
        armJob_=true;pickStart_=now();event(QStringLiteral("抓放动作组执行中；带面禁能，暂停相机移动期间的目标跟踪"));
    }catch(const std::exception &e){if(queued)arm_({{"action","stop"}});if(locked)stop();fail(QString::fromUtf8(e.what()));}refresh();
}
void ConveyorPage::release(){
    operationOk_=true;
    try {
        require(bool(controller_),"Controller disconnected");
        require(!armJob_,"Cannot release while an owned arm job is active");
        const auto state=arm_({{"action","status"}});
        require(!state.value("moving").toBool()&&!state.value("actionPlaying").toBool(),"Arm is still moving");
        if(watch_->isChecked()&&!observationReference_.isEmpty())require(SortRecipe::referenceMatches(state.value("visionReference").toObject(),observationReference_),"Return arm camera to calibrated observation pose before release");
        controller_->release(now());releasedObjectId_=sourceObjectId_;++objectId_;sourceClass_.clear();awaitEntry_=watch_->isChecked();
        plantX_=config_.target-15;plantV_=0;lastFrame_=now();message_.clear();event(QStringLiteral("已放行，继续进料"));
    }catch(const std::exception &e){fail(QString::fromUtf8(e.what()));}refresh();
}
void ConveyorPage::readVision(){
    const auto f=readJson(visionPath_->text());const auto seq=integer(f.value("sequence").isUndefined()?f.value("frame"):f.value("sequence"));
    if(sourceSeen_&&seq<=sourceSequence_)return;
    // Prefer capture_timestamp_ms; old NPU result timestamps get a conservative
    // 120 ms inference-age allowance until the producer publishes capture time.
    const bool capture=f.contains("capture_timestamp_ms");
    require(mode_->currentIndex()!=1||capture,"Hardware vision requires capture_timestamp_ms from acquisition");
    const auto stamp=integer(capture?f.value("capture_timestamp_ms"):f.value("timestamp_ms"));
    const auto wall=uint64_t(QDateTime::currentMSecsSinceEpoch());
    require(stamp<=wall,"Future visual frame");
    if(stamp<startWallMs_&&!sourceSeen_)return;
    const bool softwareCapture=f.value("capture_timestamp_source").toString()=="dma_submit_lower_bound";
    require(mode_->currentIndex()!=1||!softwareCapture||f.contains("capture_age_guard_ms"),"Measure sensor exposure/readout age guard before hardware feeding");
    const uint64_t guard=softwareCapture&&f.contains("capture_age_guard_ms")?integer(f.value("capture_age_guard_ms")):0;
    require(guard<=10000,"Capture age guard outside limits");
    const uint64_t age=wall-stamp+(capture?guard:120);require(age<=config_.visionTimeoutMs&&age<=now(),"Visual result too old");
    const auto captureMs=now()-age;
    require(!sourceSeen_||captureMs>sourceCapture_,"Visual capture time moved backwards");
    QPointF xy;QString selectedClass;uint64_t id=objectId_;
    if(f.value("coordinate_frame").toString()=="belt_cm"){
        const auto pair=f.value("xy_cm").toArray();require(pair.size()==2,"Expected xy_cm pair");
        xy=QPointF(numeric(pair[0]),numeric(pair[1]));id=integer(f.value("object_id"));
        if(awaitEntry_&&id==releasedObjectId_){sourceSequence_=seq;sourceCapture_=captureMs;sourceSeen_=true;return;}
        awaitEntry_=false;
        selectedClass=f.value("class_name").toString();
        if(!sourceClass_.isEmpty())require(selectedClass==sourceClass_,"Tracked object's class changed");
    }else{
        require(f.value("coordinate_frame").toString()=="camera_pixel","Expected belt_cm or camera_pixel coordinates");
        const auto cal=readJson(calibrationPath_->text());
        const auto state=arm_({{"action","status"}});
        const auto reference=cal.contains("visionReference")?cal.value("visionReference").toObject():observationReference_;
        require(state.value("mode").toString()=="hardware"&&state.value("enabled").toBool()&&
                state.value("initialPoseAcknowledged").toBool()&&!state.value("moving").toBool()&&!state.value("actionPlaying").toBool()&&
                SortRecipe::referenceMatches(state.value("visionReference").toObject(),reference),"Camera observation pose changed");
        QVector<QPointF> candidates;QStringList labels;
        for(const auto &value:f.value("objects").toArray()){
            const auto o=value.toObject();const auto name=o.value("class_name").toString(o.value("color").toString());
            if(!className_->text().isEmpty()&&name!=className_->text())continue;
            if(!sourceClass_.isEmpty()&&name!=sourceClass_)continue;
            if(numeric(o.value("detection_confidence"))<.5)continue;
            QPointF point;
            try{point=project(cal,QPointF(numeric(o.value("cx")),numeric(o.value("cy"))),f);}catch(const std::exception &){continue;}
            if(point.y()<config_.lateralMin||point.y()>config_.lateralMax||point.x()>config_.target+config_.tolerance||point.x()<config_.target-30)continue;
            candidates.append(point);labels.append(name);
        }
        require(candidates.size()<=1,"Multiple candidate objects in belt region; refine class/ROI");
        // After release, keep the global frame cursor and accept a new pixel
        // target only upstream of the slowdown zone. An old file or the
        // just-released object at the line must not acquire a new synthetic ID.
        if(candidates.isEmpty()||(awaitEntry_&&candidates.first().x()>config_.target-config_.slowDistance)){
            controller_->observe(conveyor::Observation{id,seq,captureMs,0,0,false},now());
            sourceSequence_=seq;sourceCapture_=captureMs;sourceSeen_=true;return;
        }
        xy=candidates.first();selectedClass=labels.first();awaitEntry_=false;
        if(sourceSeen_&&!sourceClass_.isEmpty()){
            const double dt=(captureMs-sourceCapture_)/1000.0;
            require(std::hypot(xy.x()-lastX_,xy.y()-lastY_)<=config_.maxSpeed*dt*2+.5,"Visual target association jump");
        }
    }
    controller_->observe(conveyor::Observation{id,seq,captureMs,xy.x(),xy.y()},now());
    sourceSequence_=seq;sourceCapture_=captureMs;sourceSeen_=true;
    if(!selectedClass.isEmpty())sourceClass_=selectedClass;
    sourceObjectId_=id;
    lastX_=xy.x();lastY_=xy.y();
}
void ConveyorPage::tick(){
    if(!controller_)return;
    const auto time=now();const double dt=lastTick_?std::min(.1,(time-lastTick_)/1000.0):.02;lastTick_=time;
    auto *mock=dynamic_cast<conveyor::MockRegisters*>(registers_.get());
    auto current=controller_->snapshot().state;
    try {
        if(mock){
            const double goal=(mock->read(conveyor::Status)&1)?mock->read(conveyor::ActiveDuty)/double(mock->read(conveyor::Period))*4.054:0;
            plantV_+=(goal-plantV_)*std::min(1.0,dt/.08);plantX_+=plantV_*dt;
            if(config_.encoderEnabled){encoderPosition_+=plantV_*dt/config_.cmPerCount*config_.encoderSign;
                const auto n=int64_t(std::llround(encoderPosition_));mock->advanceEncoder(int32_t(n-encoderCount_));encoderCount_=n;}
        }
        const bool active=current!=conveyor::State::Idle&&current!=conveyor::State::Fault&&current!=conveyor::State::Picking;
        if(active&&time-lastFrame_>=125){lastFrame_=time;
            if(watch_->isChecked()&&!demo_)readVision();
            else if(mock){lastX_=plantX_;lastY_=y_->value();controller_->observe(conveyor::Observation{objectId_,++sequence_,time,lastX_,lastY_},time);}
        }
        // JSON I/O and projection may have advanced the clock in observe().
        // Do not reuse the earlier plant-simulation timestamp for control.
        controller_->tick(now());current=controller_->snapshot().state;
        if(current!=previous_){event(stateText(current));previous_=current;
            if(current==conveyor::State::Holding){observationReference_=arm_({{"action","status"}}).value("visionReference").toObject();preview();}}
        if(current==conveyor::State::Fault&&armJob_){arm_({{"action","stop"}});armJob_=false;}
        if(demo_&&current==conveyor::State::Holding){controller_->beginPick(now());pickStart_=now();event(QStringLiteral("虚拟抓取中（机械臂未运动）"));}
        if(autoSort_&&current==conveyor::State::Holding&&!armJob_)beginSort();
        if(demo_&&current==conveyor::State::Picking&&time-pickStart_>=1000){release();++completed_;stop();message_=QStringLiteral("模拟联调完成：已到位、虚拟抓取并放行；已停止。实机运动未测试。");event(message_);}
        if(armJob_){const auto a=arm_({{"action","status"}});
            if(!a.value("enabled").toBool()||!a.value("error").toString().isEmpty()||!a.value("uiError").toString().isEmpty()||
               (!sortJob_&&(unsigned(a.value("actionJobId").toInt())!=armActionJobId_||a.value("actionJobState").toString()=="interrupted"||a.value("actionJobState").toString()=="failed"))||
               (sortJob_&&(unsigned(a.value("sortJobId").toInt())!=sortJobId_||a.value("sortJobState").toString()=="interrupted"||a.value("sortJobState").toString()=="failed"))){stop();fail(QStringLiteral("机械臂动作异常或中断，联动已停止"));}
            else if(!a.value("moving").toBool()&&!a.value("actionPlaying").toBool()){
                if(sortJob_){
                    require(a.value("sortJobState").toString()=="completed","Sorting queue ended without completion");
                    require(SortRecipe::referenceMatches(a.value("visionReference").toObject(),observationReference_),"Sorting return observation pose mismatch");
                    armJob_=false;sortJob_=false;release();require(operationOk_,"Sorting release failed");++sorted_;
                    event(QStringLiteral("分类放置完成，已返回观察姿态并放行下一件"));
                }else{require(a.value("actionJobState").toString()=="completed","Action queue ended without completion");armJob_=false;event(QStringLiteral("动作组已结束，等待避让和拍摄姿态确认"));if(autoRelease_->isChecked())release();}}}
    }catch(const std::exception &e){stop();fail(QString::fromUtf8(e.what()));}
    refresh();
}
void ConveyorPage::refresh(){
    if(!state_)return;
    const bool mock=bool(dynamic_cast<conveyor::MockRegisters*>(registers_.get()));
    banner_->setText(mock?QStringLiteral("模拟联调 · 传送带不输出 GPIO / PWM · 机械臂动作由显式操作发起"):
        controller_?QStringLiteral("硬件 UIO 已连接 · 运行前须核对驱动、电平与编码器标定"):QStringLiteral("传送带未连接 · 请检查模式、设备和配置"));
    const auto s=controller_?controller_->snapshot():conveyor::Snapshot();state_->setText(stateText(s.state));
    position_->setText(QStringLiteral("X %1 / Y %2 cm   ·   目标 X %3 cm").arg(lastX_,0,'f',2).arg(lastY_,0,'f',2).arg(config_.target,0,'f',2));
    speedText_->setText(QStringLiteral("目标 %1 cm/s   编码器 %2 cm/s   PWM %3%   完成演示 %4 次")
        .arg(s.targetSpeed,0,'f',2).arg(s.measuredSpeed,0,'f',2).arg(s.duty*100,0,'f',1).arg(completed_));
    if(autoSort_||sorted_)speedText_->setText(speedText_->text()+QStringLiteral("   ·   分类 %1 件  %2 → %3").arg(sorted_).arg(sortedClass_,sortedZone_));
    error_->setText(s.state==conveyor::State::Fault?QString::fromStdString(s.reason):message_);
    view_->x=lastX_;view_->y=lastY_;view_->target=config_.target;view_->simulation=mock;view_->ready=s.ready;view_->visibleObject=s.state!=conveyor::State::Idle;view_->update();
}
QJsonObject ConveyorPage::status() const {
    const auto s=controller_?controller_->snapshot():conveyor::Snapshot();
    return {{"ok",true},{"connected",bool(controller_)},{"mode",!controller_?"disconnected":dynamic_cast<conveyor::MockRegisters*>(registers_.get())?"simulation":"hardware"},
        {"automaticSorting",autoSort_},{"sortedCount",int(sorted_)},{"lastSortedClass",sortedClass_},{"lastSortedZone",sortedZone_},
        {"state",conveyor::stateName(s.state)},{"ready",s.ready},{"xCm",lastX_},{"yCm",lastY_},{"targetCm",config_.target},
        {"targetSpeedCmS",s.targetSpeed},{"measuredSpeedCmS",s.measuredSpeed},{"duty",s.duty},{"completedDemos",int(completed_)},
        {"watchVision",watch_->isChecked()},{"armJob",armJob_},{"error",QString::fromStdString(s.reason)},{"message",message_}};
}
QJsonObject ConveyorPage::handleConsoleCommand(const QJsonObject &c){
    const auto action=c.value("action").toString();
    operationOk_=true;
    try {
        if(action=="conveyor-start")start(false);else if(action=="conveyor-demo")start(true);
        else if(action=="conveyor-auto-start"){require(c.value("confirm").toBool(),"Automatic start requires confirm=true");start(false,true);}
        else if(action=="conveyor-stop")stop();
        else if(action=="conveyor-reset"){require(bool(controller_),"Not connected");controller_->resetFault(now());message_.clear();}
        else if(action=="conveyor-release"){require(c.value("confirm").toBool(),"release requires confirm=true");release();}
        else if(action=="conveyor-preview")preview();
        else if(action=="conveyor-configure"){
            stop();if(c.contains("targetCm"))target_->setValue(numeric(c.value("targetCm")));
            if(c.contains("zCm"))z_->setValue(numeric(c.value("zCm")));
            if(c.contains("pitchDeg"))pitch_->setValue(numeric(c.value("pitchDeg")));
            if(c.contains("visionPath"))visionPath_->setText(c.value("visionPath").toString());
            if(c.contains("configPath"))configPath_->setText(c.value("configPath").toString());
            if(c.contains("calibrationPath"))calibrationPath_->setText(c.value("calibrationPath").toString());
            if(c.contains("className"))className_->setText(c.value("className").toString());
            if(c.contains("sortConfig")){require(c.value("sortConfig").isObject(),"sortConfig must be an object");sortSettings_->setConfig(c.value("sortConfig").toObject());}
            if(c.contains("watchVision"))watch_->setChecked(c.value("watchVision").toBool());
            connectBackend();
        }else if(action=="conveyor-screenshot"){
            require(!controller_||controller_->snapshot().state==conveyor::State::Idle||controller_->snapshot().state==conveyor::State::Fault,
                    "Stop conveyor before exporting screenshot");
            const auto path=c.value("path").toString();require(path.startsWith("/tmp/")&&!path.contains("..")&&path.endsWith(".png"),"Screenshot must be /tmp/*.png");
            require(window()->grab().save(path),"Cannot save UI screenshot");
        }else if(action=="conveyor-show"){
            if(c.contains("tab")){const auto tab=c.value("tab").toString();require(tab=="run"||tab=="vision"||tab=="sorting","Unknown conveyor tab");findChild<QTabWidget*>("conveyorTabs")->setCurrentIndex(tab=="sorting"?2:tab=="vision"?1:0);}
        }else if(action!="conveyor-status")throw std::runtime_error("Unknown conveyor action");
        refresh();auto result=status();result["ok"]=operationOk_;if(!operationOk_)result["error"]=message_;return result;
    }catch(const std::exception &e){return {{"ok",false},{"error",QString::fromUtf8(e.what())}};}
}
