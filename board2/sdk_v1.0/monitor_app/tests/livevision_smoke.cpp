// Synthetic fixtures test UI protocol/rendering, not classification accuracy.
#include "../livevisionpage.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <ctime>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static qint64 nowMs() { timespec t{}; clock_gettime(CLOCK_MONOTONIC,&t); return qint64(t.tv_sec)*1000+t.tv_nsec/1000000; }
static QPixmap pixmap(const QLabel *l) { return l->pixmap()?*l->pixmap():QPixmap(); }
static bool publish(const QString &dir,const QJsonObject &o) {
 QSaveFile f(dir+"/latest.json"); return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(o).toJson())>0&&f.commit();
}
static QJsonObject result(const QString &dir,const QString &session="camera-one",const QString &seq="1") {
 return QJsonObject{{"schema",1},{"status","ok"},{"valid",true},{"source_session",session},{"frame_sequence",seq},
 {"frame_path",dir+"/frame_"+session+"_"+seq+".jpg"},{"published_monotonic_ms",double(nowMs())},{"source_frame_age_ms",1500.0},
 {"class_names",QJsonArray{"empty","object_present","hand_present"}},{"class_index",1},{"probabilities",QJsonArray{.03,.92,.05}},
 {"confidence",.92},{"state","object_present"},{"threshold",.8},{"fps",.5},
 {"timing_ms",QJsonObject{{"preprocess",100.0},{"cpu",1800.0},{"npu",7.0},{"head",0.0},{"total",1907.0}}},
 {"model_id","effnetb0v3"},{"model_sha256","e6dbbc45c5a38117a8d9754e3b002032c937bbe312f0c50d0bc37ad7aaa4f016"},{"validation_status","board_regression_21_ok"}};
}
static QJsonObject video(const QString &seq,const QString &session="camera-one",double age=100) {
 return QJsonObject{{"source_session",session},{"frame_sequence",seq},{"published_monotonic_ms",double(nowMs())},
 {"source_frame_age_ms",age},{"width",640},{"height",480},{"fps",10.0}};
}
#include "../monitorwindow.h"
#include "../datasource.h"
#include <QStackedWidget>
class FixtureSource:public IDataSource { public: QString sourceName() const override{return "UI fixture";} bool isCan()const override{return true;} bool isSimulation()const override{return false;} };
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    qputenv("MONITOR_PREVIEW_DISABLED","1");
    if(argc>2){int id=QFontDatabase::addApplicationFont(QString::fromLocal8Bit(argv[2])+"/DroidSansFallbackFull.ttf");if(id>=0)app.setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(),10));}
    QTemporaryDir fixture;CHECK(fixture.isValid());
    if(argc>1)CHECK(QDir().mkpath(QString::fromLocal8Bit(argv[1])));
    qputenv("MONITOR_LIVE_DIR",fixture.path().toUtf8());
    FixtureSource source;MonitorWindow window(&source);
    window.resize(1024,600);window.show();window.showLivePage();app.processEvents();
    auto *pages=window.findChild<QStackedWidget*>();CHECK(pages&&pages->count()==3&&pages->currentIndex()==2);
    auto *page=static_cast<LiveVisionPage*>(pages->widget(2));

    auto label=[&](const char *name){return window.findChild<QLabel*>(name);};
    auto *prediction=label("livePrediction"),*preview=label("livePreview"),*status=label("liveStatus"),*info=label("liveResultInfo");
    CHECK(prediction&&preview&&status&&info);
    auto snapshot=[&](){
      QStringList values{prediction->text(),label("liveConfidence")->text(),label("liveInferenceState")->text()};
      for(int i=0;i<3;++i){auto *b=window.findChild<QProgressBar*>(QString("liveProbability%1").arg(i));values<<QString::number(b->value())<<b->format();}
      return values;
    };
    auto capture=[&](const QString &name){app.processEvents();return argc<=1||window.grab().save(QString::fromLocal8Bit(argv[1])+"/"+name+".png");};
    CHECK(prediction->text()==QStringLiteral("等待首次识别")&&pixmap(preview).isNull());
    for(int i=0;i<3;++i)CHECK(window.findChild<QProgressBar*>(QString("liveProbability%1").arg(i))->format()==QStringLiteral("待识别"));
    for(const char *name:{"livePrediction","liveConfidence","liveInferenceState","livePerformance","liveResultInfo"})CHECK(!label(name)->text().contains(QChar(0x2014)));
    CHECK(capture("retained-first-wait"));
    QImage red(640,480,QImage::Format_RGB32);red.fill(Qt::red);
    QImage blue(640,480,QImage::Format_RGB32);blue.fill(Qt::blue);
    auto good=result(fixture.path());CHECK(red.save(good["frame_path"].toString(),"JPG"));
    CHECK(publish(fixture.path(),good));page->refresh();
    CHECK(prediction->text()==QStringLiteral("预测：物料在位")&&pixmap(preview).isNull());
    page->receivePreview(blue,video("10"));page->refresh();
    CHECK(!pixmap(preview).isNull()&&pixmap(preview).toImage().pixelColor(0,0)==QColor(Qt::blue));
    const auto original=snapshot();
    page->receivePreview(red,video("11"));page->refresh();
    CHECK(snapshot()==original&&pixmap(preview).toImage().pixelColor(0,0)==QColor(Qt::red));
    CHECK(info->text()==QStringLiteral("最近识别"));
    CHECK(!label("liveFrameInfo"));
    for(const QString &failure:{QString("inference_error"),QString("connecting"),QString("source_stale")}) {
      auto bad=good;bad["status"]=failure;CHECK(publish(fixture.path(),bad));page->refresh();
      CHECK(snapshot()==original&&!pixmap(preview).isNull());
      CHECK(status->text().contains(QStringLiteral("保留上次结果")));
    }
    const QString ageBefore=info->text();QThread::msleep(180);page->refresh();
    CHECK(snapshot()==original&&info->text()==ageBefore); // Age is no longer displayed.
    CHECK(capture("retained-inference-error"));
    {QSaveFile f(fixture.path()+"/latest.json");CHECK(f.open(QIODevice::WriteOnly));CHECK(f.write("{")==1&&f.commit());}
    page->refresh();CHECK(snapshot()==original&&!pixmap(preview).isNull());
    QList<QJsonObject> invalid;
    auto bad=good;bad["model_id"]="scratch_v3";invalid<<bad;
    bad=good;bad["model_sha256"]=QString(64,'a');invalid<<bad;
    bad=good;bad["validation_status"]="failed_real_validation";invalid<<bad;
    bad=good;bad["valid"]=false;invalid<<bad;
    bad=good;bad["probabilities"]=QJsonArray{.9,.9,.9};invalid<<bad;
    bad=good;bad["confidence"]=.4;invalid<<bad;
    bad=good;bad["class_index"]=0;invalid<<bad;
    bad=good;bad["state"]="empty";invalid<<bad;
    bad=good;bad["threshold"]=.7;invalid<<bad;
    bad=good;bad["frame_sequence"]="3";invalid<<bad;
    bad=good;bad["frame_path"]="/tmp/outside.jpg";invalid<<bad;
    bad=good;bad.remove("source_frame_age_ms");invalid<<bad;
    bad=good;bad["source_frame_age_ms"]=-1;invalid<<bad;
    bad=good;bad["published_monotonic_ms"]=double(nowMs()+1000);invalid<<bad;
    for(const char *field:{"roi","model_roi","color_assist","fusion"}){bad=good;bad[field]=QJsonValue::Null;invalid<<bad;}
    for(const auto &v:invalid){CHECK(publish(fixture.path(),v));page->refresh();CHECK(snapshot()==original&&!pixmap(preview).isNull());}
    // Fresh but slower publication is accepted; cached data older than 5s is retained as history.
    auto slow=good;slow["published_monotonic_ms"]=double(nowMs()-3500);slow["source_frame_age_ms"]=1000.0;
    CHECK(publish(fixture.path(),slow));page->refresh();CHECK(snapshot()==original);
    auto aging=good;aging["published_monotonic_ms"]=double(nowMs());aging["source_frame_age_ms"]=4900.0;
    CHECK(publish(fixture.path(),aging));page->refresh();QThread::msleep(220);page->refresh();
    CHECK(snapshot()==original&&info->text().contains(QStringLiteral("历史识别"))&&status->text().contains(QStringLiteral("保留上次结果")));
    CHECK(capture("retained-over-five-seconds"));
    // A complete ten-second inference replaces the old snapshot, explicitly as history.
    auto delayed=result(fixture.path(),"camera-one","15");CHECK(red.save(delayed["frame_path"].toString(),"JPG"));
    delayed["source_frame_age_ms"]=10000.0;delayed["class_index"]=0;delayed["probabilities"]=QJsonArray{.9,.05,.05};
    delayed["confidence"]=.9;delayed["state"]="empty";
    CHECK(publish(fixture.path(),delayed));page->refresh();
    CHECK(prediction->text()==QStringLiteral("预测：输送带空载")&&info->text().contains(QStringLiteral("历史识别")));
    CHECK(info->text().contains(QStringLiteral("历史识别"))&&status->text().contains(QStringLiteral("保留上次结果")));
    const auto delayedSnapshot=snapshot();CHECK(delayedSnapshot!=original);
    auto tooLate=delayed;tooLate["source_frame_age_ms"]=15001.0;
    tooLate["class_index"]=2;tooLate["probabilities"]=QJsonArray{.05,.05,.9};tooLate["confidence"]=.9;tooLate["state"]="hand_present";
    CHECK(publish(fixture.path(),tooLate));page->refresh();CHECK(snapshot()==delayedSnapshot);
    auto delayedError=delayed;delayedError["status"]="inference_error";
    CHECK(publish(fixture.path(),delayedError));page->refresh();CHECK(snapshot()==delayedSnapshot);
    CHECK(capture("retained-completed-ten-seconds"));
    good["published_monotonic_ms"]=double(nowMs());CHECK(publish(fixture.path(),good));page->refresh();CHECK(snapshot()==original);
    // Complete valid replacement updates all three bars and the class together.
    auto next=result(fixture.path(),"camera-one","2");CHECK(red.save(next["frame_path"].toString(),"JPG"));
    next["class_index"]=2;next["probabilities"]=QJsonArray{.2,.25,.55};next["confidence"]=.55;next["state"]="uncertain";
    CHECK(publish(fixture.path(),next));page->refresh();
    CHECK(prediction->text()==QStringLiteral("预测：不确定")&&label("liveConfidence")->text().contains("55.0%"));
    CHECK(window.findChild<QProgressBar*>("liveProbability0")->value()==200&&window.findChild<QProgressBar*>("liveProbability1")->value()==250&&window.findChild<QProgressBar*>("liveProbability2")->value()==550);
    const auto uncertain=snapshot();CHECK(uncertain!=original);
    auto partial=next;partial["class_index"]=0;partial["confidence"]=.99;
    CHECK(publish(fixture.path(),partial));page->refresh();CHECK(snapshot()==uncertain);
    CHECK(capture("retained-new-uncertain"));
    page->receivePreviewState("source_stale","video timeout");page->refresh();
    CHECK(pixmap(preview).isNull()&&snapshot()==uncertain);
    page->receivePreview(blue,video("12","camera-one",1700));CHECK(!pixmap(preview).isNull());
    QThread::msleep(400);page->receivePreview(red,video("12"));page->refresh();
    CHECK(pixmap(preview).isNull()&&snapshot()==uncertain); // Duplicate video cannot renew its TTL.
    page->receivePreview(blue,video("1","camera-two"));page->refresh();
    CHECK(!pixmap(preview).isNull()&&snapshot()==uncertain);
    CHECK(info->text().contains(QStringLiteral("上次相机会话结果，等待新识别")));
    CHECK(capture("retained-previous-session"));
    auto restarted=result(fixture.path(),"camera-two","2");CHECK(red.save(restarted["frame_path"].toString(),"JPG"));
    restarted["class_index"]=2;restarted["probabilities"]=QJsonArray{.1,.1,.8};restarted["confidence"]=.8;restarted["state"]="hand_present";
    CHECK(publish(fixture.path(),restarted));page->refresh();
    CHECK(prediction->text()==QStringLiteral("预测：手部侵入")&&label("liveConfidence")->text().contains("80.0%"));
    CHECK(window.findChild<QProgressBar*>("liveProbability0")->value()==100&&window.findChild<QProgressBar*>("liveProbability1")->value()==100&&window.findChild<QProgressBar*>("liveProbability2")->value()==800);
    CHECK(!info->text().contains(QStringLiteral("上次相机会话"))&&!status->text().contains(QStringLiteral("保留")));
    CHECK(capture("retained-new-session-update"));
    std::puts("RETAINED_RESULT_SMOKE_PASS: first waiting no dashes; valid snapshot retained through stale/errors/partial JSON/model mismatch; age grows; atomic full replacement; uncertain/80percent; old-session label; independent video2s TTL; duplicate video not renewed; slow inference allowed; complete10s result accepted as history; 15s deadline retains previous");
    return 0;
}
