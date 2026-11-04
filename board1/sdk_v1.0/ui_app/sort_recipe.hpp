#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <array>

struct SortWaypoint {
    QString name;
    std::array<double,4> coordinate;
    double claw=0,roll=0;
    int durationMs=1000,holdMs=0;
};
// No hardware access. Parse a confirmed recipe and freeze one object's route.
struct SortRecipe {
    QString className,zone;
    QVector<SortWaypoint> waypoints;
    std::array<double,6> observationPulses;
    QJsonObject reference;
    int returnMs=1000;
    static SortRecipe build(const QJsonObject &config,const QString &className,double x,double y);
    static bool referenceMatches(QJsonObject a,QJsonObject b);
};
