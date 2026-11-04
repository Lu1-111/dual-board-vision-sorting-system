QT += core gui
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = tracking_centers_test
INCLUDEPATH += $$PWD/..
SOURCES += $$PWD/tracking_centers_test.cpp $$PWD/../tracking_centers.cpp
HEADERS += $$PWD/../tracking_centers.hpp $$PWD/../fast_tracker.hpp
include(../tracking_opencv.pri)
