#ifndef CAMERA_CONSOLE_HPP
#define CAMERA_CONSOLE_HPP

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <functional>

// Local, same-user access to one photo held in RAM; no filesystem export.
class CameraConsole : public QObject {
public:
    using Handler = std::function<QJsonObject(const QJsonObject &)>;
    explicit CameraConsole(Handler handler, QObject *parent = nullptr);
    ~CameraConsole() override;
    bool start(QString &error);
    static int command(const QString &json);
    static int stdioBridge();
private:
    struct Private;
    Private *d;
};
#endif
