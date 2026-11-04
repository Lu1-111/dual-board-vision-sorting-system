#include "latest_frame_server.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>
#include <cstdio>
#include <unistd.h>

// Hardware-free protocol fixture. stdin controls only this test publisher.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 && argc != 5) return 2;
    LatestFrameServer server;
    QString error;
    if (!server.start("127.0.0.1", QString::fromLocal8Bit(argv[1]).toUShort(), 3, 95, error,
                      argc == 5 ? QString::fromLocal8Bit(argv[2]).toInt() : 15,
                      argc == 5 ? QString::fromLocal8Bit(argv[3]).toInt() : 640,
                      argc == 5 ? QString::fromLocal8Bit(argv[4]).toInt() : 75)) {
        std::fprintf(stderr, "%s\n", qPrintable(error)); return 3;
    }
    QImage image(1920, 1080, QImage::Format_RGB32);
    quint32 random = 12345;
    for (int y = 0; y < image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            random = random * 1664525U + 1013904223U;
            row[x] = 0xff000000U | (random & 0x00ffffffU);
        }
    }
    bool publishing = true;
    quint64 sequence = 0;
    server.beginSession(true);
    QTimer frames;
    QObject::connect(&frames, &QTimer::timeout, &app, [&]() {
        if (publishing) server.offerFrame(image, QDateTime::currentDateTimeUtc(), ++sequence);
    });
    frames.start(30);
    QSocketNotifier commands(STDIN_FILENO, QSocketNotifier::Read);
    QByteArray input;
    QObject::connect(&commands, &QSocketNotifier::activated, &app, [&](int) {
        char bytes[128];
        const ssize_t count = ::read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count <= 0) { app.quit(); return; }
        input.append(bytes, static_cast<int>(count));
        for (;;) {
            const int eol = input.indexOf('\n');
            if (eol < 0) break;
            const QByteArray cmd = input.left(eol);
            input.remove(0, eol + 1);
            if (cmd == "pause") publishing = false;
            if (cmd == "resume") publishing = true;
            if (cmd == "stop") { publishing = false; server.stopSession("fixture_stopped"); }
            if (cmd == "begin") { sequence = 0; server.beginSession(true); publishing = true; }
            if (cmd == "quit") app.quit();
            std::printf("ACK %s\n", cmd.constData()); std::fflush(stdout);
        }
    });
    std::puts("READY"); std::fflush(stdout);
    return app.exec();
}
