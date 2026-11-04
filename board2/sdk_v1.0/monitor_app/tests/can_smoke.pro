QT += core gui widgets network
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = can_smoke
SOURCES += can_smoke.cpp ../candatasource.cpp ../monitorwindow.cpp ../simdatasource.cpp ../overviewpage.cpp ../alarmpage.cpp ../livevisionpage.cpp
HEADERS += ../candatasource.h ../datasource.h ../monitorwindow.h ../simdatasource.h ../overviewpage.h ../alarmpage.h ../livevisionpage.h
QMAKE_CXXFLAGS += -Wall -Wextra

SOURCES += ../camerapreviewclient.cpp
HEADERS += ../camerapreviewclient.h
