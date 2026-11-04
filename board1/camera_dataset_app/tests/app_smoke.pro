QT += core gui widgets
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = app_smoke
INCLUDEPATH += $$PWD/..
SOURCES += $$PWD/app_smoke.cpp $$PWD/../mainwindow.cpp $$PWD/../capture_worker.cpp $$PWD/../dataset_store.cpp $$PWD/../latest_frame_server.cpp $$PWD/../tracking_centers.cpp
include(../tracking_opencv.pri)
HEADERS += $$PWD/../mainwindow.hpp $$PWD/../capture_worker.hpp $$PWD/../dataset_store.hpp $$PWD/../latest_frame_server.hpp
LIBS += -lpthread
