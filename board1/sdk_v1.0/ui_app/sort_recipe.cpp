#include "sort_recipe.hpp"
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
namespace {
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
double number(const QJsonValue &v){check(v.isDouble()&&std::isfinite(v.toDouble()),"Sorting parameter missing or nonfinite");return v.toDouble();}
int timeValue(const QJsonObject &o,const char *key,int lo){const double n=number(o.value(key));check(n==std::floor(n)&&n>=lo&&n<=10000,"Sorting time outside limits");return int(n);}
}
bool SortRecipe::referenceMatches(QJsonObject a,QJsonObject b){
    const auto ap=a.take("pulses").toArray(),bp=b.take("pulses").toArray();
    if(a!=b||ap.size()!=6||bp.size()!=6)return false;
    for(int i=0;i<6;++i)if(!ap[i].isDouble()||!bp[i].isDouble()||!std::isfinite(ap[i].toDouble())||
        !std::isfinite(bp[i].toDouble())||std::abs(ap[i].toDouble()-bp[i].toDouble())>1e-6)return false;
    return true;
}
SortRecipe SortRecipe::build(const QJsonObject &c,const QString &name,double x,double y){
    check(c.value("version").toInt()==1&&c.value("confirmed").isBool()&&c.value("confirmed").toBool(),"Confirm measured sorting parameters first");
    check(c.value("coordinate_frame").toString()=="arm_current_cm"&&c.value("unknown_class_policy").toString()=="hold","Unsupported sorting coordinate frame or unknown-class policy");
    check(std::isfinite(x)&&std::isfinite(y)&&!name.isEmpty(),"Sorting requires stopped XY and class_name");
    SortRecipe r;r.className=name;
    r.reference=c.value("vision_reference").toObject();
    check(!r.reference.isEmpty(),"Record the calibrated observation pose/model first");
    const auto pulses=c.value("observation_pulses_us").toArray();
    check(pulses.size()==6,"Observation pose needs six pulses");
    const auto refPulses=r.reference.value("pulses").toArray();check(refPulses.size()==6,"Observation reference missing pulses");
    for(int i=0;i<6;++i){r.observationPulses[i]=number(pulses[i]);check(r.observationPulses[i]>=500&&r.observationPulses[i]<=2499.2&&
        std::abs(r.observationPulses[i]-number(refPulses[i]))<1e-6,"Observation pulses/reference mismatch");}
    const auto zone=c.value("zones").toObject().value(name).toObject();
    check(!zone.isEmpty(),"Unknown class: hold belt for operator");
    r.zone=zone.value("zone").toString();check(!r.zone.isEmpty(),"Missing sorting zone name");
    const auto xyz=zone.value("place_xyz_cm").toArray();check(xyz.size()==3,"Placement needs XYZ in cm");
    const double px=number(xyz[0]),py=number(xyz[1]),pz=number(xyz[2]);
    const double pick=number(c.value("pick_z_cm")),lift=number(c.value("safe_lift_z_cm")),pitch=number(c.value("pitch_deg"));
    const double open=number(c.value("gripper_open_deg")),closed=number(c.value("gripper_closed_deg")),roll=number(c.value("wrist_roll_deg"));
    check(lift>pick&&lift>pz,"Safe lift Z must exceed both pick and place Z");
    check(open>=0&&open<=180&&closed>=0&&closed<=180&&open!=closed&&roll>=0&&roll<=180,"Invalid gripper/wrist servo angles");
    check(pitch>=-180&&pitch<=180,"Invalid sorting pitch");
    const int move=timeValue(c,"move_ms",100),grip=timeValue(c,"grip_ms",100),settle=timeValue(c,"settle_ms",0);
    r.returnMs=move;
    auto add=[&](const char *label,double a,double b,double z,double claw,int duration,int hold){
        SortWaypoint w;w.name=QString::fromLatin1(label);w.coordinate={{a,b,z,pitch}};w.claw=claw;w.roll=roll;w.durationMs=duration;w.holdMs=hold;r.waypoints.append(w);
    };
    add("approach",x,y,lift,open,move,0);
    add("descend-pick",x,y,pick,open,move,settle);
    add("grip",x,y,pick,closed,grip,settle);
    add("lift",x,y,lift,closed,move,0);
    add("transfer",px,py,lift,closed,move,0);
    add("descend-place",px,py,pz,closed,move,settle);
    add("ungrip",px,py,pz,open,grip,settle);
    add("retreat",px,py,lift,open,move,0);
    return r;
}
