QT += core gui widgets network

CONFIG += c++11
CONFIG -= app_bundle

TEMPLATE = app
TARGET = monitor_ui

SOURCES += \
    main.cpp \
    candatasource.cpp \
    monitorwindow.cpp \
    simdatasource.cpp \
    overviewpage.cpp \
    livevisionpage.cpp \
    alarmpage.cpp

HEADERS += \
    monitorwindow.h \
    candatasource.h \
    datasource.h \
    simdatasource.h \
    overviewpage.h \
    livevisionpage.h \
    fusionvalidation.h \
    alarmpage.h

QMAKE_CXXFLAGS += -Wall -Wextra

SOURCES += camerapreviewclient.cpp
HEADERS += camerapreviewclient.h
