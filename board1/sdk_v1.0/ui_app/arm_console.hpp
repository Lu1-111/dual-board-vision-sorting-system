#ifndef ARM_CONSOLE_HPP
#define ARM_CONSOLE_HPP

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <functional>

// Local, same-user transport to the existing GUI controller. No GPIO access.
class ArmConsole : public QObject {
public:
    using Handler = std::function<QJsonObject(const QJsonObject &)>;
    explicit ArmConsole(Handler handler, QObject *parent = nullptr);
    ~ArmConsole() override;
    bool start(QString &error);
    static int command(const QString &json);
private:
    struct Private;
    Private *d;
};
#endif
