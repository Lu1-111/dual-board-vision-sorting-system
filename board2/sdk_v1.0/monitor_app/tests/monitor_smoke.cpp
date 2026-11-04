// 板2 监控终端离屏烟测：模拟数据源压缩到 25ms/拍，跑约 2 秒，
// 覆盖三类告警的触发与恢复；校验三页导航、告警表，
// 可选输出三页截图（argv[1] 为截图路径，argv[2] 为字体目录）。
#include "../monitorwindow.h"
#include "../simdatasource.h"
#include "../overviewpage.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QTextBrowser>
#include <QThread>
#include <cstdio>

static void loadFonts(const QString &explicitDir)
{
    QString fontDir = explicitDir;
    if (fontDir.isEmpty()) {
        const QStringList candidates = QStringList()
            << QApplication::applicationDirPath() + QStringLiteral("/fonts")
            << QApplication::applicationDirPath()
                   + QStringLiteral("/../../ui_app/fonts")
            << QDir::currentPath() + QStringLiteral("/../ui_app/fonts");
        for (const QString &candidate : candidates) {
            if (QDir(candidate).exists()) { fontDir = candidate; break; }
        }
    }
    if (fontDir.isEmpty()) return;
    QFontDatabase::addApplicationFont(fontDir + QStringLiteral("/DejaVuSans.ttf"));
    const int fontId = QFontDatabase::addApplicationFont(
        fontDir + QStringLiteral("/DroidSansFallbackFull.ttf"));
    QString family = QStringLiteral("Noto Sans CJK SC");
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) family = families.first();
    }
    QFont font(family);
    font.setPointSize(10);
    QApplication::setFont(font);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    loadFonts(argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString());

    SimDataSource source(25);
    MonitorWindow window(&source);
    window.resize(1024, 600);
    window.show();
    app.processEvents();

    auto *pages = window.findChild<QStackedWidget *>();
    if (!pages || pages->count() != 3) return 17;

    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 2000) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(5);
    }

    auto *comm = window.findChild<QLabel *>(QStringLiteral("commValue"));
    if (!comm || !comm->text().contains(QStringLiteral("正常"))) return 1;

    auto *table = window.findChild<QTableWidget *>(QStringLiteral("alarmTable"));
    if (!table) return 2;
    if (table->rowCount() < 2) return 3;  // 剧本 60s 内应已触发 3 类告警

    if (window.findChild<QPushButton *>(QStringLiteral("aiGenerate"))) return 5;
    for (auto *nav : window.findChildren<QPushButton *>()) {
        if (nav->text() == QStringLiteral("AI 分析")) return 6;
    }

    auto *scenario = window.findChild<QComboBox *>(QStringLiteral("demoScenario"));
    if (!scenario) return 20;
    scenario->setCurrentIndex(4); // Missing material must have no item or confidence.
    app.processEvents();
    auto *material = window.findChild<QLabel *>(QStringLiteral("currentMaterial"));
    auto *confidence = window.findChild<QLabel *>(QStringLiteral("confidenceValue"));
    if (!material || material->text() != QStringLiteral("—")) return 21;
    if (!confidence || confidence->text().contains('%')) return 22;
    scenario->setCurrentIndex(3);
    app.processEvents();
    auto *state = window.findChild<QLabel *>(QStringLiteral("bigState"));
    if (!state || state->text() != QStringLiteral("运行中")) return 23;
    // Navigation cannot change independent equipment state.
    for(int scene=1; scene<=3; ++scene) {
        scenario->setCurrentIndex(scene+1);
        for(const QString &title : {QStringLiteral("告警记录"),QStringLiteral("监控总览"),QStringLiteral("实时视觉")}) {
            for(auto *nav:window.findChildren<QPushButton*>()) if(nav->text()==title) nav->click();
            app.processEvents();
            if(state->text()!=QStringLiteral("运行中")) return 26;
        }
    }
    scenario->setCurrentIndex(2);
    quint32 firstDefect=0,lastDefect=0; int samples=0;
    const auto countConnection=QObject::connect(&source,&IDataSource::counters,[&](quint32,quint32,quint32,quint32,quint32 defective){if(!samples++) firstDefect=defective; lastDefect=defective;});
    elapsed.restart();
    while(elapsed.elapsed()<180) { app.processEvents(); QThread::msleep(5); }
    QObject::disconnect(countConnection);
    if(samples<3 || lastDefect<=firstDefect || state->text()!=QStringLiteral("运行中")) return 27;
    scenario->setCurrentIndex(1); // Recovery must clear active alarms.
    app.processEvents();
    auto *active = window.findChild<QListWidget *>(QStringLiteral("alarmList"));
    if (!active || active->count() != 0) return 24;

    // Exercise beyond 8-bit event IDs; stale history must not receive recovery.
    quint32 lastId = 0;
    bool monotonic = true;
    QObject::connect(&source, &IDataSource::alarmRaised, &app, [&](const AlarmEvent &e) {
        monotonic = monotonic && e.eventId > lastId;
        lastId = e.eventId;
    });
    for (int i = 0; i < 260; ++i) { source.setScenario(1); source.setScenario(0); }
    if (!monotonic || lastId <= 255) return 25;

    scenario->setCurrentIndex(3);

    // CAN still carries five color counters; the quality UI only uses explicit quality labels.
    auto *overview = static_cast<OverviewPage *>(pages->widget(0));
    overview->applyCounters(101, 102, 103, 7, 3);
    auto *good = window.findChild<QLabel *>(QStringLiteral("goodCount"));
    auto *defect = window.findChild<QLabel *>(QStringLiteral("defectCount"));
    if (!good || !defect || good->text() != "7" || defect->text() != "3") return 28;
    LineStatus qualityStatus;
    qualityStatus.runState = RunState::Running;
    qualityStatus.currentColor = MaterialColor::Red;
    qualityStatus.confidence100 = 92;
    overview->applyStatus(qualityStatus);
    if (material->text() != QStringLiteral("未判定") || confidence->text().contains('%')) return 29;
    qualityStatus.currentColor = MaterialColor::GreenGood;
    overview->applyStatus(qualityStatus);
    if (material->text() != QStringLiteral("完好") || !confidence->text().contains("92%")) return 30;
    qualityStatus.currentColor = MaterialColor::GreenDefect;
    overview->applyStatus(qualityStatus);
    if (material->text() != QStringLiteral("缺陷")) return 31;

    if (argc > 1) {
        const QFileInfo info(QString::fromLocal8Bit(argv[1]));
        const QString navTexts[3] = {
            QStringLiteral("监控总览"), QStringLiteral("告警记录"),
             QStringLiteral("实时视觉")};
        for (int i = 0; i < 3; ++i) {
            // 走导航按钮而非 setCurrentIndex，保持侧栏高亮与页面一致。
            for (auto *nav : window.findChildren<QPushButton *>()) {
                if (nav->text() == navTexts[i]) { nav->click(); break; }
            }
            app.processEvents();
            const QString path = info.absolutePath() + QStringLiteral("/")
                + info.completeBaseName()
                + QStringLiteral("-page%1.png").arg(i);
            if (!window.grab().save(path)) return 8;
        }
    }
    std::puts("monitor smoke: PASS");
    return 0;
}
