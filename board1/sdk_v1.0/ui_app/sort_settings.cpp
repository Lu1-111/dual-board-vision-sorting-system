#include "sort_settings.hpp"
#include <QCheckBox>
#include <QFile>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>
namespace {
const char *keys[]={"pick_z_cm","safe_lift_z_cm","pitch_deg","gripper_open_deg","gripper_closed_deg","wrist_roll_deg","move_ms","grip_ms","settle_ms"};
const char *classes[]={"red","yellow","blue","green_good","green_defect"};
QJsonValue field(const QString &s){bool ok=false;const double n=s.toDouble(&ok);return ok&&std::isfinite(n)?QJsonValue(n):QJsonValue(QJsonValue::Null);}
}
SortSettings::SortSettings(std::function<QJsonObject()> status,std::function<void()> changed,QWidget *parent):QWidget(parent){
    setObjectName("sortSettings");setStyleSheet("QWidget#sortSettings {background:#111e30;} QTableWidget {background:#17283d;color:#dce7f7;gridline-color:#30455e;} QHeaderView::section {background:#20354b;color:#dce7f7;padding:5px;} QTableCornerButton::section {background:#20354b;}");
    auto *layout=new QVBoxLayout(this);auto *form=new QFormLayout;
    auto *intro=new QLabel(QStringLiteral("每个识别类别对应一个放置区。坐标单位 cm，夹爪与腕转角使用舵机角度。填写实测值并核对路径后确认；保存配置不会启动运动。"));intro->setWordWrap(true);layout->addWidget(intro);
    const QString labels[]={QStringLiteral("抓取 Z / cm"),QStringLiteral("抬升 Z / cm"),QStringLiteral("末端俯仰 / °"),QStringLiteral("夹爪张开 / °"),QStringLiteral("夹爪闭合 / °"),QStringLiteral("腕转舵机角 / °"),QStringLiteral("每段运动 / ms"),QStringLiteral("夹爪运动 / ms"),QStringLiteral("夹取 / 放置后停留 / ms")};
    for(int i=0;i<9;++i){fields_[i]=new QLineEdit;fields_[i]->setPlaceholderText(QStringLiteral("待测量 / 未填写"));fields_[i]->setObjectName(QString::fromLatin1(keys[i]));form->addRow(labels[i],fields_[i]);
        connect(fields_[i],&QLineEdit::textChanged,this,[this,changed](const QString &){confirmed_->setChecked(false);changed();});}
    layout->addLayout(form);zones_=new QTableWidget(5,4);zones_->setObjectName("sortZones");zones_->setHorizontalHeaderLabels({QStringLiteral("放置区"),"X / cm","Y / cm","Z / cm"});
    zones_->setVerticalHeaderLabels({QStringLiteral("红色 red"),QStringLiteral("黄色 yellow"),QStringLiteral("蓝色 blue"),QStringLiteral("绿色良品 green_good"),QStringLiteral("绿色缺陷 green_defect")});
    zones_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);zones_->setMinimumHeight(210);
    for(int r=0;r<5;++r)for(int c=0;c<4;++c)zones_->setItem(r,c,new QTableWidgetItem(c==0?QString(QChar('A'+r)):QString()));
    connect(zones_,&QTableWidget::itemChanged,this,[this,changed](QTableWidgetItem *){confirmed_->setChecked(false);changed();});layout->addWidget(zones_);
    auto *record=new QPushButton(QStringLiteral("记录当前观察姿态与坐标配置"));observation_=new QLabel(QStringLiteral("尚未记录观察姿态"));observation_->setWordWrap(true);layout->addWidget(record);layout->addWidget(observation_);
    connect(record,&QPushButton::clicked,this,[=](){changed();const auto s=status();
        if(!s.value("connected").toBool()||!s.value("initialPoseAcknowledged").toBool()||s.value("moving").toBool()||s.value("actionPlaying").toBool()||!s.value("coordinateFrameCompatible").toBool()){
            message_->setText(QStringLiteral("请先确认机械臂初始姿态和坐标配置，并保持停止。"));return;}
        reference_=s.value("visionReference").toObject();confirmed_->setChecked(false);observation_->setText(QStringLiteral("已记录观察姿态；机械臂每次分拣结束后返回此姿态。"));message_->clear();});
    confirmed_=new QCheckBox(QStringLiteral("已完成实物标定并核对抓放路径"));confirmed_->setObjectName("sortConfirmed");layout->addWidget(confirmed_);
    connect(confirmed_,&QCheckBox::toggled,this,[changed](bool){changed();});
    path_=new QLineEdit("/home/uisrc/qt_play/sort-zones.json");layout->addWidget(path_);auto *buttons=new QHBoxLayout;
    auto *load=new QPushButton(QStringLiteral("载入分拣配置")),*save=new QPushButton(QStringLiteral("保存分拣配置"));buttons->addWidget(load);buttons->addWidget(save);layout->addLayout(buttons);
    message_=new QLabel;message_->setWordWrap(true);layout->addWidget(message_);
    connect(save,&QPushButton::clicked,this,[=](){QSaveFile f(path_->text());const auto bytes=QJsonDocument(config()).toJson();
        const bool ok=f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();message_->setText(ok?QStringLiteral("分拣配置已保存"):f.errorString());});
    connect(load,&QPushButton::clicked,this,[=](){changed();QFile f(path_->text());QJsonParseError e;
        if(!f.open(QIODevice::ReadOnly)||f.size()>262144){message_->setText(QStringLiteral("无法读取分拣配置"));return;}
        auto d=QJsonDocument::fromJson(f.readAll(),&e);if(e.error!=QJsonParseError::NoError||!d.isObject()||d.object().value("version").toInt()!=1){message_->setText(QStringLiteral("分拣配置格式错误"));return;}
        setConfig(d.object());message_->setText(QStringLiteral("已载入配置；启动前会检查完整参数和观察姿态。"));});
    layout->addStretch();
}
QJsonObject SortSettings::config() const {
    QJsonObject c{{"version",1},{"coordinate_frame","arm_current_cm"},{"unknown_class_policy","hold"},{"confirmed",confirmed_->isChecked()},
        {"vision_reference",reference_},{"observation_pulses_us",reference_.value("pulses")}};
    for(int i=0;i<9;++i)c[keys[i]]=field(fields_[i]->text());
    QJsonObject z;for(int r=0;r<5;++r)z[classes[r]]=QJsonObject{{"zone",zones_->item(r,0)->text()},
        {"place_xyz_cm",QJsonArray{field(zones_->item(r,1)->text()),field(zones_->item(r,2)->text()),field(zones_->item(r,3)->text())}}};
    c["zones"]=z;return c;
}
void SortSettings::setConfig(const QJsonObject &c){
    confirmed_->setChecked(false);reference_=c.value("vision_reference").toObject();
    for(int i=0;i<9;++i)fields_[i]->setText(c.value(keys[i]).isDouble()?QString::number(c.value(keys[i]).toDouble(),'g',15):QString());
    auto zones=c.value("zones").toObject();for(int r=0;r<5;++r){const auto z=zones.value(classes[r]).toObject();auto xyz=z.value("place_xyz_cm").toArray();
        zones_->item(r,0)->setText(z.value("zone").toString(QString(QChar('A'+r))));
        for(int i=0;i<3;++i)zones_->item(r,i+1)->setText(xyz.size()==3&&xyz[i].isDouble()?QString::number(xyz[i].toDouble(),'g',15):QString());}
    observation_->setText(reference_.isEmpty()?QStringLiteral("尚未记录观察姿态"):QStringLiteral("已载入观察姿态和坐标配置"));
    confirmed_->setChecked(c.value("confirmed").isBool()&&c.value("confirmed").toBool());
}
