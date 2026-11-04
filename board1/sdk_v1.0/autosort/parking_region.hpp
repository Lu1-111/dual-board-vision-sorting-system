#pragma once
#include <QPointF>
namespace autosort {
// User widened Y on 2026-11-03. Shared by arrival and classification gates.
inline bool inParkingRegion(const QPointF &p){
    return p.x()>=1217&&p.x()<=1253&&p.y()>=394.5&&p.y()<=691.5;
}
}
