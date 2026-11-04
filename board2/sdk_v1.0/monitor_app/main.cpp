#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>
#include <QPixmap>

#include "monitorwindow.h"
#include "simdatasource.h"
#include "candatasource.h"
#include <memory>
#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("monitor_ui"));
    app.setOrganizationName(QStringLiteral("DR1"));

    // 与板1 hdmi_ui 相同的中文字体方案：程序目录 fonts/ 下
    // DejaVuSans（拉丁/数字）+ DroidSansFallbackFull（中文）。
    const QString latinPath = QApplication::applicationDirPath() + QStringLiteral("/fonts/DejaVuSans.ttf");
    if (QFileInfo::exists(latinPath)) QFontDatabase::addApplicationFont(latinPath);
    QString fontFamily = QStringLiteral("Noto Sans CJK SC");
    const QString chinesePath = QApplication::applicationDirPath() + QStringLiteral("/fonts/DroidSansFallbackFull.ttf");
    int fontId = QFileInfo::exists(chinesePath) ? QFontDatabase::addApplicationFont(chinesePath) : -1;
    if (fontId < 0)
        fontId = QFontDatabase::addApplicationFont(
            QStringLiteral("/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc"));
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            fontFamily = families.first();
    }
    QFont font(fontFamily);
    font.setPointSize(10);
    app.setFont(font);

    // --sim-interval <ms>：压缩模拟剧本时间轴（快速演示 / 烟测）。
    int simIntervalMs = 1000;
    const QStringList args = app.arguments();
    const int intervalIndex = args.indexOf(QStringLiteral("--sim-interval"));
    if (intervalIndex >= 0 && intervalIndex + 1 < args.size()) {
        bool ok = false;
        const int requested = args.value(intervalIndex + 1).toInt(&ok);
        if (ok && requested >= 20) simIntervalMs = requested;
    }

    const int canIndex=args.indexOf(QStringLiteral("--can"));
    const int pageIndex=args.indexOf(QStringLiteral("--start-page"));
    const QString startPage=pageIndex >= 0 ? args.value(pageIndex+1) : QStringLiteral("overview");
    if(startPage != QStringLiteral("overview") && startPage != QStringLiteral("live")) {
        std::fprintf(stderr,"--start-page must be overview or live\n"); return 2;
    }
    const int screenshotIndex=args.indexOf(QStringLiteral("--screenshot"));
    const QString screenshot=screenshotIndex >= 0 ? args.value(screenshotIndex+1) : QString();
    if(screenshotIndex >= 0 && (screenshot.isEmpty() || screenshot.startsWith('-'))) {
        std::fprintf(stderr,"--screenshot requires a file path\n"); return 2;
    }
    const int udpIndex=args.indexOf(QStringLiteral("--can-udp"));
    if(canIndex>=0 && udpIndex>=0) { std::fprintf(stderr,"Choose --can or --can-udp, not both\n"); return 2; }
    std::unique_ptr<IDataSource> source;
    if(canIndex>=0 || udpIndex>=0) {
        const bool udp=udpIndex>=0; const QString endpoint=args.value((udp?udpIndex:canIndex)+1);
        bool ok=false; const uint port=endpoint.toUInt(&ok);
        if(endpoint.isEmpty() || endpoint.startsWith('-') || (udp&&(!ok||port<1024||port>65535))) return 2;
        source.reset(new CanDataSource(endpoint,udp,true,nullptr,args.contains(QStringLiteral("--loopback-test"))));
    } else source.reset(new SimDataSource(simIntervalMs));
    MonitorWindow window(source.get());
    if (startPage == QStringLiteral("live")) window.showLivePage();
    if (args.contains(QStringLiteral("--windowed"))) {
        window.resize(1024, 600);
        window.show();
    } else {
        window.showFullScreen();
    }
    // Diagnostic snapshot of the actual widget state; keep monitoring afterwards.
    if (!screenshot.isEmpty()) QTimer::singleShot(2000, &window, [&window, screenshot]() {
        const bool saved = window.grab().save(screenshot);
        std::fprintf(stderr, "monitor screenshot %s: %s\n", saved ? "saved" : "failed", qPrintable(screenshot));
    });
    return app.exec();
}
