QT += core gui widgets
CONFIG += c++11
TEMPLATE = app
TARGET = camera_dataset_app
SOURCES += main.cpp mainwindow.cpp capture_worker.cpp dataset_store.cpp camera_console.cpp latest_frame_server.cpp tracking_centers.cpp
HEADERS += mainwindow.hpp capture_worker.hpp dataset_store.hpp camera_console.hpp latest_frame_server.hpp tracking_centers.hpp fast_tracker.hpp
include(tracking_opencv.pri)
unix:LIBS += -lpthread
target.path = /opt/camera_dataset_app
INSTALLS += target
