QT += core gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tracking_stream_fixture
INCLUDEPATH += $$PWD/..
SOURCES += $$PWD/tracking_stream_fixture.cpp $$PWD/../latest_frame_server.cpp $$PWD/../tracking_centers.cpp
HEADERS += $$PWD/../latest_frame_server.hpp $$PWD/../tracking_centers.hpp $$PWD/../fast_tracker.hpp
include(../tracking_opencv.pri)
LIBS += -lpthread
