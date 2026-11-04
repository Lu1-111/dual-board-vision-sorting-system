QT += core gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = live_stream_fixture
INCLUDEPATH += $$PWD/..
SOURCES += $$PWD/live_stream_fixture.cpp $$PWD/../latest_frame_server.cpp $$PWD/../tracking_centers.cpp
include(../tracking_opencv.pri)
HEADERS += $$PWD/../latest_frame_server.hpp
LIBS += -lpthread
