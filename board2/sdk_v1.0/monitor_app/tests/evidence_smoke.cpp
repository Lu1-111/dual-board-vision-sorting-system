#include "../aipage.h"
#include "../evidence_store.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QTextBrowser>
#include <QThread>
#include <cstdio>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void tick(QApplication &a) { for(int i=0;i<120;++i) { a.processEvents(); QThread::msleep(10); } }
int main(int argc,char **argv) {
    QApplication app(argc,argv); QTemporaryDir temp; qputenv("MONITOR_EVIDENCE_DIR",temp.path().toUtf8());
    AiPage page; page.resize(790,460); page.show();
    AlarmEvent e; e.sessionId=77; e.eventId=4; e.type=AlarmType::MaterialStuck; page.alarmRaised(e);
    QImage image(160,90,QImage::Format_RGB32); image.fill(Qt::red); QByteArray jpeg;
    QBuffer buf(&jpeg); buf.open(QIODevice::WriteOnly); CHECK(image.save(&buf,"JPEG"));
    QJsonObject o{{"session_id",77},{"event_id",4},{"alarm_type",2},{"is_anomaly",true},{"source","test"},{"sha256",Evidence::sha(jpeg)},{"image_b64",QString::fromLatin1(jpeg.toBase64())}};
    QJsonObject bad=o; bad["is_anomaly"]=false; CHECK(!Evidence::accept(bad)["ok"].toBool());
    bad=o; bad["session_id"]=78; CHECK(!Evidence::accept(bad)["ok"].toBool());
    bad=o; bad["alarm_type"]=1; CHECK(!Evidence::accept(bad)["ok"].toBool());
    bad=o; bad["sha256"]="bad"; CHECK(!Evidence::accept(bad)["ok"].toBool());
    bad=o; bad["source"]="camera"; CHECK(!Evidence::accept(bad)["ok"].toBool());
    CHECK(Evidence::accept(o)["ok"].toBool()); CHECK(Evidence::accept(o)["duplicate"].toBool());
    tick(app);
    auto button=page.findChild<QPushButton*>("aiExternal"); CHECK(button && button->isEnabled()); button->click();
    auto jobs=Evidence::rpc({{"op","poll"},{"provider","mock"}},false)["jobs"].toArray(); CHECK(jobs.size()==1);
    auto j=jobs[0].toObject(); QString id=j["request_id"].toString(); CHECK(!id.isEmpty());
    auto chunk=Evidence::rpc({{"op","image_chunk"},{"request_id",id},{"offset",0}},false);
    CHECK(QByteArray::fromBase64(chunk["data"].toString().toLatin1())==jpeg.left(1536));
    QJsonObject result{{"op","complete"},{"request_id",id},{"session_id",77},{"event_id",4},{"image_sha256","wrong"},{"status","mock"},{"analysis","test reply"}};
    CHECK(!Evidence::rpc(result,false)["ok"].toBool()); result["image_sha256"]=Evidence::sha(jpeg);
    CHECK(Evidence::rpc(result,false)["ok"].toBool()); tick(app);
    CHECK(page.findChild<QTextBrowser*>("aiResult")->toPlainText().contains("test reply"));
    AlarmEvent next=e; next.eventId=5; page.alarmRaised(next);
    auto list=page.findChild<QListWidget*>("aiAlarmList"); list->setCurrentRow(0); tick(app);
    CHECK(!button->isEnabled()); CHECK(!page.findChild<QTextBrowser*>("aiResult")->toPlainText().contains("test reply"));
    CHECK(page.findChild<QLabel*>("aiEvidenceImage")->text().contains(QStringLiteral("尚未收到")));
    page.resetSession(); CHECK(Evidence::events().isEmpty()); CHECK(!Evidence::accept(o)["ok"].toBool());
    CHECK(!Evidence::rpc(result,false)["ok"].toBool());
    puts("PASS: event/session identity, normal rejection, hash, idempotency, queue, text+image result, selection isolation"); return 0;
}
