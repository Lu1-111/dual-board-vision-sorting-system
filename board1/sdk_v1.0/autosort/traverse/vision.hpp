#ifndef CONTINUOUS_VISION_HPP
#define CONTINUOUS_VISION_HPP
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>
#include <QVector>
#include <QString>
#include <stdexcept>
namespace trial {
void require(bool,const char*);
class VisionUnavailable:public std::runtime_error {public:using std::runtime_error::runtime_error;};
void visionRequire(bool,const char*);
struct Frame {QString session,id;quint64 seq=0;QPointF center;qint64 captured=0;QJsonObject raw;};
QVector<QPointF> defaultPolygon();
void validatePolygon(const QVector<QPointF>&);
Frame readFrame(const QJsonObject&,qint64,const QVector<QPointF>&,bool allowEmpty=false);
void checkArm(const QJsonObject&,int expectedJob=10);
class Guard {QString session_,id_;quint64 seq_=0;qint64 captured_=-1;bool initialized_=false;QJsonObject last_;public:bool accept(const Frame&);};
}
#endif
