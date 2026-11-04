#include "../arm_actions.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include <cstdio>
#include <limits>

static int failures=0;
static void check(bool ok,const char *name){if(!ok){std::fprintf(stderr,"FAIL: %s\n",name);++failures;}}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);std::array<double,6> lo,hi;lo.fill(-90);hi.fill(90);
    const QByteArray valid="{\"version\":1,\"frames\":[{\"anglesDeg\":[0,1,2,3,4,5],\"durationMs\":200,\"holdMs\":50},{\"anglesDeg\":[5,4,3,2,1,0],\"durationMs\":400,\"holdMs\":0}]}";
    ArmActions actions;QString error;check(actions.load(valid,lo,hi,error),"valid JSON");check(actions.frames.size()==2,"two frames");
    const QByteArray bad[]={"{", "[]", "{\"version\":2,\"frames\":[]}",
        QByteArray(valid).replace("[0,1,2,3,4,5]","[0,1,2,3,4]"),
        QByteArray(valid).replace("[0,1,2,3,4,5]","[91,1,2,3,4,5]"),
        QByteArray(valid).replace("[0,1,2,3,4,5]","[null,1,2,3,4,5]"),
        QByteArray(valid).replace("[0,1,2,3,4,5]","[1e999,1,2,3,4,5]"),
        QByteArray(valid).replace("200","19"), QByteArray(valid).replace("200","200.5"),
        QByteArray(valid).replace("200","\"200\""), QByteArray(valid).replace("50","-1"),
        QByteArray(valid).replace("200","60001"), QByteArray(valid).replace("50","60001")};
    for(const auto &data:bad){check(!actions.load(data,lo,hi,error),"invalid JSON rejected");check(actions.frames.size()==2,"failed load retains previous group");}
    ArmActions nonfinite=actions;nonfinite.frames[0].angles[0]=std::numeric_limits<double>::infinity();
    ArmActions parsed;check(!parsed.load(nonfinite.json(),lo,hi,error),"nonfinite serialization rejected");
    ArmActions tooMany;for(int i=0;i<1001;++i)tooMany.frames.append(actions.frames[0]);
    check(!parsed.load(tooMany.json(),lo,hi,error),"frame count bounded");
    QTemporaryDir temp;check(temp.isValid(),"temp dir");check(actions.save(temp.path()+"/actions.json",error),"atomic save");
    QFile file(temp.path()+"/actions.json");check(file.open(QIODevice::ReadOnly),"saved file open");check(parsed.load(file.readAll(),lo,hi,error),"save roundtrip");
    check(!actions.save(temp.path()+"/absent/actions.json",error),"save error propagated");

    const QByteArray coordinateJson=R"({"version":2,"coordinateFrame":{"enabled":true,"originBaseCm":[12.5,0,20],"debugMapping":true,"referencePitchDeg":-45},"frames":[{"type":"coordinate","coordinate":[1,0,-2,-45],"toolAnglesDeg":[10,20],"durationMs":100,"holdMs":0},{"type":"angles","anglesDeg":[0,1,2,3,4,5],"durationMs":200,"holdMs":50}]})";
    ArmActions coordinates;
    check(coordinates.load(coordinateJson,lo,hi,error),"coordinate and angle frames load together");
    check(coordinates.usesCoordinates()&&coordinates.frames.size()==2&&coordinates.frames[0].kind==ArmActionFrame::Kind::Coordinate,"coordinate frame retains its type");
    check(coordinates.frames[0].coordinate[2]==-2&&coordinates.frames[0].angles[1]==20,"coordinate and tool targets retained");
    check(coordinates.matchesCoordinateFrame(coordinates.coordinateFrame),"same origin matches");
    auto changed=coordinates.coordinateFrame;changed.originCm[0]+=1;
    check(!coordinates.matchesCoordinateFrame(changed),"changed origin is rejected");
    changed=coordinates.coordinateFrame;changed.debugMapping=false;
    check(!coordinates.matchesCoordinateFrame(changed),"changed mapping is rejected");
    check(parsed.load(coordinates.json(),lo,hi,error)&&parsed.frames[0].coordinate==coordinates.frames[0].coordinate,"coordinate JSON round trip");
    const QByteArray badCoordinates[]={
        QByteArray(coordinateJson).replace("\"enabled\":true","\"enabled\":false"),
        QByteArray(coordinateJson).replace("[12.5,0,20]","[12.5,null,20]"),
        QByteArray(coordinateJson).replace("[1,0,-2,-45]","[1,0,null,-45]"),
        QByteArray(coordinateJson).replace("[10,20]","[10,91]"),
        QByteArray(coordinateJson).replace("\"coordinate\",\"coordinate\"","\"unknown\",\"coordinate\""),
        QByteArray(coordinateJson).replace("\"type\":\"coordinate\"","\"anglesDeg\":[0,0,0,0,0,0],\"type\":\"coordinate\"")};
    for(const auto &data:badCoordinates){check(!coordinates.load(data,lo,hi,error),"malformed coordinate action rejected");check(coordinates.frames.size()==2,"failed coordinate load retains action group");}
    coordinates.frames[0].coordinate[0]=std::numeric_limits<double>::infinity();
    check(!parsed.load(coordinates.json(),lo,hi,error),"nonfinite coordinate serialization rejected");
    ArmActionPlayer player;int started=0;
    const ArmActionPlayer::Start start=[&](const ArmActionFrame&,QString&){++started;return true;};
    player.play(actions);player.tick(0,false,QString(),start);check(started==1&&player.active(),"first frame starts");
    player.tick(100,true,QString(),start);check(started==1,"wait while moving");
    player.tick(200,false,QString(),start);check(started==1,"hold starts after completion");
    player.tick(249,false,QString(),start);check(started==1,"hold not expired");
    player.tick(250,false,QString(),start);check(started==2,"next frame at hold expiry");
    player.tick(650,false,QString(),start);check(!player.active(),"completed group");
    player.play(actions);player.tick(700,false,QString(),start);player.stop();int before=started;
    player.tick(5000,false,QString(),start);check(started==before&&!player.active(),"stop cancels all queued frames");
    player.play(actions);player.tick(0,false,QString(),start);player.tick(200,false,QString(),start);
    player.stop();before=started;player.tick(1000,false,QString(),start);
    check(started==before&&!player.active(),"stop during hold cancels next frame");
    player.play(actions);player.tick(0,false,QString(),[](const ArmActionFrame&,QString&e){e="rejected";return false;});
    check(!player.active()&&player.error()=="rejected","start failure cancels queue");before=started;
    player.tick(100,false,QString(),start);check(started==before,"failed frame never auto resumes");
    player.play(actions);player.tick(0,false,QString(),start);player.tick(20,true,"GPIO failure",start);
    check(!player.active()&&player.error()=="GPIO failure","backend error cancels queue");before=started;
    player.tick(5000,false,QString(),start);check(started==before,"backend failure never resumes");
    player.stop();check(player.error().isEmpty(),"explicit stop clears playback error");
    std::printf("action tests: %s (%d failures)\n",failures?"FAIL":"PASS",failures);return failures?1:0;
}
