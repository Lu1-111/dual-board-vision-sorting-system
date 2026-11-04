QT += core gui widgets network
CONFIG += c++11 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = monitor_smoke
SOURCES += monitor_smoke.cpp ../monitorwindow.cpp ../simdatasource.cpp \
    ../overviewpage.cpp ../alarmpage.cpp ../candatasource.cpp ../livevisionpage.cpp
HEADERS += ../monitorwindow.h ../datasource.h ../simdatasource.h \
    ../overviewpage.h ../alarmpage.h ../candatasource.h ../livevisionpage.h
QMAKE_CXXFLAGS += -Wall -Wextra

SOURCES += ../camerapreviewclient.cpp
HEADERS += ../camerapreviewclient.h
